/**
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srslte/phy/ue/ue_dl_nr.h"
#include <complex.h>

#define UE_DL_NR_PDCCH_CORR_DEFAULT_THR 0.5f
#define UE_DL_NR_PDCCH_EPRE_DEFAULT_THR -10.0f

static int ue_dl_nr_alloc_prb(srslte_ue_dl_nr_t* q, uint32_t new_nof_prb)
{
  if (q->max_prb < new_nof_prb) {
    q->max_prb = new_nof_prb;

    srslte_chest_dl_res_free(&q->chest);
    if (srslte_chest_dl_res_init(&q->chest, q->max_prb) < SRSLTE_SUCCESS) {
      return SRSLTE_ERROR;
    }

    for (uint32_t i = 0; i < q->nof_rx_antennas; i++) {
      if (q->sf_symbols[i] != NULL) {
        free(q->sf_symbols[i]);
      }

      q->sf_symbols[i] = srslte_vec_cf_malloc(SRSLTE_SLOT_LEN_RE_NR(q->max_prb));
      if (q->sf_symbols[i] == NULL) {
        ERROR("Malloc");
        return SRSLTE_ERROR;
      }
    }
  }

  return SRSLTE_SUCCESS;
}

int srslte_ue_dl_nr_init(srslte_ue_dl_nr_t* q, cf_t* input[SRSLTE_MAX_PORTS], const srslte_ue_dl_nr_args_t* args)
{
  if (!q || !input || !args) {
    return SRSLTE_ERROR_INVALID_INPUTS;
  }

  if (args->nof_rx_antennas == 0) {
    ERROR("Error invalid number of antennas (%d)", args->nof_rx_antennas);
    return SRSLTE_ERROR;
  }

  q->nof_rx_antennas = args->nof_rx_antennas;
  if (isnormal(args->pdcch_dmrs_corr_thr)) {
    q->pdcch_dmrs_corr_thr = args->pdcch_dmrs_corr_thr;
  } else {
    q->pdcch_dmrs_corr_thr = UE_DL_NR_PDCCH_CORR_DEFAULT_THR;
  }
  if (isnormal(args->pdcch_dmrs_epre_thr)) {
    q->pdcch_dmrs_epre_thr = args->pdcch_dmrs_epre_thr;
  } else {
    q->pdcch_dmrs_epre_thr = UE_DL_NR_PDCCH_EPRE_DEFAULT_THR;
  }

  if (srslte_pdsch_nr_init_ue(&q->pdsch, &args->pdsch) < SRSLTE_SUCCESS) {
    return SRSLTE_ERROR;
  }

  if (srslte_pdcch_nr_init_rx(&q->pdcch, &args->pdcch)) {
    return SRSLTE_ERROR;
  }

  if (ue_dl_nr_alloc_prb(q, args->nof_max_prb)) {
    ERROR("Error allocating");
    return SRSLTE_ERROR;
  }

  srslte_ofdm_cfg_t fft_cfg = {};
  fft_cfg.nof_prb           = args->nof_max_prb;
  fft_cfg.symbol_sz         = srslte_symbol_sz(args->nof_max_prb);
  fft_cfg.keep_dc           = true;

  for (uint32_t i = 0; i < q->nof_rx_antennas; i++) {
    fft_cfg.in_buffer  = input[i];
    fft_cfg.out_buffer = q->sf_symbols[i];
    srslte_ofdm_rx_init_cfg(&q->fft[i], &fft_cfg);
  }

  if (srslte_dmrs_sch_init(&q->dmrs_pdsch, true) < SRSLTE_SUCCESS) {
    ERROR("Error DMRS");
    return SRSLTE_ERROR;
  }

  q->pdcch_ce = SRSLTE_MEM_ALLOC(srslte_dmrs_pdcch_ce_t, 1);
  if (q->pdcch_ce == NULL) {
    ERROR("Error alloc");
    return SRSLTE_ERROR;
  }

  return SRSLTE_SUCCESS;
}

void srslte_ue_dl_nr_free(srslte_ue_dl_nr_t* q)
{
  if (q == NULL) {
    return;
  }

  for (uint32_t i = 0; i < SRSLTE_MAX_PORTS; i++) {
    srslte_ofdm_rx_free(&q->fft[i]);

    if (q->sf_symbols[i] != NULL) {
      free(q->sf_symbols[i]);
    }
  }

  srslte_chest_dl_res_free(&q->chest);
  srslte_pdsch_nr_free(&q->pdsch);
  srslte_dmrs_sch_free(&q->dmrs_pdsch);

  for (uint32_t i = 0; i < SRSLTE_UE_DL_NR_MAX_NOF_CORESET; i++) {
    srslte_dmrs_pdcch_estimator_free(&q->dmrs_pdcch[i]);
  }
  srslte_pdcch_nr_free(&q->pdcch);

  if (q->pdcch_ce) {
    free(q->pdcch_ce);
  }

  SRSLTE_MEM_ZERO(q, srslte_ue_dl_nr_t, 1);
}

int srslte_ue_dl_nr_set_carrier(srslte_ue_dl_nr_t* q, const srslte_carrier_nr_t* carrier)
{
  if (srslte_pdsch_nr_set_carrier(&q->pdsch, carrier) < SRSLTE_SUCCESS) {
    return SRSLTE_ERROR;
  }

  if (srslte_dmrs_sch_set_carrier(&q->dmrs_pdsch, carrier) < SRSLTE_SUCCESS) {
    ERROR("Error DMRS");
    return SRSLTE_ERROR;
  }

  if (ue_dl_nr_alloc_prb(q, carrier->nof_prb)) {
    ERROR("Error allocating");
    return SRSLTE_ERROR;
  }

  if (carrier->nof_prb != q->carrier.nof_prb) {
    for (uint32_t i = 0; i < q->nof_rx_antennas; i++) {
      srslte_ofdm_cfg_t cfg = {};
      cfg.nof_prb           = carrier->nof_prb;
      cfg.symbol_sz         = srslte_min_symbol_sz_rb(carrier->nof_prb);
      cfg.cp                = SRSLTE_CP_NORM;
      cfg.keep_dc           = true;
      srslte_ofdm_rx_init_cfg(&q->fft[i], &cfg);
    }
  }

  q->carrier = *carrier;

  return SRSLTE_SUCCESS;
}

int srslte_ue_dl_nr_set_pdcch_config(srslte_ue_dl_nr_t* q, const srslte_ue_dl_nr_pdcch_cfg_t* cfg)
{
  if (q == NULL || cfg == NULL) {
    return SRSLTE_ERROR_INVALID_INPUTS;
  }

  // Copy new configuration
  q->cfg = *cfg;

  // iterate over all possible CORESET and initialise/update the present ones
  for (uint32_t i = 0; i < SRSLTE_UE_DL_NR_MAX_NOF_CORESET; i++) {
    if (cfg->coreset_present[i]) {
      if (srslte_dmrs_pdcch_estimator_init(&q->dmrs_pdcch[i], &q->carrier, &cfg->coreset[i]) < SRSLTE_SUCCESS) {
        return SRSLTE_ERROR;
      }
    }
  }

  return SRSLTE_SUCCESS;
}

void srslte_ue_dl_nr_estimate_fft(srslte_ue_dl_nr_t* q, const srslte_slot_cfg_t* slot_cfg)
{
  if (q == NULL || slot_cfg == NULL) {
    return;
  }

  // OFDM demodulation
  for (uint32_t i = 0; i < q->nof_rx_antennas; i++) {
    srslte_ofdm_rx_sf(&q->fft[i]);
  }

  // Estimate PDCCH channel for every configured CORESET
  for (uint32_t i = 0; i < SRSLTE_UE_DL_NR_MAX_NOF_CORESET; i++) {
    if (q->cfg.coreset_present[i]) {
      srslte_dmrs_pdcch_estimate(&q->dmrs_pdcch[i], slot_cfg, q->sf_symbols[0]);
    }
  }
}

static int ue_dl_nr_find_dci_ncce(srslte_ue_dl_nr_t*     q,
                                  srslte_dci_msg_nr_t*   dci_msg,
                                  srslte_pdcch_nr_res_t* pdcch_res,
                                  uint32_t               coreset_id)
{
  // Select debug information
  srslte_ue_dl_nr_pdcch_info_t* pdcch_info = NULL;
  if (q->pdcch_info_count < SRSLTE_MAX_NOF_CANDIDATES_SLOT_NR) {
    pdcch_info = &q->pdcch_info[q->pdcch_info_count];
    q->pdcch_info_count++;
  } else {
    ERROR("The UE does not expect more than %d candidates in this serving cell", SRSLTE_MAX_NOF_CANDIDATES_SLOT_NR);
    return SRSLTE_ERROR;
  }
  SRSLTE_MEM_ZERO(pdcch_info, srslte_ue_dl_nr_pdcch_info_t, 1);
  pdcch_info->coreset_id         = dci_msg->coreset_id;
  pdcch_info->ss_id              = dci_msg->search_space;
  pdcch_info->location           = dci_msg->location;
  srslte_dmrs_pdcch_measure_t* m = &pdcch_info->measure;

  // Measures the PDCCH transmission DMRS
  if (srslte_dmrs_pdcch_get_measure(&q->dmrs_pdcch[coreset_id], &dci_msg->location, m) < SRSLTE_SUCCESS) {
    ERROR("Error getting measure location L=%d, ncce=%d", dci_msg->location.L, dci_msg->location.ncce);
    return SRSLTE_ERROR;
  }

  // If measured correlation is invalid, early return
  if (!isnormal(m->norm_corr)) {
    INFO("Discarded PDCCH candidate L=%d;ncce=%d; Invalid measurement;", dci_msg->location.L, dci_msg->location.ncce);
    return SRSLTE_SUCCESS;
  }

  // Compare EPRE with threshold
  if (m->epre_dBfs < q->pdcch_dmrs_epre_thr) {
    INFO("Discarded PDCCH candidate L=%d;ncce=%d; EPRE is too weak (%.1f<%.1f);",
         dci_msg->location.L,
         dci_msg->location.ncce,
         m->epre_dBfs,
         q->pdcch_dmrs_epre_thr);
    return SRSLTE_SUCCESS;
  }

  // Compare DMRS correlation with threshold
  if (m->norm_corr < q->pdcch_dmrs_corr_thr) {
    INFO("Discarded PDCCH candidate L=%d;ncce=%d; Correlation is too low (%.1f<%.1f); EPRE=%+.2f; RSRP=%+.2f;",
         dci_msg->location.L,
         dci_msg->location.ncce,
         m->norm_corr,
         q->pdcch_dmrs_corr_thr,
         m->epre_dBfs,
         m->rsrp_dBfs);
    return SRSLTE_SUCCESS;
  }

  // Extract PDCCH channel estimates
  if (srslte_dmrs_pdcch_get_ce(&q->dmrs_pdcch[coreset_id], &dci_msg->location, q->pdcch_ce) < SRSLTE_SUCCESS) {
    ERROR("Error extracting PDCCH DMRS");
    return SRSLTE_ERROR;
  }

  // Decode PDCCH
  if (srslte_pdcch_nr_decode(&q->pdcch, q->sf_symbols[0], q->pdcch_ce, dci_msg, pdcch_res) < SRSLTE_SUCCESS) {
    ERROR("Error decoding PDCCH");
    return SRSLTE_ERROR;
  }

  // Save information
  pdcch_info->result = *pdcch_res;

  return SRSLTE_SUCCESS;
}

static bool find_dci_msg(srslte_dci_msg_nr_t* dci_msg, uint32_t nof_dci_msg, srslte_dci_msg_nr_t* match)
{
  bool     found    = false;
  uint32_t nof_bits = match->nof_bits;

  for (int k = 0; k < nof_dci_msg && !found; k++) {
    if (dci_msg[k].nof_bits == nof_bits) {
      if (memcmp(dci_msg[k].payload, match->payload, nof_bits) == 0) {
        found = true;
      }
    }
  }

  return found;
}

static int ue_dl_nr_find_dl_dci_ss(srslte_ue_dl_nr_t*           q,
                                   const srslte_slot_cfg_t*     slot_cfg,
                                   const srslte_search_space_t* search_space,
                                   uint16_t                     rnti,
                                   srslte_rnti_type_t           rnti_type)
{
  // Select CORESET
  uint32_t coreset_id = search_space->coreset_id;
  if (coreset_id >= SRSLTE_UE_DL_NR_MAX_NOF_CORESET || !q->cfg.coreset_present[coreset_id]) {
    ERROR("CORESET %d is not present in search space %d", search_space->coreset_id, search_space->id);
    return SRSLTE_ERROR;
  }
  srslte_coreset_t* coreset = &q->cfg.coreset[search_space->coreset_id];

  // Set CORESET in PDCCH decoder
  if (srslte_pdcch_nr_set_carrier(&q->pdcch, &q->carrier, coreset) < SRSLTE_SUCCESS) {
    ERROR("Setting carrier and CORESETºn");
    return SRSLTE_ERROR;
  }

  // Hard-coded values
  srslte_dci_format_nr_t dci_format = srslte_dci_format_nr_1_0;

  // Calculate number of DCI bits
  int dci_nof_bits = srslte_dci_nr_format_1_0_sizeof(&q->carrier, coreset, rnti_type);
  if (dci_nof_bits <= SRSLTE_SUCCESS) {
    ERROR("Error DCI size");
    return SRSLTE_ERROR;
  }

  // Iterate all possible aggregation levels
  for (uint32_t L = 0; L < SRSLTE_SEARCH_SPACE_NOF_AGGREGATION_LEVELS_NR && q->dci_msg_count < SRSLTE_MAX_DCI_MSG_NR;
       L++) {
    // Calculate possible PDCCH DCI candidates
    uint32_t candidates[SRSLTE_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR] = {};
    int      nof_candidates                                        = srslte_pdcch_nr_locations_coreset(
        coreset, search_space, rnti, L, SRSLTE_SLOT_NR_MOD(q->carrier.numerology, slot_cfg->idx), candidates);
    if (nof_candidates < SRSLTE_SUCCESS) {
      ERROR("Error calculating DCI candidate location");
      return SRSLTE_ERROR;
    }

    // Iterate over the candidates
    for (int ncce_idx = 0; ncce_idx < nof_candidates && q->dci_msg_count < SRSLTE_MAX_DCI_MSG_NR; ncce_idx++) {
      // Set DCI context
      srslte_dci_msg_nr_t dci_msg = {};
      dci_msg.location.L          = L;
      dci_msg.location.ncce       = candidates[ncce_idx];
      dci_msg.search_space        = search_space->type;
      dci_msg.coreset_id          = search_space->coreset_id;
      dci_msg.rnti_type           = rnti_type;
      dci_msg.rnti                = rnti;
      dci_msg.format              = dci_format;
      dci_msg.nof_bits            = (uint32_t)dci_nof_bits;

      // Find and decode PDCCH transmission in the given ncce
      srslte_pdcch_nr_res_t res = {};
      if (ue_dl_nr_find_dci_ncce(q, &dci_msg, &res, coreset_id) < SRSLTE_SUCCESS) {
        return SRSLTE_ERROR;
      }

      // If the CRC was not match, move to next candidate
      if (!res.crc) {
        continue;
      }

      // Detect if the DCI was a format 0_0
      if (!srslte_dci_nr_format_1_0_valid(&dci_msg)) {
        // Change grant format to 0_0
        dci_msg.format = srslte_dci_format_nr_0_0;

        // If the pending UL grant list is full or has the dci message, keep moving
        if (q->pending_ul_dci_count >= SRSLTE_MAX_DCI_MSG_NR ||
            find_dci_msg(q->pending_ul_dci_msg, q->pending_ul_dci_count, &dci_msg)) {
          continue;
        }

        // Save the grant in the pending UL grant list
        q->pending_ul_dci_msg[q->pending_ul_dci_count] = dci_msg;
        q->pending_ul_dci_count++;

        // Move to next candidate
        continue;
      }

      // Check if the grant exists already in the message list
      if (find_dci_msg(q->dci_msg, q->dci_msg_count, &dci_msg)) {
        // The same DCI is in the list, keep moving
        continue;
      }

      INFO("Found DCI in L=%d,ncce=%d", dci_msg.location.L, dci_msg.location.ncce);
      // Append DCI message into the list
      q->dci_msg[q->dci_msg_count] = dci_msg;
      q->dci_msg_count++;
    }
  }

  return SRSLTE_SUCCESS;
}

int srslte_ue_dl_nr_find_dl_dci(srslte_ue_dl_nr_t*       q,
                                const srslte_slot_cfg_t* slot_cfg,
                                uint16_t                 rnti,
                                srslte_rnti_type_t       rnti_type,
                                srslte_dci_dl_nr_t*      dci_dl_list,
                                uint32_t                 nof_dci_msg)
{
  // Check inputs
  if (q == NULL || slot_cfg == NULL || dci_dl_list == NULL) {
    return SRSLTE_ERROR_INVALID_INPUTS;
  }

  // Limit maximum number of DCI messages to find
  nof_dci_msg = SRSLTE_MIN(nof_dci_msg, SRSLTE_MAX_DCI_MSG_NR);

  // Reset grant and blind search information counters
  q->dci_msg_count    = 0;
  q->pdcch_info_count = 0;

  // If the UE looks for a RAR and RA search space is provided, search for it
  if (q->cfg.ra_search_space_present && rnti_type == srslte_rnti_type_ra) {
    // Find DCIs in the RA search space
    int ret = ue_dl_nr_find_dl_dci_ss(q, slot_cfg, &q->cfg.ra_search_space, rnti, rnti_type);
    if (ret < SRSLTE_SUCCESS) {
      ERROR("Error searching RAR DCI");
      return SRSLTE_ERROR;
    }
  } else {
    // Iterate all possible common and UE search spaces
    for (uint32_t i = 0; i < SRSLTE_UE_DL_NR_MAX_NOF_SEARCH_SPACE && q->dci_msg_count < nof_dci_msg; i++) {
      // Skip search space if not present
      if (!q->cfg.search_space_present[i]) {
        continue;
      }

      // Find DCIs in the selected search space
      int ret = ue_dl_nr_find_dl_dci_ss(q, slot_cfg, &q->cfg.search_space[i], rnti, rnti_type);
      if (ret < SRSLTE_SUCCESS) {
        ERROR("Error searching DCI");
        return SRSLTE_ERROR;
      }
    }
  }

  // Convert found DCI messages into DL grants
  uint32_t dci_msg_count = SRSLTE_MIN(nof_dci_msg, q->dci_msg_count);
  for (uint32_t i = 0; i < dci_msg_count; i++) {
    const srslte_coreset_t* coreset = &q->cfg.coreset[q->dci_msg[i].coreset_id];
    srslte_dci_nr_format_1_0_unpack(&q->carrier, coreset, &q->dci_msg[i], &dci_dl_list[i]);
  }

  return (int)dci_msg_count;
}

int srslte_ue_dl_nr_find_ul_dci(srslte_ue_dl_nr_t*       q,
                                const srslte_slot_cfg_t* slot_cfg,
                                uint16_t                 rnti,
                                srslte_rnti_type_t       rnti_type,
                                srslte_dci_ul_nr_t*      dci_ul_list,
                                uint32_t                 nof_dci_msg)
{
  int count = 0;

  // Check inputs
  if (q == NULL || slot_cfg == NULL || dci_ul_list == NULL) {
    return SRSLTE_ERROR_INVALID_INPUTS;
  }

  // Get DCI messages from the pending list
  for (uint32_t i = 0; i < q->pending_ul_dci_count && count < nof_dci_msg; i++) {
    srslte_dci_msg_nr_t* dci_msg = &q->pending_ul_dci_msg[i];

    if (dci_msg->rnti_type != rnti_type || dci_msg->rnti != rnti) {
      continue;
    }

    const srslte_coreset_t* coreset = &q->cfg.coreset[dci_msg->coreset_id];
    if (srslte_dci_nr_format_0_0_unpack(&q->carrier, coreset, dci_msg, &dci_ul_list[count]) < SRSLTE_SUCCESS) {
      ERROR("Unpacking DCI 0_0");
      continue;
    }
    count++;
  }

  // Reset pending UL grant list
  q->pending_ul_dci_count = 0;

  return count;
}

int srslte_ue_dl_nr_decode_pdsch(srslte_ue_dl_nr_t*         q,
                                 const srslte_slot_cfg_t*   slot,
                                 const srslte_sch_cfg_nr_t* cfg,
                                 srslte_pdsch_res_nr_t*     res)
{
  if (srslte_dmrs_sch_estimate(&q->dmrs_pdsch, slot, cfg, &cfg->grant, q->sf_symbols[0], &q->chest) < SRSLTE_SUCCESS) {
    return SRSLTE_ERROR;
  }

  if (srslte_pdsch_nr_decode(&q->pdsch, cfg, &cfg->grant, &q->chest, q->sf_symbols, res) < SRSLTE_SUCCESS) {
    return SRSLTE_ERROR;
  }

  if (SRSLTE_DEBUG_ENABLED && srslte_verbose >= SRSLTE_VERBOSE_INFO && !handler_registered) {
    char str[512];
    srslte_ue_dl_nr_pdsch_info(q, cfg, res, str, sizeof(str));
    INFO("PDSCH: %s", str);
  }

  return SRSLTE_SUCCESS;
}

int srslte_ue_dl_nr_pdsch_info(const srslte_ue_dl_nr_t*     q,
                               const srslte_sch_cfg_nr_t*   cfg,
                               const srslte_pdsch_res_nr_t* res,
                               char*                        str,
                               uint32_t                     str_len)
{
  int len = 0;

  // Append PDSCH info
  len += srslte_pdsch_nr_rx_info(&q->pdsch, cfg, &cfg->grant, res, &str[len], str_len - len);

  // Append channel estimator info
  len = srslte_print_check(str, str_len, len, ",SNR=%+.1f", q->chest.snr_db);

  return len;
}

// Implements TS 38.213 Table 9.1.3-1: Value of counter DAI in DCI format 1_0 and of counter DAI or total DAI DCI format
// 1_1
static uint32_t ue_dl_nr_V_DL_DAI(uint32_t dai)
{
  return dai + 1;
}

static int ue_dl_nr_gen_ack_type2(const srslte_ue_dl_nr_harq_ack_cfg_t* cfg,
                                  const srslte_pdsch_ack_nr_t*          ack_info,
                                  srslte_uci_data_nr_t*                 uci_data)
{
  bool harq_ack_spatial_bundling =
      ack_info->use_pusch ? cfg->harq_ack_spatial_bundling_pusch : cfg->harq_ack_spatial_bundling_pucch;
  uint8_t* o_ack = uci_data->value.ack;

  uint32_t m = 0; // PDCCH with DCI format 1_0 or DCI format 1_1 monitoring occasion index: lower index corresponds to
                  // earlier PDCCH with DCI format 1_0 or DCI format 1_1 monitoring occasion
  uint32_t j       = 0;
  uint32_t V_temp  = 0;
  uint32_t V_temp2 = 0;

  uint32_t N_DL_cells = ack_info->nof_cc; // number of serving cells configured by higher layers for the UE

  // The following code follows the exact pseudo-code provided in TS 38.213 9.1.3.1 Type-2 HARQ-ACK codebook ...
  while (m < SRSLTE_UCI_NR_MAX_M) {
    uint32_t c = 0; // serving cell index: lower indexes correspond to lower RRC indexes of corresponding cell
    while (c < N_DL_cells) {
      // Get ACK information of serving cell c for the PDCH monitoring occasion m
      const srslte_pdsch_ack_m_nr_t* ack = &ack_info->cc[c].m[m];

      // Get DAI counter value
      uint32_t V_DL_CDAI = ue_dl_nr_V_DL_DAI(ack->resource.v_dai_dl);
      uint32_t V_DL_TDAI = ack->resource.dci_format_1_1 ? ue_dl_nr_V_DL_DAI(ack->resource.v_dai_dl) : UINT32_MAX;

      // Get ACK values
      uint32_t ack_tb0 = ack->value[0];
      uint32_t ack_tb1 = ack->value[1];

      // For a PDCCH monitoring occasion with DCI format 1_0 or DCI format 1_1 in the active DL BWP of a serving cell,
      // when a UE receives a PDSCH with one transport block and the value of maxNrofCodeWordsScheduledByDCI is 2, the
      // HARQ-ACK information is associated with the first transport block and the UE generates a NACK for the second
      // transport block if harq-ACK-SpatialBundlingPUCCH is not provided and generates HARQ-ACK information with
      // value of ACK for the second transport block if harq-ACK-SpatialBundlingPUCCH is provided.
      if (cfg->max_cw_sched_dci_is_2 && ack->second_tb_present) {
        ack_tb1 = harq_ack_spatial_bundling ? 1 : 0;
      }

      // if PDCCH monitoring occasion m is before an active DL BWP change on serving cell c or an active UL
      // BWP change on the PCell and an active DL BWP change is not triggered by a DCI format 1_1 in PDCCH
      // monitoring occasion m
      if (ack->dl_bwp_changed || ack->ul_bwp_changed) {
        c = c + 1;
      } else {
        if (ack->present) {
          // Load ACK resource data into UCI info
          uci_data->cfg.pucch.resource_id = ack_info->cc[c].m[m].resource.pucch_resource_id;
          uci_data->cfg.pucch.rnti        = ack_info->cc[c].m[m].resource.rnti;

          if (V_DL_CDAI <= V_temp) {
            j = j + 1;
          }

          V_temp = V_DL_CDAI;

          if (V_DL_TDAI == UINT32_MAX) {
            V_temp2 = V_DL_CDAI;
          } else {
            V_temp2 = V_DL_TDAI;
          }

          // if harq-ACK-SpatialBundlingPUCCH is not provided and m is a monitoring occasion for PDCCH with DCI format
          // 1_0 or DCI format 1_1 and the UE is configured by maxNrofCodeWordsScheduledByDCI with reception of two
          // transport blocks for at least one configured DL BWP of at least one serving cell,
          if (!harq_ack_spatial_bundling && cfg->max_cw_sched_dci_is_2) {
            o_ack[8 * j + 2 * (V_DL_CDAI - 1) + 0] = ack_tb0;
            o_ack[8 * j + 2 * (V_DL_CDAI - 1) + 1] = ack_tb1;
          }
          // elseif harq-ACK-SpatialBundlingPUCCH is provided to the UE and m is a monitoring occasion for
          // PDCCH with DCI format 1_1 and the UE is configured by maxNrofCodeWordsScheduledByDCI with
          // reception of two transport blocks in at least one configured DL BWP of a serving cell,
          else if (harq_ack_spatial_bundling && ack->resource.dci_format_1_1 && cfg->max_cw_sched_dci_is_2) {
            o_ack[4 * j + (V_DL_CDAI - 1)] = ack_tb0 & ack_tb1;
          }
          // else
          else {
            o_ack[4 * j + (V_DL_CDAI - 1)] = ack_tb0;
          }
        }
        c = c + 1;
      }
    }
    m = m + 1;
  }
  if (V_temp2 < V_temp) {
    j = j + 1;
  }

  // if harq-ACK-SpatialBundlingPUCCH is not provided to the UE and the UE is configured by
  // maxNrofCodeWordsScheduledByDCI with reception of two transport blocks for at least one configured DL BWP of a
  // serving cell,
  if (!harq_ack_spatial_bundling && cfg->max_cw_sched_dci_is_2) {
    uci_data->cfg.o_ack = 2 * (4 * j + V_temp2);
  } else {
    uci_data->cfg.o_ack = 4 * j + V_temp2;
  }

  // Implement here SPS PDSCH reception
  // ...

  return SRSLTE_SUCCESS;
}

int ue_dl_nr_pdsch_k1(const srslte_ue_dl_nr_harq_ack_cfg_t* cfg, const srslte_dci_dl_nr_t* dci_dl)
{
  // For DCI format 1_0, the PDSCH-to-HARQ_feedback timing indicator field values map to {1, 2, 3, 4, 5, 6, 7, 8}
  if (dci_dl->format == srslte_dci_format_nr_1_0) {
    return (int)dci_dl->harq_feedback + 1;
  }

  // For DCI format 1_1, if present, the PDSCH-to-HARQ_feedback timing indicator field values map to values for a set of
  // number of slots provided by dl-DataToUL-ACK as defined in Table 9.2.3-1.
  if (dci_dl->harq_feedback >= SRSLTE_MAX_NOF_DL_DATA_TO_UL || dci_dl->harq_feedback >= cfg->nof_dl_data_to_ul_ack) {
    ERROR("Out-of-range PDSCH-to-HARQ feedback index (%d, max %d)", dci_dl->harq_feedback, cfg->nof_dl_data_to_ul_ack);
    return SRSLTE_ERROR;
  }

  return (int)cfg->dl_data_to_ul_ack[dci_dl->harq_feedback];
}

int srslte_ue_dl_nr_pdsch_ack_resource(const srslte_ue_dl_nr_harq_ack_cfg_t* cfg,
                                       const srslte_dci_dl_nr_t*             dci_dl,
                                       srslte_pdsch_ack_resource_nr_t*       pdsch_ack_resource)
{
  if (cfg == NULL || dci_dl == NULL || pdsch_ack_resource == NULL) {
    return SRSLTE_ERROR_INVALID_INPUTS;
  }

  // Calculate Data to UL ACK timing k1
  int k1 = ue_dl_nr_pdsch_k1(cfg, dci_dl);
  if (k1 < SRSLTE_ERROR) {
    ERROR("Error calculating K1");
    return SRSLTE_ERROR;
  }

  // Fill PDSCH resource
  pdsch_ack_resource->dci_format_1_1    = (dci_dl->format == srslte_dci_format_nr_1_1);
  pdsch_ack_resource->k1                = k1;
  pdsch_ack_resource->v_dai_dl          = dci_dl->dai;
  pdsch_ack_resource->rnti              = dci_dl->rnti;
  pdsch_ack_resource->pucch_resource_id = dci_dl->pucch_resource;

  return SRSLTE_SUCCESS;
}

int srslte_ue_dl_nr_gen_ack(const srslte_ue_dl_nr_harq_ack_cfg_t* cfg,
                            const srslte_pdsch_ack_nr_t*          ack_info,
                            srslte_uci_data_nr_t*                 uci_data)
{
  // Check inputs
  if (cfg == NULL || ack_info == NULL || uci_data == NULL) {
    return SRSLTE_ERROR_INVALID_INPUTS;
  }

  // According TS 38.213 9.1.2 Type-1 HARQ-ACK codebook determination
  if (cfg->pdsch_harq_ack_codebook == srslte_pdsch_harq_ack_codebook_semi_static) {
    // This clause applies if the UE is configured with pdsch-HARQ-ACK-Codebook = semi-static.
    ERROR("Type-1 HARQ-ACK codebook determination is NOT implemented");
    return SRSLTE_ERROR;
  }

  // According TS 38.213 9.1.3 Type-2 HARQ-ACK codebook determination
  if (cfg->pdsch_harq_ack_codebook == srslte_pdsch_harq_ack_codebook_dynamic) {
    // This clause applies if the UE is configured with pdsch-HARQ-ACK-Codebook = dynamic.
    return ue_dl_nr_gen_ack_type2(cfg, ack_info, uci_data);
  }

  ERROR("No HARQ-ACK codebook determination is NOT implemented");
  return SRSLTE_ERROR;
}