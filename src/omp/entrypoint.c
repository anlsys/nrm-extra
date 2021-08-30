#include <assert.h>
#include <stdio.h>

#include "nrm_omp.h"

static ompt_start_tool_result_t nrm_ompt_start;
ompt_set_callback_t nrm_ompt_set_callback;

struct nrm_context *ctxt;

int nrm_ompt_initialize(ompt_function_lookup_t lookup,
                        int initial_device_num,
                        ompt_data_t *tool_data)
{
	ompt_set_result_t ret;

	/* initialize the NRM context */
	ctxt = nrm_ctxt_create();
	assert(ctxt != NULL);

	/* right now nrm ctxt need to know the rank and cpuid of the application
	 * which is probably a mistake...we initialize it to 0 as a ugly fix.
	 */
	nrm_init(ctxt, "nrm-ompt", 0, 0);

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
	nrm_fini(ctxt);
	nrm_ctxt_delete(ctxt);
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
