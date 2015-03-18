/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The srsLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the srsLTE library.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/select.h>
#include <pthread.h>
#include <semaphore.h>

#include "srslte/srslte.h"
#include "srslte/rrc/rrc.h"

#ifndef DISABLE_UHD
#include "srslte/cuhd/cuhd.h"
void *uhd;
#endif

char *output_file_name = NULL;

#define LEFT_KEY  68
#define RIGHT_KEY 67
#define UP_KEY    65
#define DOWN_KEY  66

srslte_cell_t cell = {
  6,            // nof_prb
  1,            // nof_ports
  1,            // cell_id
  SRSLTE_SRSLTE_CP_NORM,       // cyclic prefix
  SRSLTE_PHICH_R_1,          // PHICH resources      
  SRSLTE_PHICH_NORM    // PHICH length
};
  
int net_port = -1; // -1 generates random data

uint32_t cfi=2;
uint32_t mcs_idx = 1, last_mcs_idx = 1;
int nof_frames = -1;

char *uhd_args = "";
float uhd_amp = 0.03, uhd_gain = 70.0, uhd_freq = 2400000000;

bool null_file_sink=false; 
srslte_filesink_t fsink;
srslte_fft_t ifft;
pbch_t pbch;
pcfich_t pcfich;
pdcch_t pdcch;
pdsch_t pdsch;
harq_t harq_process;
regs_t regs;
ra_pdsch_t ra_dl;  


cf_t *sf_buffer = NULL, *output_buffer = NULL;
int sf_n_re, sf_n_samples;

pthread_t net_thread; 
void *net_thread_fnc(void *arg);
sem_t net_sem;
bool net_packet_ready = false; 
srslte_netsource_t net_source; 
srslte_netsink_t net_sink; 

int prbset_num = 1, last_prbset_num = 1; 
int prbset_orig = 0; 

void usage(char *prog) {
  printf("Usage: %s [agmfoncvpu]\n", prog);
#ifndef DISABLE_UHD
  printf("\t-a UHD args [Default %s]\n", uhd_args);
  printf("\t-l UHD amplitude [Default %.2f]\n", uhd_amp);
  printf("\t-g UHD TX gain [Default %.2f dB]\n", uhd_gain);
  printf("\t-f UHD TX frequency [Default %.1f MHz]\n", uhd_freq / 1000000);
#else
  printf("\t   UHD is disabled. CUHD library not available\n");
#endif
  printf("\t-o output_file [Default USRP]\n");
  printf("\t-m MCS index [Default %d]\n", mcs_idx);
  printf("\t-n number of frames [Default %d]\n", nof_frames);
  printf("\t-c cell id [Default %d]\n", cell.id);
  printf("\t-p nof_prb [Default %d]\n", cell.nof_prb);
  printf("\t-u listen TCP port for input data (-1 is random) [Default %d]\n", net_port);
  printf("\t-v [set verbose to debug, default none]\n");
}

void parse_args(int argc, char **argv) {
  int opt;
  while ((opt = getopt(argc, argv, "aglfmoncpvu")) != -1) {
    switch (opt) {
    case 'a':
      uhd_args = argv[optind];
      break;
    case 'g':
      uhd_gain = atof(argv[optind]);
      break;
    case 'l':
      uhd_amp = atof(argv[optind]);
      break;
    case 'f':
      uhd_freq = atof(argv[optind]);
      break;
    case 'o':
      output_file_name = argv[optind];
      break;
    case 'm':
      mcs_idx = atoi(argv[optind]);
      break;
    case 'u':
      net_port = atoi(argv[optind]);
      break;
    case 'n':
      nof_frames = atoi(argv[optind]);
      break;
    case 'p':
      cell.nof_prb = atoi(argv[optind]);
      break;
    case 'c':
      cell.id = atoi(argv[optind]);
      break;
    case 'v':
      verbose++;
      break;
    default:
      usage(argv[0]);
      exit(-1);
    }
  }
#ifdef DISABLE_UHD
  if (!output_file_name) {
    usage(argv[0]);
    exit(-1);
  }
#endif
}

void base_init() {
  
  /* init memory */
  sf_buffer = malloc(sizeof(cf_t) * sf_n_re);
  if (!sf_buffer) {
    perror("malloc");
    exit(-1);
  }
  output_buffer = malloc(sizeof(cf_t) * sf_n_samples);
  if (!output_buffer) {
    perror("malloc");
    exit(-1);
  }
  /* open file or USRP */
  if (output_file_name) {
    if (strcmp(output_file_name, "NULL")) {
      if (srslte_filesink_init(&fsink, output_file_name, SRSLTE_COMPLEX_FLOAT_BIN)) {
        fprintf(stderr, "Error opening file %s\n", output_file_name);
        exit(-1);
      }      
      null_file_sink = false; 
    } else {
      null_file_sink = true; 
    }
  } else {
#ifndef DISABLE_UHD
    printf("Opening UHD device...\n");
    if (cuhd_open(uhd_args, &uhd)) {
      fprintf(stderr, "Error opening uhd\n");
      exit(-1);
    }
#else
    printf("Error UHD not available. Select an output file\n");
    exit(-1);
#endif
  }
  
  if (net_port > 0) {
    if (srslte_netsource_init(&net_source, "0.0.0.0", net_port, SRSLTE_NETSOURCE_TCP)) {
      fprintf(stderr, "Error creating input UDP socket at port %d\n", net_port);
      exit(-1);
    }
    if (null_file_sink) {
      if (srslte_netsink_init(&net_sink, "127.0.0.1", net_port+1, SRSLTE_NETSINK_TCP)) {
        fprintf(stderr, "Error sink\n");
        exit(-1);
      }      
    }
    if (sem_init(&net_sem, 0, 1)) {
      perror("sem_init");
      exit(-1);
    }
  }

  /* create ifft object */
  if (lte_ifft_init(&ifft, SRSLTE_SRSLTE_CP_NORM, cell.nof_prb)) {
    fprintf(stderr, "Error creating iFFT object\n");
    exit(-1);
  }
  srslte_fft_set_normalize(&ifft, true);
  if (pbch_init(&pbch, cell)) {
    fprintf(stderr, "Error creating PBCH object\n");
    exit(-1);
  }

  if (regs_init(&regs, cell)) {
    fprintf(stderr, "Error initiating regs\n");
    exit(-1);
  }

  if (pcfich_init(&pcfich, &regs, cell)) {
    fprintf(stderr, "Error creating PBCH object\n");
    exit(-1);
  }

  if (regs_set_cfi(&regs, cfi)) {
    fprintf(stderr, "Error setting CFI\n");
    exit(-1);
  }

  if (pdcch_init(&pdcch, &regs, cell)) {
    fprintf(stderr, "Error creating PDCCH object\n");
    exit(-1);
  }

  if (pdsch_init(&pdsch, cell)) {
    fprintf(stderr, "Error creating PDSCH object\n");
    exit(-1);
  }
  
  pdsch_set_rnti(&pdsch, 1234);
  
  if (harq_init(&harq_process, cell)) {
    fprintf(stderr, "Error initiating HARQ process\n");
    exit(-1);
  }
}

void base_free() {

  harq_free(&harq_process);
  pdsch_free(&pdsch);
  pdcch_free(&pdcch);
  regs_free(&regs);
  pbch_free(&pbch);

  lte_ifft_free(&ifft);

  if (sf_buffer) {
    free(sf_buffer);
  }
  if (output_buffer) {
    free(output_buffer);
  }
  if (output_file_name) {
    if (!null_file_sink) {
      srslte_filesink_free(&fsink);      
    }
  } else {
#ifndef DISABLE_UHD
    cuhd_close(&uhd);
#endif
  }
  
  if (net_port > 0) {
    srslte_netsource_free(&net_source);
    sem_close(&net_sem);
  }  
}

unsigned int
reverse(register unsigned int x)
{
    x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
    x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
    x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
    x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));
    return((x >> 16) | (x << 16));

}

uint32_t prbset_to_bitmask() {
  uint32_t mask=0;
  int nb = (int) ceilf((float) cell.nof_prb / ra_type0_P(cell.nof_prb));
  for (int i=0;i<nb;i++) {
    if (i >= prbset_orig && i < prbset_orig + prbset_num) {
      mask = mask | (0x1<<i);     
    }
  }
  return reverse(mask)>>(32-nb); 
}

int update_radl(uint32_t sf_idx) {
  ra_dl_alloc_t prb_alloc;
  
  bzero(&ra_dl, sizeof(ra_pdsch_t));
  ra_dl.harq_process = 0;
  ra_dl.mcs_idx = mcs_idx;
  ra_dl.ndi = 0;
  ra_dl.rv_idx = 0;
  ra_dl.alloc_type = alloc_type0;
  ra_dl.type0_alloc.rbg_bitmask = prbset_to_bitmask();
    
  ra_dl_alloc(&prb_alloc, &ra_dl, cell.nof_prb);
  ra_dl_alloc_re(&prb_alloc, cell.nof_prb, 1, cell.nof_prb<10?(cfi+1):cfi, SRSLTE_SRSLTE_CP_NORM);
  ra_mcs_from_idx_dl(mcs_idx, prb_alloc.slot[0].nof_prb, &ra_dl.mcs);

  ra_pdsch_fprint(stdout, &ra_dl, cell.nof_prb);
  printf("Type new MCS index and press Enter: "); fflush(stdout);
  
  harq_reset(&harq_process);
  if (harq_setup_dl(&harq_process, ra_dl.mcs, ra_dl.rv_idx, sf_idx, &prb_alloc)) {
    fprintf(stderr, "Error configuring HARQ process\n");
    return -1; 
  }
  
  return 0; 
}

/* Read new MCS from stdin */
int update_control(uint32_t sf_idx) {
  char input[128];
  
  fd_set set; 
  FD_ZERO(&set);
  FD_SET(0, &set);
  
  struct timeval to; 
  to.tv_sec = 0; 
  to.tv_usec = 0; 

  int n = select(1, &set, NULL, NULL, &to);
  if (n == 1) {
    // stdin ready
    if (fgets(input, sizeof(input), stdin)) {
      if(input[0] == 27) {
        switch(input[2]) {
          case RIGHT_KEY:
            if (prbset_orig  + prbset_num < (int) ceilf((float) cell.nof_prb / ra_type0_P(cell.nof_prb)))
              prbset_orig++;
            break;
          case LEFT_KEY:
            if (prbset_orig > 0)
              prbset_orig--;
            break;
          case UP_KEY:
            if (prbset_num < (int) ceilf((float) cell.nof_prb / ra_type0_P(cell.nof_prb)))
              prbset_num++;
            break;
          case DOWN_KEY:
            last_prbset_num = prbset_num;
            if (prbset_num > 0)
              prbset_num--;          
            break;          
        }
      } else {
        last_mcs_idx = mcs_idx; 
        mcs_idx = atoi(input);          
      }
      bzero(input,sizeof(input));
      if (update_radl(sf_idx)) {
        printf("Trying with last known MCS index\n");
        mcs_idx = last_mcs_idx; 
        prbset_num = last_prbset_num; 
        return update_radl(sf_idx);
      }
    }
    return 0; 
  } else if (n < 0) {
    // error
    perror("select");
    return -1; 
  } else {
    return 0; 
  }
}

#define DATA_BUFF_SZ    1000
uint8_t data[8*DATA_BUFF_SZ], data_unpacked[DATA_BUFF_SZ];
uint8_t data_tmp[DATA_BUFF_SZ];

/** Function run in a separate thread to receive UDP data */
void *net_thread_fnc(void *arg) {
  int n; 
  int rpm = 0, wpm=0; 
  
  do {
    n = srslte_netsource_read(&net_source, &data_unpacked[rpm], DATA_BUFF_SZ-rpm);
    if (n > 0) {
      int nbytes = 1+(ra_dl.mcs.tbs-1)/8;
      rpm += n; 
      INFO("received %d bytes. rpm=%d/%d\n",n,rpm,nbytes);
      wpm = 0; 
      while (rpm >= nbytes) {
        // wait for packet to be transmitted
        sem_wait(&net_sem);
        bit_pack_vector(&data_unpacked[wpm], data, nbytes*8);          
        INFO("Sent %d/%d bytes ready\n", nbytes, rpm);
        rpm -= nbytes;          
        wpm += nbytes; 
        net_packet_ready = true; 
      }
      if (wpm > 0) {
        INFO("%d bytes left in buffer for next packet\n", rpm);
        memcpy(data_unpacked, &data_unpacked[wpm], rpm * sizeof(uint8_t));
      }
    } else if (n == 0) {
      rpm = 0; 
    } else {
      fprintf(stderr, "Error receiving from network\n");
      exit(-1);
    }      
  } while(n >= 0);
  return NULL;
}

int main(int argc, char **argv) {
  int nf=0, sf_idx=0, N_id_2=0;
  cf_t pss_signal[PSS_LEN];
  float sss_signal0[SSS_LEN]; // for subframe 0
  float sss_signal5[SSS_LEN]; // for subframe 5
  uint8_t bch_payload[BCH_PAYLOAD_LEN], bch_payload_packed[BCH_PAYLOAD_LEN/8];
  int i;
  cf_t *sf_symbols[SRSLTE_MAX_PORTS];
  cf_t *slot1_symbols[SRSLTE_MAX_PORTS];
  dci_msg_t dci_msg;
  dci_location_t locations[SRSLTE_NSUBFRAMES_X_FRAME][30];
  uint32_t sfn; 
  srslte_chest_dl_t est; 
  
#ifdef DISABLE_UHD
  if (argc < 3) {
    usage(argv[0]);
    exit(-1);
  }
#endif

  parse_args(argc, argv);

  N_id_2 = cell.id % 3;
  sf_n_re = 2 * SRSLTE_SRSLTE_SRSLTE_CP_NORM_NSYMB * cell.nof_prb * SRSLTE_NRE;
  sf_n_samples = 2 * SRSLTE_SLOT_LEN(srslte_symbol_sz(cell.nof_prb));

  cell.phich_length = SRSLTE_PHICH_NORM;
  cell.phich_resources = SRSLTE_PHICH_R_1;
  sfn = 0;

  prbset_num = (int) ceilf((float) cell.nof_prb / ra_type0_P(cell.nof_prb)); 
  last_prbset_num = prbset_num; 
  
  /* this *must* be called after setting slot_len_* */
  base_init();

  /* Generate PSS/SSS signals */
  pss_generate(pss_signal, N_id_2);
  sss_generate(sss_signal0, sss_signal5, cell.id);
  
  /* Generate CRS signals */
  if (srslte_chest_dl_init(&est, cell)) {
    fprintf(stderr, "Error initializing equalizer\n");
    exit(-1);
  }

  for (i = 0; i < SRSLTE_MAX_PORTS; i++) { // now there's only 1 port
    sf_symbols[i] = sf_buffer;
    slot1_symbols[i] = &sf_buffer[SRSLTE_SLOT_LEN_RE(cell.nof_prb, cell.cp)];
  }

#ifndef DISABLE_UHD
  if (!output_file_name) {
    printf("Set TX rate: %.2f MHz\n",
        cuhd_set_tx_srate(uhd, srslte_sampling_freq_hz(cell.nof_prb)) / 1000000);
    printf("Set TX gain: %.1f dB\n", cuhd_set_tx_gain(uhd, uhd_gain));
    printf("Set TX freq: %.2f MHz\n",
        cuhd_set_tx_freq(uhd, uhd_freq) / 1000000);
  }
#endif

  if (update_radl(sf_idx)) {
    exit(-1);
  }
  
  if (net_port > 0) {
    if (pthread_create(&net_thread, NULL, net_thread_fnc, NULL)) {
      perror("pthread_create");
      exit(-1);
    }
  }
  
  /* Initiate valid DCI locations */
  for (i=0;i<SRSLTE_NSUBFRAMES_X_FRAME;i++) {
    pdcch_ue_locations(&pdcch, locations[i], 30, i, cfi, 1234);
    
  }
    
  nf = 0;
  
  bool send_data = false; 

  while (nf < nof_frames || nof_frames == -1) {
    for (sf_idx = 0; sf_idx < SRSLTE_NSUBFRAMES_X_FRAME && (nf < nof_frames || nof_frames == -1); sf_idx++) {
      bzero(sf_buffer, sizeof(cf_t) * sf_n_re);

      if (sf_idx == 0 || sf_idx == 5) {
        pss_put_slot(pss_signal, sf_buffer, cell.nof_prb, SRSLTE_SRSLTE_CP_NORM);
        sss_put_slot(sf_idx ? sss_signal5 : sss_signal0, sf_buffer, cell.nof_prb,
            SRSLTE_SRSLTE_CP_NORM);
      }

      srslte_refsignal_cs_put_sf(cell, 0, est.csr_signal.pilots[0][sf_idx], sf_buffer);

      bcch_bch_pack(&cell, sfn, bch_payload_packed, BCH_PAYLOAD_LEN/8);
      bit_pack_vector(bch_payload_packed, bch_payload, BCH_PAYLOAD_LEN);
      if (sf_idx == 0) {
        pbch_encode(&pbch, bch_payload, slot1_symbols);
      }

      pcfich_encode(&pcfich, cfi, sf_symbols, sf_idx);       

      /* Update DL resource allocation from control port */
      if (update_control(sf_idx)) {
        fprintf(stderr, "Error updating parameters from control port\n");
      }
      
      /* Transmit PDCCH + PDSCH only when there is data to send */
      if (sf_idx != 0 && sf_idx != 5) {
        if (net_port > 0) {
          send_data = net_packet_ready; 
          if (net_packet_ready) {
            INFO("Transmitting packet\n",0);
          }
        } else {
          INFO("SF: %d, Generating %d random bits\n", sf_idx, ra_dl.mcs.tbs);
          for (i=0;i<ra_dl.mcs.tbs;i++) {
            data[i] = rand()%2;
          }
          send_data = true; 
        }        
      } else {
        send_data = false; 
      }
      
      if (send_data) {
        dci_msg_pack_pdsch(&ra_dl, &dci_msg, Format1, cell.nof_prb, false);
        INFO("Putting DCI to location: n=%d, L=%d\n", locations[sf_idx][0].ncce, locations[sf_idx][0].L);
        if (pdcch_encode(&pdcch, &dci_msg, locations[sf_idx][0], 1234, sf_symbols, sf_idx, cfi)) {
          fprintf(stderr, "Error encoding DCI message\n");
          exit(-1);
        }
        
        if (pdsch_encode(&pdsch, &harq_process, data, sf_symbols)) {
          fprintf(stderr, "Error encoding PDSCH\n");
          exit(-1);
        }        
        if (net_port > 0 && net_packet_ready) {
          if (null_file_sink) {
            bit_unpack_vector(data, data_tmp, ra_dl.mcs.tbs);
            if (srslte_netsink_write(&net_sink, data_tmp, 1+(ra_dl.mcs.tbs-1)/8) < 0) {
              fprintf(stderr, "Error sending data through UDP socket\n");
            }            
          }
          net_packet_ready = false; 
          sem_post(&net_sem);
        }
      }
      
      /* Transform to OFDM symbols */
      lte_ifft_run_sf(&ifft, sf_buffer, output_buffer);
      
      float norm_factor = (float) cell.nof_prb/15/sqrtf(ra_dl.prb_alloc.slot[0].nof_prb);
      vec_sc_prod_cfc(output_buffer, uhd_amp*norm_factor, output_buffer, SRSLTE_SF_LEN_PRB(cell.nof_prb));
      
      /* send to file or usrp */
      if (output_file_name) {
        if (!null_file_sink) {
          srslte_filesink_write(&fsink, output_buffer, sf_n_samples);          
        }
        usleep(1000);
      } else {
#ifndef DISABLE_UHD
        vec_sc_prod_cfc(output_buffer, uhd_amp, output_buffer, sf_n_samples);
        cuhd_send(uhd, output_buffer, sf_n_samples, true);
#endif
      }
      nf++;
    }
    sfn = (sfn + 1) % 1024;
  }

  base_free();

  printf("Done\n");
  exit(0);
}


