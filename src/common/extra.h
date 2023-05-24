/*******************************************************************************
 * Copyright 2021 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef NRM_EXTRA_H
#define NRM_EXTRA_H 1

#include "nrm.h"

int nrm_extra_create_name(const char *pattern, char **name);
int nrm_extra_create_name_ssu(const char *pattern,
                              const char *extra,
                              unsigned int idx,
                              char **name);
int nrm_extra_find_allowed_scope(nrm_client_t *client,
                                 const char *toolname,
                                 nrm_scope_t **scope,
                                 int *added);
int nrm_extra_find_scope(nrm_client_t *client, nrm_scope_t **scope, int *added);

typedef struct nrm_extra_common_opt_s {
	char *upstream_uri;
	int pub_port;
	int rpc_port;
	int log_level;
	double freq;
	int help;
} nrm_extra_common_args_t;


int nrm_extra_parse_common_args(int *argc, char **argv[],
				nrm_extra_common_args_t *args);

#endif
