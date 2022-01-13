/*******************************************************************************
 * Copyright 2019 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef NRM_OMP_H
#define NRM_OMP_H 1

#include <omp-tools.h>
#include <nrm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct nrm_context *ctxt;
extern nrm_scope_t *global_scope;
extern ompt_set_callback_t nrm_ompt_set_callback;

void nrm_ompt_register_cbs(void);

#ifdef __cplusplus
}
#endif

#endif
