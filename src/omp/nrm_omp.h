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

extern nrm_client_t *global_client;
extern nrm_scope_t *global_scope;
extern nrm_sensor_t *global_sensor;

extern char *upstream_uri;
extern int pub_port;
extern int rpc_port;

extern ompt_set_callback_t nrm_ompt_set_callback;

extern nrm_time_t nrmtime;

upstream_uri = "tcp://127.0.0.1";
pub_port = 2345;
rpc_port = 3456;

nrm_client_create(&global_client, upstream_uri, pub_port, rpc_port);

global_scope = nrm_scope_create();

char *name = "nrm-omp";
global_sensor = nrm_sensor_create(name);

void nrm_ompt_register_cbs(void);

#ifdef __cplusplus
}
#endif

#endif
