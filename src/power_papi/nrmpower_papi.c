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

static int log_level = NRM_LOG_ERROR;
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

bool is_energy_event(const char *event_name, uint64_t data_type)
{
	return (strncmp(event_name, "powercap:::ENERGY_UJ:",
	                strlen("powercap:::ENERGY_UJ:")) == 0 &&
	        (data_type == PAPI_DATATYPE_UINT64));
}

bool is_name_event(const char *event_name)
{
	return strncmp(event_name,
	               "powercap:::NAME:", strlen("powercap:::NAME:")) == 0;
}

bool is_dram_event(const char *event_desc)
{
	return strcmp(event_desc, "dram") == 0;
}

double get_watts(double event_value, int64_t elapsed_time)
{
	return ((double)event_value / 1.0e6) / (elapsed_time / 1.0e9);
}

int get_zone_id(const char *event_name)
{
	// Extract zone (not subzone) number from event names such as:
	// powercap:::ENERGY_UJ:ZONE0
	// powercap:::ENERGY_UJ:ZONE0_SUBZONE0
	// powercap:::NAME:ZONE0
	const char *zone;
	zone = strstr(event_name, ":ZONE");
	assert(zone);
	return strtol(zone + strlen(":ZONE"), NULL, 10);
}

int get_subzone_id(const char *event_name)
{
	// Extract optional subzone number from event names such as:
	// powercap:::ENERGY_UJ:ZONE0_SUBZONE0
	// powercap:::NAME:ZONE0_SUBZONE0
	// If none found, return -1.
	const char *subzone;
	if ((subzone = strstr(event_name, "_SUBZONE")) != NULL)
		return strtol(subzone + strlen("_SUBZONE"), NULL, 10);
	else
		return -1;
}

int get_zone_name_id(
        int zone_id,
        const char EventNames[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN],
        int num_events)
{
	// Look for the index of the NAME event for the zone (not the subzone).
	for (int i = 0; i < num_events; i++)
		if (is_name_event(EventNames[i]) &&
		    get_zone_id(EventNames[i]) == zone_id &&
		    get_subzone_id(EventNames[i]) == -1)
			return i;

	return -1;
}

int get_package_id(char *event_name,
                   const char EventNames[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN],
                   char *const EventDescs[MAX_powercap_EVENTS],
                   int num_events,
                   const char **desc)
{
	// Zone IDs and package IDs need not be the same, e.g.,
	// /sys/class/powercap/intel-rapl:2/name may be "package-1", and PAPI
	// inherits that from RAPL.
	int zone_id = get_zone_id(event_name);
	// Get the index of the name event for this zone, which will contain
	// the package name in the description.
	int zone_name_id = get_zone_name_id(zone_id, EventNames, num_events);
	assert(zone_name_id != -1);
	if (desc)
		*desc = EventDescs[zone_name_id];
	if (strncmp(EventDescs[zone_name_id], "package-", strlen("package-")) ==
	    0)
		return strtol(EventDescs[zone_name_id] + strlen("package-"),
		              NULL, 10);
	else
		return -1;
}

const char *
get_subzone_desc(char *event_name,
                 const char EventNames[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN],
                 char *const EventDescs[MAX_powercap_EVENTS],
                 int num_events)
{
	int zone_id = get_zone_id(event_name);
	int subzone_id = get_subzone_id(event_name);
	if (subzone_id == -1)
		return NULL;
	for (int i = 0; i < num_events; i++)
		if (is_name_event(EventNames[i]) &&
		    get_zone_id(EventNames[i]) == zone_id &&
		    get_subzone_id(EventNames[i]) == subzone_id)
			return EventDescs[i];
	assert(0);
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

	while (1) {
		static struct option long_options[] = {
		        {"verbose", no_argument, 0, 'v'},
		        {"help", no_argument, 0, 'h'},
		        {"frequency", required_argument, 0, 'f'},
		        {0, 0, 0, 0}};

		int option_index = 0;
		char_opt = getopt_long(argc, argv, "vhf:", long_options,
		                       &option_index);

		if (char_opt == -1)
			break;
		switch (char_opt) {
		case 0:
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

	nrm_init(NULL, NULL);
	assert(nrm_log_init(stderr, "nrm.extra.papi") == 0);

	nrm_log_setlevel(log_level);
	nrm_log_debug("NRM logging initialized.\n");

	nrm_client_create(&client, upstream_uri, pub_port, rpc_port);
	nrm_log_debug("NRM client initialized.\n");
	assert(client != NULL);

	// create sensor
	sensor = nrm_sensor_create("nrm.sensor.power-papi");

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
	char *EventDescs[MAX_powercap_EVENTS];
	int NumaIDs[MAX_powercap_EVENTS];
	PAPI_event_info_t EventInfo;

	papi_retval = PAPI_enum_cmp_event(&EventCode, PAPI_ENUM_FIRST,
	                                  powercap_component_id);
	while (papi_retval == PAPI_OK) {

		assert(PAPI_event_code_to_name(
		               EventCode, EventNames[num_events]) == PAPI_OK);
		nrm_log_debug("code: %d, event: %s\n", EventCode,
		              EventNames[num_events]);
		assert(PAPI_get_event_info(EventCode, &EventInfo) == PAPI_OK);
		assert(num_events < MAX_powercap_EVENTS);
		DataTypes[num_events] = EventInfo.data_type;
		if (strlen(EventInfo.long_descr) > 0) {
			if (EventInfo.long_descr[strlen(EventInfo.long_descr) -
			                         1] == '\n')
				EventInfo.long_descr
				        [strlen(EventInfo.long_descr) - 1] =
				        '\0';
			EventDescs[num_events] = strdup(EventInfo.long_descr);
			nrm_log_debug("long_descr %s\n", EventInfo.long_descr);
		} else
			EventDescs[num_events] = NULL;

		num_events++;
		papi_retval = PAPI_enum_cmp_event(&EventCode, PAPI_ENUM_EVENTS,
		                                  powercap_component_id);
	}

	hwloc_topology_t topology;
	hwloc_obj_t numanode;
	hwloc_cpuset_t cpus;

	// These arrays are indexed by energy event id [0..n_energy_events-1].
	nrm_scope_t *nrm_scopes[MAX_MEASUREMENTS];
	bool nrm_scopes_free[MAX_MEASUREMENTS];
	const char *nrm_event_names[MAX_MEASUREMENTS];

	int n_energy_events = 0, n_scopes = 0, n_numa_scopes = 0,
	    n_cpu_scopes = 0, cpu_idx, cpu, numa_id;
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
			const char *zone_desc;

			nrm_log_debug("energy event detected %s\n", event);

			if ((numa_id = get_package_id(event, EventNames,
			                              EventDescs, num_events,
			                              &zone_desc)) == -1) {
				nrm_log_debug(
				        "skipping; not part of a package (%s)\n",
				        zone_desc);
				continue;
			}

			const char *subzone_desc = get_subzone_desc(
			        event, EventNames, EventDescs, num_events);

			if (subzone_desc != NULL) {
				if (is_dram_event(subzone_desc)) {
					err = nrm_extra_create_name_ssu(
					        "nrm.papi", "numa", numa_id,
					        &scope_name);
					nrm_log_debug(
					        "Creating new scope: %s\n",
					        scope_name);

					scope = nrm_scope_create(scope_name);
					nrm_scope_add(scope,
					              NRM_SCOPE_TYPE_NUMA,
					              numa_id);
					nrm_extra_find_scope(client, &scope,
					                     &added);
					free(scope_name);

					n_numa_scopes++;

					nrm_log_debug(
					        "adding NUMA event (%s/%s)\n",
					        zone_desc, subzone_desc);
				} else {
					nrm_log_debug(
					        "skipping; not a NUMA event (%s/%s)\n",
					        zone_desc, subzone_desc);
					continue;
				}
			} else { // need NUMANODE object to parse CPU
				 // indexes
				err = nrm_extra_create_name_ssu("nrm.papi",
				                                "cpu", numa_id,
				                                &scope_name);
				nrm_log_debug("Creating new scope: %s\n",
				              scope_name);

				scope = nrm_scope_create(scope_name);
				numanode = hwloc_get_obj_by_type(
				        topology, HWLOC_OBJ_NUMANODE, numa_id);
				cpus = numanode->cpuset;
				hwloc_bitmap_foreach_begin(cpu, cpus)
				{
					cpu_idx = get_cpu_idx(topology, cpu);
					nrm_scope_add(scope, NRM_SCOPE_TYPE_CPU,
					              cpu_idx);
				}
				hwloc_bitmap_foreach_end();
				nrm_extra_find_scope(client, &scope, &added);
				free(scope_name);

				n_cpu_scopes++;

				nrm_log_debug("adding CPU event (%s)\n",
				              zone_desc);
			}

			nrm_scopes[n_energy_events] = scope;
			nrm_scopes_free[n_energy_events] = added;
			nrm_event_names[n_energy_events] = event;
			n_energy_events++;
			if (added)
				n_scopes++;

			assert(PAPI_add_named_event(EventSet, event) ==
			       PAPI_OK);
		}
	}

	nrm_log_debug("%d relevant energy events detected.\n", n_energy_events);
	nrm_log_debug("NRM scopes initialized: %d NUMA, %d CPU (%d new)\n",
	              n_numa_scopes, n_cpu_scopes, n_scopes);

	long long *event_values;
	nrm_time_t last_time, current_time;
	int64_t elapsed_time;
	double watts_value, *event_totals;

	event_values = calloc(n_energy_events, sizeof(long long));
	event_totals = calloc(n_energy_events, sizeof(double)); // converting
	                                                        // then storing

	nrm_time_gettime(&last_time);

	assert(PAPI_start(EventSet) == PAPI_OK);

	stop = 0;
	double sleeptime = 1 / freq;

	while (true) {

		/* sleep for a frequency */
		struct timespec req, rem;
		req.tv_sec = sleeptime;
		req.tv_nsec = (sleeptime - req.tv_sec) * 1e9;

		err = nanosleep(&req, &rem);
		if (err == -1 && errno == EINTR) {
			nrm_log_error("interupted during sleep, exiting\n");
			break;
		}

		// Read EventSet measurements into "event_values"...
		assert(PAPI_read(EventSet, event_values) == PAPI_OK);

		nrm_time_gettime(&current_time);
		elapsed_time = nrm_time_diff(&last_time, &current_time);

		nrm_log_debug("scaled energy measurements:\n");
		for (i = 0; i < n_energy_events; i++) {
			watts_value = get_watts(event_values[i] -
			                                event_totals[i] * 1e6,
			                        elapsed_time);
			event_totals[i] = event_values[i] / 1e6;

			nrm_log_debug("%-45s%4f J (avg. power %f W)\n",
			              nrm_event_names[i], event_totals[i],
			              watts_value);

			scope = nrm_scopes[i];

			err = nrm_client_send_event(client, current_time,
			                            sensor, scope,
			                            event_totals[i]);
		}
		if (err == -1 || errno == EINTR) {
			nrm_log_error("Interrupted. Exiting\n");
			break;
		}

		last_time = current_time;
	}

	int papi_status;
	assert(PAPI_state(EventSet, &papi_status) == PAPI_OK);
	if (papi_status == PAPI_RUNNING) {
		assert(PAPI_stop(EventSet, event_values) == PAPI_OK);

		nrm_time_gettime(&current_time);

		/* final send here */
		for (i = 0; i < n_energy_events; i++) {
			event_totals[i] = event_values[i] / 1e6;

			scope = nrm_scopes[i];

			nrm_client_send_event(client, current_time, sensor,
			                      scope, event_totals[i]);
		}
	}

	for (i = 0; i < n_scopes; i++)
		if (nrm_scopes_free[i]) {
			nrm_client_remove_scope(client, nrm_scopes[i]);
			nrm_scope_destroy(nrm_scopes[i]);
		}
	nrm_log_debug("NRM scopes deleted.\n");

	for (i = 0; i < num_events; i++)
		free(EventDescs[i]);

	nrm_sensor_destroy(&sensor);
	nrm_client_destroy(&client);

	nrm_finalize();
	free(event_values);
	free(event_totals);

	exit(EXIT_SUCCESS);
}
