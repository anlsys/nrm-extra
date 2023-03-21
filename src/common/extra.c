/*******************************************************************************
 * Copyright 2021 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nrm.h>

#include "extra.h"

int nrm_extra_create_name(const char *pattern, char **name)
{
	char *buf;
	int pid;
	size_t bufsize;
	pid = getpid();
	bufsize = snprintf(NULL, 0, "%s.%u", pattern, pid);
	bufsize++;
	buf = calloc(1, bufsize);
	if (!buf)
		return -NRM_ENOMEM;
	snprintf(buf, bufsize, "%s.%u", pattern, pid);
	*name = buf;
	return 0;
}

int nrm_extra_create_name_ssu(const char *pattern,
                              const char *extra,
                              unsigned int idx,
                              char **name)
{
	char *buf;
	size_t bufsize;
	bufsize = snprintf(NULL, 0, "%s.%s.%u", pattern, extra, idx);
	bufsize++;
	buf = calloc(1, bufsize);
	if (!buf)
		return -NRM_ENOMEM;
	snprintf(buf, bufsize, "%s.%s.%u", pattern, extra, idx);
	*name = buf;
	return 0;
}

int nrm_extra_find_allowed_scope(nrm_client_t *client,
                                 const char *toolname,
                                 nrm_scope_t **scope,
                                 int *added)
{
	char *buf;
	int err;
	err = nrm_extra_create_name(toolname, &buf);
	if (err)
		return err;
	/* figure out our scope:
	 * - TODO: add some info about slices across the code base
	 * - figure out our current cpuset/memset, and find the corresponding
	 *   scope
	 */
	int newscope = 0;
	nrm_scope_t *allowed = nrm_scope_create_hwloc_allowed(buf);
	free(buf);
	nrm_vector_t *nrmd_scopes;
	nrm_client_list_scopes(client, &nrmd_scopes);
	size_t numscopes;
	nrm_vector_length(nrmd_scopes, &numscopes);
	for (size_t i = 0; i < numscopes; i++) {
		nrm_scope_t *s;
		nrm_vector_pop_back(nrmd_scopes, &s);
		if (!nrm_scope_cmp(s, allowed)) {
			nrm_scope_destroy(allowed);
			allowed = s;
			newscope = 1;
			continue;
		}
		nrm_scope_destroy(s);
	}
	if (!newscope) {
		nrm_log_debug(
		        "allowed scope not found in nrmd, adding a new one\n");
		assert(nrm_client_add_scope(client, allowed) == 0);
	}
	nrm_vector_destroy(&nrmd_scopes);
	*added = !newscope;
	*scope = allowed;
	return 0;
}

int nrm_extra_find_scope(nrm_client_t *client, nrm_scope_t **scope, int *added)
{
	nrm_vector_t *nrmd_scopes;
	int newscope = 0;
	size_t numscopes;
	nrm_client_list_scopes(client, &nrmd_scopes);
	nrm_vector_length(nrmd_scopes, &numscopes);
	for (size_t i = 0; i < numscopes; i++) {
		nrm_scope_t *s;
		nrm_vector_pop_back(nrmd_scopes, &s);
		if (!nrm_scope_cmp(s, *scope)) {
			nrm_scope_destroy(*scope);
			*scope = s;
			newscope = 1;
			continue;
		}
		nrm_scope_destroy(s);
	}
	if (!newscope) {
		nrm_log_debug(
		        "allowed scope not found in nrmd, adding a new one\n");
		assert(nrm_client_add_scope(client, *scope) == 0);
	}
	nrm_vector_destroy(&nrmd_scopes);
	*added = !newscope;
	return 0;
}

