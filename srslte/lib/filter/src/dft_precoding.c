/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The libLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the libLTE library.
 *
 * libLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include "srslte/common/phy_common.h"
#include "srslte/utils/debug.h"
#include "srslte/utils/vector.h"
#include "srslte/utils/dft.h"
#include "srslte/filter/dft_precoding.h"

/* Create DFT plans for transform precoding */
int srslte_srslte_dft_precoding_init(srslte_srslte_dft_precoding_t *q, uint32_t max_prb) 
{
  int ret = SRSLTE_ERROR_INVALID_INPUTS; 
  bzero(q, sizeof(srslte_srslte_dft_precoding_t));
  
  if (max_prb <= SRSLTE_MAX_PRB) {
    ret = SRSLTE_ERROR; 
    for (uint32_t i=1;i<max_prb;i++) {
      if(srslte_srslte_dft_precoding_valid_prb(i)) {        
        DEBUG("Initiating DFT precoding plan for %d PRBs\n", i);
        if (dft_plan_c(&q->dft_plan[i], i*SRSLTE_NRE, FORWARD)) {
          fprintf(stderr, "Error: Creating DFT plan %d\n",i);
          goto clean_exit;
        }
        dft_plan_set_norm(&q->dft_plan[i], true);
        if (dft_plan_c(&q->idft_plan[i], i*SRSLTE_NRE, BACKWARD)) {
          fprintf(stderr, "Error: Creating DFT plan %d\n",i);
          goto clean_exit;
        }
        dft_plan_set_norm(&q->idft_plan[i], true);
      }
    }
    q->max_prb = max_prb;
    ret = SRSLTE_SUCCESS;
  } 

clean_exit:
  if (ret == SRSLTE_ERROR) {
    srslte_srslte_dft_precoding_free(q);
  }
  return ret; 
}

/* Free DFT plans for transform precoding */
void srslte_srslte_dft_precoding_free(srslte_srslte_dft_precoding_t *q) 
{
  for (uint32_t i=1;i<q->max_prb;i++) {
    if(srslte_srslte_dft_precoding_valid_prb(i)) {      
      DEBUG("Freeing DFT precoding plan for %d PRBs\n", i);
      dft_plan_free(&q->dft_plan[i]);
      dft_plan_free(&q->idft_plan[i]);        
    }
  }  
  bzero(q, sizeof(srslte_srslte_dft_precoding_t));
}

bool srslte_srslte_dft_precoding_valid_prb(uint32_t nof_prb) {
  if (nof_prb == 1 || (nof_prb%2) == 0 || (nof_prb%3) == 0 || (nof_prb%5) == 0) {
    return true; 
  } else {
    return false; 
  }
}

int srslte_dft_precoding(srslte_srslte_dft_precoding_t *q, cf_t *input, cf_t *output, 
                  uint32_t nof_prb, uint32_t nof_symbols) 
{
  
  if (!srslte_srslte_dft_precoding_valid_prb(nof_prb)) {
    fprintf(stderr, "Error invalid number of PRB (%d)\n", nof_prb);
    return SRSLTE_ERROR; 
  }

  for (uint32_t i=0;i<nof_symbols;i++) {
    dft_run_c(&q->dft_plan[nof_prb], &input[i*SRSLTE_NRE*nof_prb], &output[i*SRSLTE_NRE*nof_prb]);
  }
  
  return SRSLTE_SUCCESS;
}

int srslte_dft_predecoding(srslte_srslte_dft_precoding_t *q, cf_t *input, cf_t *output, 
                    uint32_t nof_prb, uint32_t nof_symbols)
{
  if (!srslte_srslte_dft_precoding_valid_prb(nof_prb)) {
    fprintf(stderr, "Error invalid number of PRB (%d)\n", nof_prb);
    return SRSLTE_ERROR; 
  }

  for (uint32_t i=0;i<nof_symbols;i++) {
    dft_run_c(&q->dft_plan[nof_prb], &input[i*SRSLTE_NRE*nof_prb], &output[i*SRSLTE_NRE*nof_prb]);
  }
  
  return SRSLTE_SUCCESS;
  
}