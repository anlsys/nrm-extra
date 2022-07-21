/*******************************************************************************
 * Copyright 2021 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

/* Filename: nrmpower_variorum.c
 *
 * Description: Implements power measurements via Variorum
 *               and the NRM downstream interface.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <hwloc.h>
#include <jansson.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <variorum.h>

#include <nrm.h>

static int log_level = 0;
volatile sig_atomic_t stop;

static nrm_client_t *client;
static nrm_scope_t *scope;
static nrm_sensor_t *sensor;

static char *upstream_uri = "tcp://127.0.0.1";
static int pub_port = 2345;
static int rpc_port = 3456;

char *usage =
        "usage: nrm-power [options] \n"
        "     options:\n"
        "            -v, --verbose           Produce verbose output. Log messages will be displayed to stderr\n"
        "            -h, --help              Displays this help message\n";

#define MAX_MEASUREMENTS 16

// handler for interrupt?
void interrupt(int signum)
{
	nrm_log_debug("Interrupt caught. Exiting loop.\n");
	stop = 1;
}

int get_cpu_idx(hwloc_topology_t topology, int cpu)
{
	hwloc_obj_t pu;
	pu = hwloc_get_pu_obj_by_os_index(topology, cpu);
	return pu->logical_index;
}

int main(int argc, char **argv)
{
	int char_opt, err;
	double freq = 1;
	json_t *json_measurements = json_object();

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
			nrm_log_error("Wrong option argument\n");
			nrm_log_error("%s", usage);
			exit(EXIT_FAILURE);
		}
	}

	nrm_init(NULL, NULL);
	assert(nrm_log_init(stderr, "nrmpower-variorum") == 0);

	nrm_log_setlevel(log_level);
	nrm_log_debug("NRM logging initialized.\n");

	nrm_client_create(&client, upstream_uri, pub_port, rpc_port);
	nrm_log_debug("NRM client initialized.\n");
	assert(client != NULL);

	// create sensor
	char *name = "variorum-power";
	sensor = nrm_sensor_create(name);

	// client add sensor
	assert(nrm_client_add_sensor(client, sensor) == 0);

	// 1st measure, only to determine viable measurements
	// without delta, watts values should all be zero
	// but since we're not reporting yet, that's ok
	assert(variorum_get_node_power_json(json_measurements) == 0);
	nrm_log_debug(
	        "Variorum first measurement performed. Detecting candidate fields and system topology.\n");

	hwloc_topology_t topology;
	hwloc_obj_t numanode;
	hwloc_cpuset_t cpus;

	nrm_scope_t *nrm_cpu_scopes[MAX_MEASUREMENTS],
	        *nrm_numa_scopes[MAX_MEASUREMENTS];
	int i, n_scopes = 0, n_numa_scopes = 0, n_cpu_scopes = 0, cpu_idx, cpu,
	       numa_id;
	const char *key, *json_soutput;
	json_t *value;

	assert(hwloc_topology_init(&topology) == 0);
	assert(hwloc_topology_load(topology) == 0);

	json_object_foreach(json_measurements, key, value)
	{
		// variorum inits un-measureable as -1.0, measureable as 0.0
		if (strstr(key, "socket") && (json_real_value(value) != -1.0)) {
			scope = nrm_scope_create();
			numa_id = key[strlen(key) - 1] - '0';

			if (strstr(key, "power_cpu_watts")) { // need NUMANODE
				// object to parse
				// CPU indexes
				numanode = hwloc_get_obj_by_type(
				        topology, HWLOC_OBJ_NUMANODE, numa_id);
				cpus = numanode->cpuset;

				hwloc_bitmap_foreach_begin(cpu, cpus)
				        cpu_idx = get_cpu_idx(topology, cpu);
				nrm_scope_add(scope, NRM_SCOPE_TYPE_CPU,
				              cpu_idx);
				hwloc_bitmap_foreach_end();

				nrm_cpu_scopes[numa_id] = scope;
				n_cpu_scopes++;

			} else if (strstr(key, "power_mem_watts")) {
				nrm_scope_add(scope, NRM_SCOPE_TYPE_NUMA,
				              numa_id);
				nrm_numa_scopes[numa_id] = scope;
				n_numa_scopes++;
			}
			nrm_client_add_scope(client, scope);
			n_scopes++;
		}
	}

	nrm_log_debug(
	        "%i Candidate socket fields detected. (%i CPU, %i NUMA) NRM scopes initialized.\n",
	        n_scopes, n_cpu_scopes, n_numa_scopes);

	// loop until ctrl+c interrupt?
	stop = 0;
	double sleeptime = 1;

	nrm_time_t before_time, after_time;
	int64_t elapsed_time;

	nrm_log_debug("Beginning loop. ctrl+c to exit.\n");
	do {

		nrm_time_gettime(&before_time);

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

		assert(variorum_get_node_power_json(json_measurements) == 0);

		json_object_foreach(json_measurements, key, value)
		{
			// variorum inits un-measureable as -1.0, measureable as
			// 0.0
			if (strstr(key, "socket") &&
			    (json_real_value(value) != -1.0)) {
				numa_id = key[strlen(key) - 1] - '0';

				if (strstr(key, "power_cpu_watts")) {
					scope = nrm_cpu_scopes[numa_id];
				} else if (strstr(key, "power_mem_watts")) {
					scope = nrm_numa_scopes[numa_id];
				}
				nrm_client_send_event(
				        client, nrm_time_fromns(elapsed_time),
				        sensor, scope, json_real_value(value));
			}
		}

		// Some verbose output just to look at numbers
		json_soutput = json_dumps(json_measurements, JSON_INDENT(4));
		nrm_log_debug("Variorum energy measurements:\n");
		nrm_log_debug("%s\n", json_soutput);

	} while (!stop);

	/* final send here */
	/* finalize program */

	for (i = 0; i < n_cpu_scopes; i++) {
		nrm_scope_destroy(nrm_cpu_scopes[i]);
	}

	for (i = 0; i < n_numa_scopes; i++) {
		nrm_scope_destroy(nrm_numa_scopes[i]);
	}

	nrm_log_debug("NRM scopes deleted.\n");
	nrm_client_destroy(&client);
	nrm_finalize();

	exit(EXIT_SUCCESS);
}
