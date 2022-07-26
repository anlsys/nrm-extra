/*******************************************************************************
 * Copyright 2019 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#include <assert.h>
#include <stdio.h>

#include "nrm_omp.h"

static ompt_start_tool_result_t nrm_ompt_start;
ompt_set_callback_t nrm_ompt_set_callback;


int nrm_ompt_initialize(ompt_function_lookup_t lookup,
                        int initial_device_num,
                        ompt_data_t *tool_data)
{
	ompt_set_result_t ret;

	/* right now nrm ctxt need to know the rank and cpuid of the application
	 * which is probably a mistake...we initialize it to 0 as a ugly fix.
	 */
	nrm_init(NULL, NULL);

	// initialize global client
	nrm_client_create(&global_client, upstream_uri, pub_port, rpc_port);

	// create global scope;
	global_scope = nrm_scope_create();

	// global sensor
	char *name = "ompt_init";
	global_sensor = nrm_sensor_create(name);

	// add global scope and sensor to client, as usual
	nrm_client_add_scope(global_client, global_scope);
	nrm_client_add_sensor(global_client, global_sensor);

	/* use the lookup function to retrieve a function pointer to
	 * ompt_set_callback.
	 */
	nrm_ompt_set_callback = lookup("ompt_set_callback");
	assert(nrm_ompt_set_callback != NULL);

	nrm_ompt_register_cbs();

	/* spec dictates that we return non-zero to keep the tool active */
	return 1;
}

void nrm_ompt_finalize(ompt_data_t *tool_data)
{
	nrm_scope_destroy(&global_scope);
	nrm_client_destroy(&global_client);
	nrm_finalize();
	return;
}

ompt_start_tool_result_t *ompt_start_tool(unsigned int omp_version,
                                          const char *runtime_version)
{
	fprintf(stderr, "OMPT start: %u, %s\n", omp_version, runtime_version);
	return &nrm_ompt_start;
}

static ompt_start_tool_result_t nrm_ompt_start = {
        .initialize = nrm_ompt_initialize,
        .finalize = nrm_ompt_finalize,
        .tool_data = ompt_data_none,
};
