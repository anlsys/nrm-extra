/* generated. See extra/omp */
#include <assert.h>

#include "nrm_omp.h"

void nrm_ompt_callback_thread_begin_cb(ompt_thread_t thread_type,
                                       ompt_data_t *thread_data)
{
	nrm_scope_t *scope = nrm_scope_create();
	thread_data->ptr = (void *)scope;
	nrm_scope_threadprivate(scope);
	nrm_scope_threadshared(global_scope);
	nrm_send_progress(ctxt, 1, scope);
}

void nrm_ompt_callback_thread_end_cb(ompt_data_t *thread_data)
{
	nrm_scope_t *scope = (nrm_scope_t *)thread_data->ptr;
	nrm_scope_threadprivate(scope);
	nrm_send_progress(ctxt, 1, scope);
	nrm_scope_delete(scope);
	thread_data->ptr = NULL;
}

void nrm_ompt_callback_parallel_begin_cb(
        ompt_data_t *encountering_task_data,
        const ompt_frame_t *encountering_task_frame,
        ompt_data_t *parallel_data,
        unsigned int requested_parallelism,
        int flags,
        const void *codeptr_ra)
{
	nrm_send_progress(ctxt, 1, global_scope);
}

void nrm_ompt_callback_parallel_end_cb(ompt_data_t *parallel_data,
                                       ompt_data_t *encountering_task_data,
                                       int flags,
                                       const void *codeptr_ra)
{
	nrm_send_progress(ctxt, 1, global_scope);
}

void nrm_ompt_callback_work_cb(ompt_work_t wstype,
                               ompt_scope_endpoint_t endpoint,
                               ompt_data_t *parallel_data,
                               ompt_data_t *task_data,
                               uint64_t count,
                               const void *codeptr_ra)
{
}

void nrm_ompt_callback_dispatch_cb(ompt_data_t *parallel_data,
                                   ompt_data_t *task_data,
                                   ompt_dispatch_t kind,
                                   ompt_data_t instance)
{
}

void nrm_ompt_callback_task_create_cb(
        ompt_data_t *encountering_task_data,
        const ompt_frame_t *encountering_task_frame,
        ompt_data_t *new_task_data,
        int flags,
        int has_dependences,
        const void *codeptr_ra)
{
}

void nrm_ompt_callback_dependences_cb(ompt_data_t *task_data,
                                      const ompt_dependence_t *deps,
                                      int ndeps)
{
}

void nrm_ompt_callback_task_dependence_cb(ompt_data_t *src_task_data,
                                          ompt_data_t *sink_task_data)
{
}

void nrm_ompt_callback_task_schedule_cb(ompt_data_t *prior_task_data,
                                        ompt_task_status_t prior_task_status,
                                        ompt_data_t *next_task_data)
{
}

void nrm_ompt_callback_implicit_task_cb(ompt_scope_endpoint_t endpoint,
                                        ompt_data_t *parallel_data,
                                        ompt_data_t *task_data,
                                        unsigned int actual_parallelism,
                                        unsigned int index,
                                        int flags)
{
}

void nrm_ompt_callback_sync_region_cb(ompt_sync_region_t kind,
                                      ompt_scope_endpoint_t endpoint,
                                      ompt_data_t *parallel_data,
                                      ompt_data_t *task_data,
                                      const void *codeptr_ra)
{
}

void nrm_ompt_callback_mutex_acquire_cb(ompt_mutex_t kind,
                                        unsigned int hint,
                                        unsigned int impl,
                                        ompt_wait_id_t wait_id,
                                        const void *codeptr_ra)
{
}

void nrm_ompt_callback_nest_lock_cb(ompt_scope_endpoint_t endpoint,
                                    ompt_wait_id_t wait_id,
                                    const void *codeptr_ra)
{
}

void nrm_ompt_callback_flush_cb(ompt_data_t *thread_data,
                                const void *codeptr_ra)
{
}

void nrm_ompt_callback_cancel_cb(ompt_data_t *task_data,
                                 int flags,
                                 const void *codeptr_ra)
{
}

void nrm_ompt_callback_device_initialize_cb(int device_num,
                                            const char *type,
                                            ompt_device_t *device,
                                            ompt_function_lookup_t lookup,
                                            const char *documentation)
{
}

void nrm_ompt_callback_device_finalize_cb(int device_num)
{
}

void nrm_ompt_callback_device_load_cb(int device_num,
                                      const char *filename,
                                      int64_t offset_in_file,
                                      void *vma_in_file,
                                      size_t bytes,
                                      void *host_addr,
                                      void *device_addr,
                                      uint64_t module_id)
{
}

void nrm_ompt_callback_device_unload_cb(int device_num, uint64_t module_id)
{
}

void nrm_ompt_callback_target_data_op_cb(ompt_id_t target_id,
                                         ompt_id_t host_op_id,
                                         ompt_target_data_op_t optype,
                                         void *src_addr,
                                         int src_device_num,
                                         void *dest_addr,
                                         int dest_device_num,
                                         size_t bytes,
                                         const void *codeptr_ra)
{
}

void nrm_ompt_callback_target_cb(ompt_target_t kind,
                                 ompt_scope_endpoint_t endpoint,
                                 int device_num,
                                 ompt_data_t *task_data,
                                 ompt_id_t target_id,
                                 const void *codeptr_ra)
{
}

void nrm_ompt_callback_target_map_cb(ompt_id_t target_id,
                                     unsigned int nitems,
                                     void **host_addr,
                                     void **device_addr,
                                     size_t *bytes,
                                     unsigned int *mapping_flags,
                                     const void *codeptr_ra)
{
}

void nrm_ompt_callback_target_submit_cb(ompt_id_t target_id,
                                        ompt_id_t host_op_id,
                                        unsigned int requested_num_teams)
{
}

int nrm_ompt_callback_control_tool_cb(uint64_t command,
                                      uint64_t modifier,
                                      void *arg,
                                      const void *codeptr_ra)
{
}

void nrm_ompt_register_cbs(void)
{
	ompt_set_result_t ret;

	ret = nrm_ompt_set_callback(
	        ompt_callback_thread_begin,
	        (ompt_callback_t)nrm_ompt_callback_thread_begin_cb);
	assert(ret == ompt_set_always);

	ret = nrm_ompt_set_callback(
	        ompt_callback_thread_end,
	        (ompt_callback_t)nrm_ompt_callback_thread_end_cb);
	assert(ret == ompt_set_always);

	ret = nrm_ompt_set_callback(
	        ompt_callback_parallel_begin,
	        (ompt_callback_t)nrm_ompt_callback_parallel_begin_cb);
	assert(ret == ompt_set_always);

	ret = nrm_ompt_set_callback(
	        ompt_callback_parallel_end,
	        (ompt_callback_t)nrm_ompt_callback_parallel_end_cb);
	assert(ret == ompt_set_always);

	ret = nrm_ompt_set_callback(ompt_callback_work,
	                            (ompt_callback_t)nrm_ompt_callback_work_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_dispatch,
	        (ompt_callback_t)nrm_ompt_callback_dispatch_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_task_create,
	        (ompt_callback_t)nrm_ompt_callback_task_create_cb);
	assert(ret == ompt_set_always);

	ret = nrm_ompt_set_callback(
	        ompt_callback_dependences,
	        (ompt_callback_t)nrm_ompt_callback_dependences_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_task_dependence,
	        (ompt_callback_t)nrm_ompt_callback_task_dependence_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_task_schedule,
	        (ompt_callback_t)nrm_ompt_callback_task_schedule_cb);
	assert(ret == ompt_set_always);

	ret = nrm_ompt_set_callback(
	        ompt_callback_implicit_task,
	        (ompt_callback_t)nrm_ompt_callback_implicit_task_cb);
	assert(ret == ompt_set_always);

	ret = nrm_ompt_set_callback(
	        ompt_callback_sync_region,
	        (ompt_callback_t)nrm_ompt_callback_sync_region_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_mutex_acquire,
	        (ompt_callback_t)nrm_ompt_callback_mutex_acquire_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_nest_lock,
	        (ompt_callback_t)nrm_ompt_callback_nest_lock_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_flush,
	        (ompt_callback_t)nrm_ompt_callback_flush_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_cancel,
	        (ompt_callback_t)nrm_ompt_callback_cancel_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_device_initialize,
	        (ompt_callback_t)nrm_ompt_callback_device_initialize_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_device_finalize,
	        (ompt_callback_t)nrm_ompt_callback_device_finalize_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_device_load,
	        (ompt_callback_t)nrm_ompt_callback_device_load_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_device_unload,
	        (ompt_callback_t)nrm_ompt_callback_device_unload_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_target_data_op,
	        (ompt_callback_t)nrm_ompt_callback_target_data_op_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_target,
	        (ompt_callback_t)nrm_ompt_callback_target_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_target_map,
	        (ompt_callback_t)nrm_ompt_callback_target_map_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_target_submit,
	        (ompt_callback_t)nrm_ompt_callback_target_submit_cb);

	ret = nrm_ompt_set_callback(
	        ompt_callback_control_tool,
	        (ompt_callback_t)nrm_ompt_callback_control_tool_cb);
	assert(ret == ompt_set_always);
}
