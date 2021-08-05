#include <omp-tools.h>

int nrm_ompt_initialize(ompt_lookup_t *lookup,
			int initial_device_num,
			ompt_data_t *tool_data)
{
	/* spec dictates that we return non-zero to keep the tool active */
	return 1;
}

void nrm_ompt_finalize(ompt_data_t *tool_data)
{
	return;
}

ompt_start_result_t *ompt_start_tool(unsigned int omp_version,
				     const char *runtime_version)
{
	fprintf(stderr, "OMPT start: %u, %s\n", omp_version, runtime_version);
	return &nrm_ompt_start;
}

static ompt_start_result_t nrm_ompt_start = {
	.initialize = nrm_ompt_initialize;
	.finalize = nrm_ompt_finalize;
	.data = ompt_data_none;
};
