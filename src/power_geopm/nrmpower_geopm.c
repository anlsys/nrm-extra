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

#include <stddef.h>
#include <sys/syslimits.h>
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <geopm_pio.h>
#include <geopm_topo.h>
#include <getopt.h>
#include <hwloc.h>
#include <limits.h>
#include <math.h>
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

#include "extra.h"

static int log_level = NRM_LOG_DEBUG;
volatile sig_atomic_t stop;

static nrm_client_t *client;
static nrm_scope_t *scope;
static nrm_sensor_t *sensor;
static int custom_scope = 0;

static char *upstream_uri = "tcp://127.0.0.1";
static int pub_port = 2345;
static int rpc_port = 3456;

char *usage =
        "usage: nrm-power [options] \n"
        "     options:\n"
        "            -s	 --signals           Single GEOPM Signal name. Default: both CPU_POWER and DRAM_POWER\n"
        "                                       Use `sudo geopmread` to determine valid signal names         \n"
        "            -v, --verbose           Produce verbose output. Log messages will be displayed to stderr\n"
        "            -h, --help              Displays this help message\n";

#define MAX_powercap_EVENTS 128
#define MAX_MEASUREMENTS 8

// handler for interrupt?
void interrupt(int signum)
{
	nrm_log_debug("Interrupt caught. Exiting loop.\n");
	stop = 1;
}

double get_watts(double event_value, int64_t elapsed_time)
{
	return ((double)event_value / 1.0e6) / (elapsed_time / 1.0e9);
}

int get_cpu_idx(hwloc_topology_t topology, int cpu)
{
	hwloc_obj_t pu;
	pu = hwloc_get_pu_obj_by_os_index(topology, cpu);
	return pu->logical_index;
}

char *get_domain_label(char *string)
{
	char *label;
	do {
		label = strtok(string, "_");
	} while (label != NULL);
	return label; // should return last token after the last underscore
}

int main(int argc, char **argv)
{
	int i, j, char_opt, err;
	double freq = 1;
	size_t MAX_SIGNAL_NAME_LENGTH = 55;
	nrm_vector_t *signals;

	assert(nrm_vector_create(&signals, MAX_SIGNAL_NAME_LENGTH) ==
	       NRM_SUCCESS);

	nrm_init(NULL, NULL);
	assert(nrm_log_init(stderr, "nrm.extra.geopm") == 0);

	nrm_log_setlevel(log_level);
	nrm_log_debug("NRM logging initialized.\n");

	// register callback handler for interrupt
	signal(SIGINT, interrupt);

	// TODO: fix "-v" not being parsed as verbose
	while (1) {
		static struct option long_options[] = {
		        {"signals", required_argument, 0, 's'},
		        {"verbose", no_argument, &log_level, 1},
		        {"help", no_argument, 0, 'h'},
		        {"frequency", required_argument, 0, 'f'},
		        {0, 0, 0, 0}};

		int option_index = 0;
		char_opt = getopt_long(argc, argv, "s:vhf:", long_options,
		                       &option_index);

		if (char_opt == -1)
			break;
		switch (char_opt) {
		case 0:
			break;
		case 's':
			nrm_log_debug("Parsed signal %s\n", optarg);
			nrm_vector_push_back(signals, &optarg);
			break;
		case 'v':
			log_level = NRM_LOG_DEBUG;
			break;
		case 'f':
			freq = strtod(optarg, NULL);
			break;
		case 'h':
			fprintf(stderr, "%s", usage);
			exit(EXIT_SUCCESS);
		case '?':
		default:
			fprintf(stderr, "Wrong option argument\n");
			fprintf(stderr, "%s", usage);
			exit(EXIT_FAILURE);
		}
	}

	nrm_client_create(&client, upstream_uri, pub_port, rpc_port);
	nrm_log_debug("NRM client initialized.\n");
	assert(client != NULL);

	// create sensor
	const char *name = "nrm.sensor.power-geopm";
	sensor = nrm_sensor_create(name);

	// client add sensor
	assert(nrm_client_add_sensor(client, sensor) == 0);

	int nsignals = geopm_pio_num_signal_name();
	assert(nsignals > 0); // just check that we can obtain signals. Already
	                      // using strs

	// we've stored all signals in vector, we need to store corresponding
	// domain_type labels and
	//	component idxs for those domains

	size_t n_signals;
	assert(nrm_vector_length(signals, &n_signals) == NRM_SUCCESS);

	int SignalDomainTypes[n_signals],
	        SignalDomainIndexes[n_signals][128]; // e.g. store ~128 logical
	                                             // CPU indexes for a
	                                             // GEOPM_DOMAIN_CPU?

	int i, j, domain_type;
	char *signal_name, *full_domain_name;
	for (i = 0; i < n_signals; i++) {
		void *p;
		nrm_vector_get(signals, i, &p);
		signal_name = *(char *)p;
		domain_type = geopm_pio_signal_domain_type(signal_name);
		assert(domain_type >= 0); // GEOPM_DOMAIN_INVALID = -1
		SignalDomainTypes[i] = domain_type;
		err = geopm_topo_domain_name(domain_type, NAME_MAX,
		                             full_domain_name);
		char *domain_token = get_domain_label(full_domain_name);
		nrm_log_debug("We get signal: %s. Main screen turn on.\n",
		              full_domain_name);
	}

	// assert(PAPI_library_init(PAPI_VER_CURRENT) == PAPI_VER_CURRENT);
	// nrm_log_debug("PAPI initialized.\n");

	/* Prepare to detect powercap PAPI component */
	// int component_id, powercap_component_id = -1, num_components;
	// const PAPI_component_info_t *component_info = NULL;

	/* Detect powercap component by iterating through all components */
	// num_components = PAPI_num_components();
	// for (component_id = 0; component_id < num_components; component_id++)
	// { 	if ((component_info = PAPI_get_component_info(component_id)) ==
	// 	    NULL) {
	// 		nrm_log_error(
	// 		        "PAPI component identification failed: %s\n");
	// 		exit(EXIT_FAILURE);
	// 	}

	// 	if (strstr(component_info->name, "powercap")) {
	// 		powercap_component_id = component_id;
	// 		nrm_log_debug(
	// 		        "PAPI found powercap component at component_id
	// %d\n", 		        powercap_component_id);
	// assert(!component_info->disabled); 		break;
	// 	}
	// }

	int EventSet = PAPI_NULL;
	assert(component_id != num_components); // Matching component ID not
	                                        // found
	assert(component_info->num_cntrs != 0); // Component has no hardware
	                                        // counters
	assert(PAPI_create_eventset(&EventSet) == PAPI_OK);
	nrm_log_debug("PAPI EventSet created\n");

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
		nrm_log_debug("code: %d, event: %s\n", EventCode,
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
	hwloc_cpuset_t cpus;

	nrm_scope_t *nrm_cpu_scopes[MAX_MEASUREMENTS],
	        *nrm_numa_scopes[MAX_MEASUREMENTS],
	        *custom_scopes[MAX_MEASUREMENTS];

	int n_energy_events = 0, n_scopes = 0, n_numa_scopes = 0,
	    n_cpu_scopes = 0, n_custom_scopes = 0, cpu_idx, cpu, numa_id;
	char *event;
	char *scope_name;

	assert(hwloc_topology_init(&topology) == 0);
	assert(hwloc_topology_load(topology) == 0);

	int added;

	// INSTEAD: create a scope for each measure-able event, with
	// corresponding indexes
	for (i = 0; i < num_events; i++) {
		event = EventNames[i];

		// need to create custom scope name first out of available
		// information, then scope
		if (is_energy_event(event, DataTypes[i])) {
			n_energy_events++;
			numa_id = parse_numa_id(event); // should match
			                                // NUMANODE's logical ID
			nrm_log_debug("energy event detected.\n",
			              n_energy_events);

			if (is_NUMA_event(event)) {
				err = nrm_extra_create_name_ssu("nrm.geopm",
				                                "numa", numa_id,
				                                &scope_name);
				nrm_log_debug("Creating new scope: %s\n",
				              scope_name);

				scope = nrm_scope_create(scope_name);
				nrm_scope_add(scope, NRM_SCOPE_TYPE_NUMA,
				              numa_id);
				nrm_extra_find_scope(client, &scope, &added);
				free(scope_name);
				nrm_numa_scopes[numa_id] = scope;
				n_numa_scopes++;
			} else { // need NUMANODE object to parse CPU
				 // indexes
				err = nrm_extra_create_name_ssu("nrm.geopm",
				                                "cpu", numa_id,
				                                &scope_name);
				nrm_log_debug("Creating new scope: %s\n",
				              scope_name);

				scope = nrm_scope_create(scope_name);
				numanode = hwloc_get_obj_by_type(
				        topology, HWLOC_OBJ_NUMANODE, numa_id);
				cpus = numanode->cpuset;
				hwloc_bitmap_foreach_begin(cpu, cpus)
				        cpu_idx = get_cpu_idx(topology, cpu);
				nrm_scope_add(scope, NRM_SCOPE_TYPE_CPU,
				              cpu_idx);
				hwloc_bitmap_foreach_end();
				nrm_extra_find_scope(client, &scope, &added);
				free(scope_name);
				nrm_cpu_scopes[numa_id] = scope;
				n_cpu_scopes++;
			}
			if (added) {
				custom_scopes[numa_id] = scope;
				n_custom_scopes++;
			}
			n_scopes++;
		}
	}

	nrm_log_debug("%d candidate energy events detected.\n",
	              n_energy_events);
	nrm_log_debug(
	        "%d NRM scopes initialized (%d NUMA, %d CPU, %d custom)\n",
	        n_scopes, n_numa_scopes, n_cpu_scopes, n_custom_scopes);

	long long *event_values;
	nrm_time_t before_time, after_time;
	int64_t elapsed_time;
	double watts_value, *event_totals;

	event_values = calloc(num_events, sizeof(long long));
	event_totals = calloc(num_events, sizeof(double)); // converting then
	                                                   // storing

	stop = 0;
	double sleeptime = 1 / freq;

	while (true) {

		nrm_time_gettime(&before_time);

		assert(PAPI_start(EventSet) == PAPI_OK);

		/* sleep for a frequency */
		struct timespec req, rem;
		req.tv_sec = ceil(sleeptime);
		req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;

		err = nanosleep(&req, &rem);
		if (err == -1 && errno == EINTR) {
			nrm_log_error("interupted during sleep, exiting\n");
			break;
		}

		nrm_time_gettime(&after_time);
		elapsed_time = nrm_time_diff(&before_time, &after_time);

		// Stop and read EventSet measurements into "event_values"...
		assert(PAPI_stop(EventSet, event_values) == PAPI_OK);

		nrm_log_debug("scaled energy measurements:\n");
		for (i = 0; i < num_events; i++) {
			event = EventNames[i];

			if (is_energy_event(event, DataTypes[i])) {
				watts_value = get_watts(event_values[i],
				                        elapsed_time);
				numa_id = parse_numa_id(event); // should match
				                                // NUMANODE's
				                                // logical ID

				event_totals[i] += watts_value;

				if (is_NUMA_event(event)) {
					scope = nrm_numa_scopes[numa_id];
				} else {
					scope = nrm_cpu_scopes[numa_id];
				}
				nrm_log_debug(
				        "%-45s%4i uj (Total Power %f W)\n",
				        EventNames[i], event_values[i],
				        event_totals[i]);

				err = nrm_client_send_event(client, after_time,
				                            sensor, scope,
				                            event_totals[i]);
			}
		}
		if (err == -1 || errno == EINTR) {
			nrm_log_error("Interrupted. Exiting\n");
			break;
		}
	}

	int papi_status;
	assert(PAPI_state(EventSet, &papi_status) == PAPI_OK);
	if (papi_status == PAPI_RUNNING) {
		assert(PAPI_stop(EventSet, event_values) == PAPI_OK);
	}

	/* final send here */
	for (i = 0; i < num_events; i++) {
		event = EventNames[i];

		if (is_energy_event(event, DataTypes[i])) {
			watts_value = get_watts(event_values[i], elapsed_time);
			numa_id = parse_numa_id(event); // should match
			                                // NUMANODE's logical ID

			event_totals[i] += watts_value;

			if (is_NUMA_event(event)) {
				scope = nrm_numa_scopes[numa_id];
			} else {
				scope = nrm_cpu_scopes[numa_id];
			}
			nrm_client_send_event(client, after_time, sensor, scope,
			                      event_totals[i]);
		}
	}

	for (i = 0; i < n_custom_scopes; i++) {
		nrm_client_remove_scope(client, custom_scopes[i]);
	}
	for (i = 0; i < n_cpu_scopes; i++) {
		nrm_scope_destroy(nrm_cpu_scopes[i]);
	}

	for (i = 0; i < n_numa_scopes; i++) {
		nrm_scope_destroy(nrm_numa_scopes[i]);
	}
	nrm_log_debug("NRM scopes deleted.\n");

	nrm_sensor_destroy(&sensor);
	nrm_client_destroy(&client);

	nrm_finalize();
	free(event_values);
	free(event_totals);

	exit(EXIT_SUCCESS);
}
