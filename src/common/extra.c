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


int nrm_extra_find_allowed_scope(nrm_client_t *client, const char *toolname,
				 nrm_scope_t **scope, int *added)
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
		nrm_log_debug("allowed scope not found in nrmd, adding a new one\n");
		assert(nrm_client_add_scope(client, allowed) == 0);
	}
	*added = !newscope;
	*scope = allowed;
	return 0;
}
