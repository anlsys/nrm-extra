/*******************************************************************************
 * Copyright 2019 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the libnrm project.
 * For more info, see https://xgitlab.cels.anl.gov/argo/libnrm
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

extern nrm_context *ctxt;

void nrm_ompt_register_cbs;

#ifdef __cplusplus
}
#endif

#endif
