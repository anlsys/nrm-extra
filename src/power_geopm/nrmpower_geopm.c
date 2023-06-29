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

#include <limits.h>
#include <stddef.h>
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

struct signal_info_s {
	char *signal_name;
	char *domain_token;
	int domain_type;
};

typedef struct signal_info_s signal_info_t;

int main(int argc, char **argv)
{
	int j, char_opt, err;
	double freq = 1;
	size_t i, MAX_SIGNAL_NAME_LENGTH = 55;
	nrm_vector_t *signal_args = NULL;
	nrm_vector_t *signal_info_list = NULL;

	// a vector of GEOPM signal names; will be pushed into by e.g.: -s
	// DRAM_POWER -s CPU_POWER
	assert(nrm_vector_create(&signal_args, sizeof(char *)) == NRM_SUCCESS);
	assert(nrm_vector_create(&signal_info_list, sizeof(signal_info_t *)) ==
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
			nrm_vector_push_back(signal_args, optarg);
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
	assert(nsignals > 0); // just check that we can obtain signals

	size_t n_signals = 1;
	assert(nrm_vector_length(signal_args, &n_signals) == NRM_SUCCESS);

	// this loop will obtain our signal information
	int domain_type;
	char *signal_name, full_domain_name[NAME_MAX + 1];
	for (i = 0; i < n_signals; i++) {
		void *p;
		nrm_vector_get(signal_args, i, &p);
		signal_name = (char *)p;

		signal_info_t *ret = calloc(1, sizeof(signal_info_t));
		ret->signal_name = signal_name;
		ret->domain_type = domain_type;

		domain_type = geopm_pio_signal_domain_type(signal_name);
		if (domain_type < 0) {
			nrm_log_error(
			        "Unable to parse domain. Either the signal name is incorrect, or you must sudo-run this utility.\n"); // GEOPM_DOMAIN_INVALID = -1
			exit(EXIT_FAILURE);
		}

		err = geopm_topo_domain_name(domain_type, NAME_MAX,
		                             full_domain_name);
		assert(err == 0);
		nrm_log_debug("We get signal: %s. Main screen turn on.\n",
		              full_domain_name);
		ret->domain_token = full_domain_name;

		nrm_log_debug("We get token: %s. \n", ret->domain_token);
		nrm_vector_push_back(signal_info_list, ret);
	}

	hwloc_topology_t topology;
	hwloc_obj_t socket;
	hwloc_cpuset_t cpus;

	assert(hwloc_topology_init(&topology) == 0);
	assert(hwloc_topology_load(topology) == 0);

	nrm_scope_t *custom_scopes[n_signals], *scopes[n_signals];
	signal_info_t signal_info;

	char *suffix, *scope_name;
	int component_idxs[256], added, n_scopes = 0, n_numa_scopes = 0,
	                                n_cpu_scopes = 0, n_custom_scopes = 0,
	                                n_gpu_scopes = 0, cpu_idx, cpu,
	                                numa_id = 0;
	// TODO: determine numa_id from hwloc

	for (i = 0; i < n_signals; i++) {
		void *p;
		nrm_vector_get(signal_info_list, i, &p);
		signal_info = (signal_info_t *)p;

		int num_domains =
		        geopm_topo_num_domain(signal_info->domain_type);
		assert(num_domains >= 0);
		suffix = signal_info->domain_token;

		err = nrm_extra_create_name_ssu("nrm.geopm", suffix, 0,
		                                &scope_name); // what index
		                                              // should we use?
		scope = nrm_scope_create(scope_name);

		// signals like CPU_POWER belong to "package"
		if (strcmp(suffix, "cpu") || strcmp(suffix, "package")) {
			// lets use hwloc CPU indexes instead
			socket = hwloc_get_obj_by_type(
			        topology, HWLOC_OBJ_SOCKET, numa_id);
			cpus = socket->cpuset;
			hwloc_bitmap_foreach_begin(cpu, cpus)
			        cpu_idx = get_cpu_idx(topology, cpu);
			nrm_scope_add(scope, NRM_SCOPE_TYPE_CPU, cpu_idx);
			hwloc_bitmap_foreach_end();
			n_cpu_scopes++;
		} else if (strcmp(suffix, "gpu")) {
			for (j = 0; j < num_domains; j++) {
				nrm_scope_add(scope, NRM_SCOPE_TYPE_GPU, j);
			}
			n_gpu_scopes++;
		} else if (strcmp(suffix, "memory")) {
			for (j = 0; j < num_domains; j++) {
				nrm_scope_add(scope, NRM_SCOPE_TYPE_NUMA, j);
			}
			n_numa_scopes++;
		}
		nrm_extra_find_scope(client, &scope, &added);
		if (added) {
			custom_scopes[n_custom_scopes] = scope;
			n_custom_scopes++;
		}
		scopes[i] = scope;
	}

	nrm_log_debug(
	        "%d NRM scopes initialized (%d NUMA, %d CPU, %d GPU, %d custom)\n",
	        n_scopes, n_numa_scopes, n_cpu_scopes, n_gpu_scopes,
	        n_custom_scopes);

	nrm_time_t before_time, after_time;
	int64_t elapsed_time;
	double *event_totals;

	event_totals = calloc(n_signals, sizeof(double)); // converting then
	                                                  // storing
	stop = 0;
	int num_domains;
	char *domain_token;
	double sleeptime = 1 / freq;

	while (true) {

		nrm_time_gettime(&before_time);

		/* sleep for a frequency */
		struct timespec req, rem;
		req.tv_sec = ceil(sleeptime);
		req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;

		err = nanosleep(&req, &rem);
		if (err == -1 && errno == EINTR) {
			nrm_log_error("interrupted during sleep, exiting\n");
			break;
		}

		nrm_time_gettime(&after_time);
		elapsed_time = nrm_time_diff(&before_time, &after_time);

		for (i = 0; i < n_signals; i++) {
			void *p;
			nrm_vector_get(signal_info_list, i, &p);
			signal_info = (signal_info_t *)p;

			num_domains =
			        geopm_topo_num_domain(signal_info->domain_type);

			double total = 0;
			for (j = 0; j < num_domains; j++) { // accumulate
				                            // measurements
				double value = 0;
				err = geopm_pio_read_signal(
				        signal_info->signal_name,
				        signal_info->domain_type, j, &value);
				total += value;
			}
			event_totals[i] = total;
			nrm_log_debug("%s:%s - energy measurement: %d\n",
			              signal_info->domain_token,
			              signal_info->signal_name, total);
			// need to get our matching scope
			nrm_client_send_event(client, after_time, sensor,
			                      scopes[i], total);
		}

		if (err == -1 || errno == EINTR) {
			nrm_log_error("Interrupted. Exiting\n");
			break;
		}
	}

	/* final send here */
	for (i = 0; i < n_signals; i++) {
		void *p;
		nrm_vector_get(signal_info_list, i, &p);
		signal_info = (signal_info_t *)p;

		num_domains = geopm_topo_num_domain(signal_info->domain_type);

		double total = 0;
		for (j = 0; j < num_domains; j++) { // accumulate measurements
			double value = 0;
			err = geopm_pio_read_signal(signal_info->signal_name,
			                            signal_info->domain_type, j,
			                            &value);
			total += value;
		}
		event_totals[i] = total;
		nrm_client_send_event(client, after_time, sensor, scopes[i],
		                      total);
	}

	for (j = 0; j < n_custom_scopes; j++) {
		nrm_client_remove_scope(client, custom_scopes[j]);
	}
	for (i = 0; i < n_signals; i++) {
		nrm_scope_destroy(scopes[i]);
	}

	nrm_log_debug("NRM scopes deleted.\n");

	nrm_sensor_destroy(&sensor);
	nrm_client_destroy(&client);

	nrm_vector_destroy(&signal_args);
	nrm_vector_destroy(&signal_info_list);

	nrm_finalize();
	free(event_totals);
	hwloc_bitmap_free(cpus);
	hwloc_topology_destroy(topology);

	exit(EXIT_SUCCESS);
}
