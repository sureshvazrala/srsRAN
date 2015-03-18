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

#ifndef _TC_INTERL_H
#define _TC_INTERL_H

#include "srslte/config.h"

typedef struct SRSLTE_API {
  uint32_t *forward;
  uint32_t *reverse;
  uint32_t max_long_cb;
} srs_tc_interl_t;

SRSLTE_API int srs_tc_interl_LTE_gen(srs_tc_interl_t *h, 
                                 uint32_t long_cb);

SRSLTE_API int srs_tc_interl_UMTS_gen(srs_tc_interl_t *h, 
                                  uint32_t long_cb);

SRSLTE_API int srs_tc_interl_init(srs_tc_interl_t *h, 
                              uint32_t max_long_cb);

SRSLTE_API void srs_tc_interl_free(srs_tc_interl_t *h);

#endif
