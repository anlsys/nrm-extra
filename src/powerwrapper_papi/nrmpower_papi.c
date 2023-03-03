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

bool is_energy_event(char *event_name, uint64_t data_type)
{
	return (strstr(event_name, "ENERGY_UJ") &&
	        (data_type == PAPI_DATATYPE_UINT64));
}

bool is_NUMA_event(char *event_name)
{
	return (strstr(event_name, "SUBZONE"));
}

double get_watts(double event_value, int64_t elapsed_time)
{
	return ((double)event_value / 1.0e6) / (elapsed_time / 1.0e9);
}

int parse_numa_id(char *event_name)
{
	// Extract zone number from each of the following:
	// powercap:::ENERGY_UJ:ZONE0
	// powercap:::ENERGY_UJ:ZONE0_SUBZONE0
	char str_numa_id;
	str_numa_id = event_name[25];
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

	// TODO: fix "-v" not being parsed as verbose
	while (1) {
		static struct option long_options[] = {
		        {"verbose", no_argument, &log_level, 1},
		        {"help", no_argument, 0, 'h'},
		        {0, 0, 0, 0}};

		int option_index = 0;
		char_opt = getopt_long(argc, argv, "vh", long_options,
		                       &option_index);

		if (char_opt == -1)
			break;
		switch (char_opt) {
		case 0:
			break;
		case 'v':
			log_level = NRM_LOG_DEBUG;
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

	nrm_init(NULL, NULL);
	assert(nrm_log_init(stderr, "nrm.extra.papi") == 0);

	nrm_log_setlevel(log_level);
	nrm_log_debug("NRM logging initialized.\n");

	nrm_client_create(&client, upstream_uri, pub_port, rpc_port);
	nrm_log_debug("NRM client initialized.\n");
	assert(client != NULL);

	// create sensor
	const char *name = "nrm.sensor.power-papi";
	sensor = nrm_sensor_create(name);

	// client add sensor
	assert(nrm_client_add_sensor(client, sensor) == 0);

	assert(PAPI_library_init(PAPI_VER_CURRENT) == PAPI_VER_CURRENT);
	nrm_log_debug("PAPI initialized.\n");

	/* Prepare to detect powercap PAPI component */
	int component_id, powercap_component_id = -1, num_components;
	const PAPI_component_info_t *component_info = NULL;

	/* Detect powercap component by iterating through all components */
	num_components = PAPI_num_components();
	for (component_id = 0; component_id < num_components; component_id++) {
		if ((component_info = PAPI_get_component_info(component_id)) ==
		    NULL) {
			nrm_log_error(
			        "PAPI component identification failed: %s\n");
			exit(EXIT_FAILURE);
		}

		if (strstr(component_info->name, "powercap")) {
			powercap_component_id = component_id;
			nrm_log_debug(
			        "PAPI found powercap component at component_id %d\n",
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
	        *nrm_numa_scopes[MAX_MEASUREMENTS];
	static int custom_scopes[MAX_MEASUREMENTS];

	int n_energy_events = 0, n_scopes = 0, n_numa_scopes = 0,
	    n_cpu_scopes = 0, cpu_idx, cpu, numa_id;
	char *event;
	char *scope_name, scope_type[5];
	const char scope_format[] = "nrm.papi.%s.%u"; //*pattern =
	                                              //"nrm.papi.%s.%d"; //
	                                              // e.g. nrm.papi.numa.1 or
	                                              // nrm.papi.cpu.2
	size_t bufsize = 16;

	assert(hwloc_topology_init(&topology) == 0);
	assert(hwloc_topology_load(topology) == 0);

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

			scope_name = calloc(1, bufsize);
			if (is_NUMA_event(event)) {
				scope_type = "NUMA";
				n_numa_scopes++;
				snprintf(scope_name, bufsize, scope_format,
				         scope_type, n_numa_scopes);
				nrm_log_debug("NUMA energy event detected.\n",
				              n_energy_events);
			} else { // need NUMANODE object to parse CPU indexes
				scope_type = "CPU";
				n_cpu_scopes++;
				snprintf(scope_name, bufsize, scope_format,
				         scope_type, n_cpu_scopes);
				nrm_log_debug("CPU energy event detected.\n",
				              n_energy_events);
			}
			nrm_vector_t *search_results;
			err = nrm_client_find(client, NRM_MSG_TARGET_TYPE_SCOPE,
			                      scope_name, &search_results);

			if (err) {
				nrm_log_error("error during client request\n");
				exit(EXIT_FAILURE);
			}

			nrm_vector_foreach(search_results, iterator)
			{
				nrm_scope_t *result =
				        nrm_vector_iterator_get(iterator);
			}
			if (result != NULL) {
				nrm_log_debug(
				        "Scope %s retrieved from daemon.\n",
				        scope_name);
				scope = result;
				if (is_NUMA_event(event)) { // we only tried
					                    // retrieving a numa
					                    // scope on
					                    // detecting a numa
					                    // event
					nrm_numa_scopes[numa_id] = scope;
				} else {
					nrm_cpu_scopes[numa_id] = scope;
				}
			} else {
				nrm_log_debug("Creating new scope: %s\n",
				              scope_name);

				scope = nrm_scope_create(scope_name);

				if (is_NUMA_event(event)) {
					nrm_scope_add(scope,
					              NRM_SCOPE_TYPE_NUMA,
					              numa_id);
					nrm_numa_scopes[numa_id] = scope;

				} else { // need NUMANODE object to parse CPU
					 // indexes
					numanode = hwloc_get_obj_by_type(
					        topology, HWLOC_OBJ_NUMANODE,
					        numa_id);
					cpus = numanode->cpuset;
					hwloc_bitmap_foreach_begin(cpu, cpus)
					        cpu_idx = get_cpu_idx(topology,
					                              cpu);
					nrm_scope_add(scope, NRM_SCOPE_TYPE_CPU,
					              cpu_idx);
					hwloc_bitmap_foreach_end();
					nrm_cpu_scopes[numa_id] = scope;
				}
				nrm_client_add_scope(client, scope);
			}
			n_scopes++;
		}
	}

	nrm_log_debug("%d candidate energy events detected.\n",
	              n_energy_events);
	nrm_log_debug("%d NRM scopes initialized (%d NUMA and %d CPU)\n",
	              n_scopes, n_numa_scopes, n_cpu_scopes);

	long long *event_values;
	nrm_time_t before_time, after_time;
	int64_t elapsed_time;
	double watts_value, *event_totals;

	event_values = calloc(num_events, sizeof(long long));
	event_totals = calloc(num_events, sizeof(double)); // converting then
	                                                   // storing

	stop = 0;
	double sleeptime = 1 / freq;

	do {

		nrm_time_gettime(&before_time);

		assert(PAPI_start(EventSet) == PAPI_OK);

		/* sleep for a frequency */
		struct timespec req, rem;
		req.tv_sec = ceil(sleeptime);
		req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;

		do {
			err = nanosleep(&req, &rem);
			req = rem;
		} while (err == -1 && errno == EINTR);

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
					nrm_log_debug(
					        "%-45s%4.2f J (Total Power %.2fW)\n",
					        EventNames[i], event_values[i],
					        event_totals[i]);
				}
				nrm_client_send_event(client, after_time,
				                      sensor, scope,
				                      event_totals[i]);
			}
		}
	} while (!stop);

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

	for (i = 0; i < MAX_MEASUREMENTS; i++) {
		nrm_scope_destroy(nrm_cpu_scopes[i]);
	}

	for (i = 0; i < MAX_MEASUREMENTS; i++) {
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
