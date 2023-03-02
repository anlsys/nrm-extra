/*******************************************************************************
 * Copyright 2023 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the libnrm project.
 * For more info, see https://github.com/anlsys/libnrm
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef NRM_EXTRA_H
#define NRM_EXTRA_H 1

int nrm_extra_create_name(const char *pattern, char **name);
int nrm_extra_find_allowed_scope(nrm_client_t *client, const char *toolname,
				 nrm_scope_t **scope, int *added);

#endif