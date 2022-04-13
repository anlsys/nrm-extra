/*******************************************************************************
 * Copyright 2021 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

/* Filename: nrmpower_papi.c
 *
 * Description: Implements middleware between powercap, measured via PAPI,
 *               and the NRM downstream interface. Resources detected via hwloc.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <hwloc.h>
#include <math.h>
#include <papi.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <nrm.h>

static int log_level = 0;
volatile sig_atomic_t stop;

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

char *usage =
        "usage: nrm-power [options] \n"
        "     options:\n"
        "            -f, --frequency         Frequency in hz to poll. Default: 1.0\n"
        "            -v, --verbose           Produce verbose output. Log messages will be displayed to stderr\n"
        "            -h, --help              Displays this help message\n";

void logging(
        int level, const char *file, unsigned int line, const char *fmt, ...)
{
	if (level <= log_level) {
		fprintf(stderr, "%s:\t%u:\t", file, line);
		va_list ap;
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
}

#define normal(...) logging(0, __FILE__, __LINE__, __VA_ARGS__)
#define verbose(...) logging(1, __FILE__, __LINE__, __VA_ARGS__)
#define error(...) logging(0, __FILE__, __LINE__, __VA_ARGS__)

#define MAX_powercap_EVENTS 128
#define MAX_CPU_scopes 256
#define MAX_NUMA_scopes 8

// handler for interrupt?
void interrupt(int signum)
{
	verbose("Interrupt caught. Exiting loop.\n");
	stop = 1;
}

bool is_energy_event(char *event_name, uint64_t data_type)
{
	return (strstr(event_name, "ENERGY_UJ") &&
	        (data_type == PAPI_DATATYPE_UINT64));
}

bool is_NUMA_event(char *event_name)
{
	return (strstr(event_name, "SUBZONE"));
}

double get_watts(double event_value, double elapsed_time)
{
	return ((double)event_value / 1.0e6) / elapsed_time;
}

int parse_numa_id(char *event_name)
{
	char str_numa_id;
	str_numa_id = event_name[strlen(event_name) - 1];
	return str_numa_id - '0'; // ??? converts numa_id to integer ???
}

int get_cpu_idx(hwloc_topology_t topology, int cpu)
{
	hwloc_obj_t pu;
	pu = hwloc_get_pu_obj_by_os_index(topology, cpu);
	return pu->logical_index;
}

int main(int argc, char **argv)
{
	int i, j, char_opt, err;
	double freq = 1;

	// register callback handler for interrupt
	signal(SIGINT, interrupt);

	ctxt = nrm_ctxt_create();
	assert(ctxt != NULL);
	nrm_init(ctxt, "nrm-power", 0, 0);
	verbose("NRM context initialized.\n");

	// TODO: fix "-v" not being parsed as verbose
	while (1) {
		static struct option long_options[] = {
		        {"verbose", no_argument, &log_level, 1},
		        {"frequency", optional_argument, 0, 'f'},
		        {"help", no_argument, 0, 'h'},
		        {0, 0, 0, 0}};

		int option_index = 0;
		char_opt = getopt_long(argc, argv, "+vf:m:h", long_options,
		                       &option_index);

		if (char_opt == -1)
			break;
		switch (char_opt) {
		case 0:
			break;
		case 'f':
			errno = 0;
			freq = strtod(optarg, NULL);
			if (errno != 0 || freq == 0) {
				error("Error during conversion to double: %s\n",
				      strerror(errno));
				exit(EXIT_FAILURE);
			}
			break;
		case 'h':
			fprintf(stderr, "%s", usage);
			exit(EXIT_SUCCESS);
		case '?':
		default:
			error("Wrong option argument\n");
			error("%s", usage);
			exit(EXIT_FAILURE);
		}
	}

	assert(PAPI_library_init(PAPI_VER_CURRENT) == PAPI_VER_CURRENT);
	verbose("PAPI initialized.\n");

	/* Prepare to detect powercap PAPI component */
	int component_id, powercap_component_id = -1, num_components;
	const PAPI_component_info_t *component_info = NULL;

	/* Detect powercap component by iterating through all components */
	num_components = PAPI_num_components();
	for (component_id = 0; component_id < num_components; component_id++) {
		if ((component_info = PAPI_get_component_info(component_id)) ==
		    NULL) {
			error("PAPI component identification failed: %s\n");
			exit(EXIT_FAILURE);
		}

		if (strstr(component_info->name, "powercap")) {
			powercap_component_id = component_id;
			verbose("PAPI found powercap component at component_id %d\n",
			        powercap_component_id);
			assert(!component_info->disabled);
			break;
		}
	}

	int EventSet = PAPI_NULL;
	assert(component_id != num_components); // Matching component ID not
	                                        // found
	assert(component_info->num_cntrs != 0); // Component has no hardware
	                                        // counters
	assert(PAPI_create_eventset(&EventSet) == PAPI_OK);
	verbose("PAPI EventSet created\n");

	int papi_retval, num_events = 0;
	int EventCode = PAPI_NATIVE_MASK;
	char EventNames[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN]; // For substring
	                                                        // checking in
	                                                        // measurement
	                                                        // loop
	int DataTypes[MAX_powercap_EVENTS]; // For datatype checking in
	                                    // measurement loop
	PAPI_event_info_t EventInfo;

	papi_retval = PAPI_enum_cmp_event(&EventCode, PAPI_ENUM_FIRST,
	                                  powercap_component_id);
	while (papi_retval == PAPI_OK) {

		assert(PAPI_event_code_to_name(
		               EventCode, EventNames[num_events]) == PAPI_OK);
		verbose("code: %d, event: %s\n", EventCode,
		        EventNames[num_events]);
		assert(PAPI_get_event_info(EventCode, &EventInfo) == PAPI_OK);
		DataTypes[num_events] = EventInfo.data_type;
		assert(PAPI_add_event(EventSet, EventCode) == PAPI_OK);

		num_events++;
		papi_retval = PAPI_enum_cmp_event(&EventCode, PAPI_ENUM_EVENTS,
		                                  powercap_component_id);
	}

	hwloc_topology_t topology;
	hwloc_obj_t numanode;
	hwloc_cpuset_t cpus, cpu_sets[MAX_NUMA_scopes];

	nrm_scope_t *nrm_cpu_scopes[MAX_CPU_scopes],
	        *nrm_numa_scopes[MAX_NUMA_scopes];
	int n_numanodes = 0, n_cpus = 0, n_cpuset_counted = 0, n_scopes = 0,
	    cpu_idx, cpu, numa_id;
	char *event;

	assert(hwloc_topology_init(&topology) == 0);
	assert(hwloc_topology_load(topology) == 0);

	n_numanodes = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_NUMANODE);
	n_cpus = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PU);

	for (i = 0; i < n_numanodes; i++) {
		scope = nrm_scope_create();
		nrm_scope_add(scope, NRM_SCOPE_TYPE_NUMA, i);
		nrm_numa_scopes[i] = scope;
		n_scopes++;

		numanode = hwloc_get_obj_by_type(topology, HWLOC_OBJ_NUMANODE,
		                                 i); // HWLOC_OBJ_PACKAGE
		cpus = numanode->cpuset;
		cpu_sets[i] = cpus; // cpusets indexed by NUMANODE index

		hwloc_bitmap_foreach_begin(cpu, cpus)
		        scope = nrm_scope_create();
		cpu_idx = get_cpu_idx(topology, cpu);
		nrm_scope_add(scope, NRM_SCOPE_TYPE_CPU, cpu_idx);
		nrm_cpu_scopes[cpu_idx] = scope;
		n_scopes++;
		n_cpuset_counted++;

		hwloc_bitmap_foreach_end();
	}

	assert(n_cpuset_counted == n_cpus);

	verbose("%d NRM scopes initialized (%d NUMA and %d CPU)\n", n_scopes,
	        n_numanodes, n_cpuset_counted);

	long long before_time, after_time;
	long long *event_values;
	double elapsed_time, watts_value;

	event_values = calloc(num_events, sizeof(long long));

	stop = 0;
	double sleeptime = 1 / freq;

	do {

		before_time = PAPI_get_real_nsec();

		assert(PAPI_start(EventSet) == PAPI_OK);

		/* sleep for a frequency */
		struct timespec req, rem;
		req.tv_sec = floor(sleeptime);
		req.tv_nsec = sleeptime * 1e9 - floor(sleeptime) * 1e9;

		do {
			err = nanosleep(&req, &rem);
			req = rem;
		} while (err == -1 && errno == EINTR);

		after_time = PAPI_get_real_nsec();
		elapsed_time = ((double)(after_time - before_time)) / 1.0e9;

		// Stop and read EventSet measurements into "event_values"...
		assert(PAPI_stop(EventSet, event_values) == PAPI_OK);

		verbose("scaled energy measurements:\n");
		for (i = 0; i < num_events; i++) {
			event = EventNames[i];
			if (is_energy_event(event, DataTypes[i])) {
				watts_value = get_watts(event_values[i],
				                        elapsed_time);
				numa_id = parse_numa_id(event);

				if (is_NUMA_event(event)) {
					nrm_send_progress(
					        ctxt, watts_value,
					        nrm_numa_scopes[numa_id]);

				} else {
					hwloc_bitmap_foreach_begin(
					        cpu, cpu_sets[numa_id])
					        cpu_idx = get_cpu_idx(topology,
					                              cpu);
					nrm_send_progress(
					        ctxt, watts_value,
					        nrm_cpu_scopes[cpu_idx]);
					hwloc_bitmap_foreach_end();
				}
				verbose("%-45s%4.2f J (Average Power %.2fW)\n",
				        EventNames[i], (double)event_values[i],
				        watts_value);
			}
		}
	} while (!stop);
	//
	int papi_status;
	assert(PAPI_state(EventSet, &papi_status) == PAPI_OK);
	if (papi_status == PAPI_RUNNING) {
		assert(PAPI_stop(EventSet, event_values) == PAPI_OK);
	}
	//
	// /* final send here */
	for (i = 0; i < num_events; i++) {
		event = EventNames[i];
		if (is_energy_event(event, DataTypes[i])) {
			watts_value = get_watts(event_values[i], elapsed_time);
			numa_id = parse_numa_id(event);

			if (is_NUMA_event(event)) {
				nrm_send_progress(ctxt, watts_value,
				                  nrm_numa_scopes[numa_id]);

			} else {
				hwloc_bitmap_foreach_begin(cpu,
				                           cpu_sets[numa_id])
				        cpu_idx = get_cpu_idx(topology, cpu);
				nrm_send_progress(ctxt, watts_value,
				                  nrm_cpu_scopes[cpu_idx]);
				hwloc_bitmap_foreach_end();
			}
		}
	}
	verbose("Finalized PAPI-event read/send to NRM.\n");

	nrm_fini(ctxt);
	verbose("Finalized NRM context.\n");

	for (i = 0; i < MAX_CPU_scopes; i++) {
		nrm_scope_delete(nrm_cpu_scopes[i]);
	}

	for (i = 0; i < MAX_NUMA_scopes; i++) {
		nrm_scope_delete(nrm_numa_scopes[i]);
	}

	verbose("NRM scopes deleted.\n");

	nrm_ctxt_delete(ctxt);
	verbose("NRM context deleted. Exiting.\n");

	exit(EXIT_SUCCESS);
}
