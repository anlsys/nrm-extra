#include <assert.h>
#include <omp-tools.h>
#include <stdio.h>

#include "nrm.h"

static ompt_start_tool_result_t nrm_ompt_start;
static ompt_set_callback_t nrm_ompt_set_callback;

static struct nrm_context *ctxt;

/* see OMP Spec 5.0 4.5.2: callbacks function signatures
 */

void nrm_ompt_parallel_begin_cb(ompt_data_t *encountering_task_data,
                                const ompt_frame_t *encountering_task_frame,
                                ompt_data_t *parallel_data,
                                unsigned int requested_parallelism,
                                int flags,
                                const void *codeptr_ra)
{
	nrm_send_progress(ctxt, 1);
}

void nrm_ompt_parallel_end_cb(ompt_data_t *parallel_data,
                              ompt_data_t *encountering_task_data,
                              int flags,
                              const void *codeptr_ra)
{
	nrm_send_progress(ctxt, 1);
}

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

	/* set up callbacks:
	 * right now, the only type of progress we want from OMPT is the
	 * entry/exit of parallel sections. We don't try to retrieve progress
	 * per thread.
	 * We'll send one progress at the beginning of a section and one at the
	 * end.
	 * All OpenMP runtimes are supposed to support them.
	 */
	ret = nrm_ompt_set_callback(
	        ompt_callback_parallel_begin,
	        (ompt_callback_t)nrm_ompt_parallel_begin_cb);
	assert(ret == ompt_set_always);

	ret = nrm_ompt_set_callback(ompt_callback_parallel_end,
	                            (ompt_callback_t)nrm_ompt_parallel_end_cb);
	assert(ret == ompt_set_always);
	/* spec dictates that we return non-zero to keep the tool active */
	return 1;
}

void nrm_ompt_finalize(ompt_data_t *tool_data)
{
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
