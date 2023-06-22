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
#include <limits.h>
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

	// a vector of GEOPM signal names; will be pushed into by e.g.: -s
	// DRAM_POWER -s CPU_POWER
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
	assert(nsignals > 0); // just check that we can obtain signals

	// we've stored all signals in vector, we need to store corresponding
	// domain_type labels (ints), domain_name tokens (str) and
	//	component idxs for those domains
	// domain_type labels: used for sampling signals
	// domain_name tokens: used for creating NRM scopes
	// component idxs: will add to scopes, and used to sampling

	size_t n_signals = 1;
	assert(nrm_vector_length(signals, &n_signals) == NRM_SUCCESS);

	int DomainTypes[n_signals];
	char DomainTokens[n_signals][NAME_MAX];
	char SignalNames[n_signals][NAME_MAX]; // only needed for signal reading

	// this loop will obtain our labels and tokens
	int domain_type;
	char *signal_name, *full_domain_name;
	for (i = 0; i < n_signals; i++) {
		// pull out requested signal, convert to integer label
		void *p;
		nrm_vector_get(signals, i, &p);
		signal_name = (char*)p;
		// SignalNames[i] = signal_name;
		strcpy(SignalNames[i], signal_name);
		domain_type = geopm_pio_signal_domain_type(signal_name);
		assert(domain_type >= 0); // GEOPM_DOMAIN_INVALID = -1
		DomainTypes[i] = domain_type;

		// convert integer label to full domain name
		err = geopm_topo_domain_name(domain_type, NAME_MAX,
		                             full_domain_name);
		assert(err == 0);
		nrm_log_debug("We get signal: %s. Main screen turn on.\n",
		              full_domain_name);

		// convert full name to last component. e.g.: GEOPM_DOMAIN_CPU
		// -> CPU
		// DomainTokens[i] = get_domain_label(full_domain_name);
		strcpy(DomainTokens[i], get_domain_label(full_domain_name));
		nrm_log_debug("We get token: %s. \n", DomainTokens[i]);
	}

	hwloc_topology_t topology;
	hwloc_obj_t numanode;
	hwloc_cpuset_t cpus;

	assert(hwloc_topology_init(&topology) == 0);
	assert(hwloc_topology_load(topology) == 0);

	nrm_scope_t *nrm_cpu_scopes[n_signals], *nrm_numa_scopes[n_signals],
	        *nrm_gpu_scopes[n_signals], *custom_scopes[n_signals], *scopes[n_signals];

	char *suffix, *scope_name;
	int component_idxs[256], added, n_scopes = 0, n_numa_scopes = 0,
	                                n_cpu_scopes = 0, n_custom_scopes = 0,
	                                n_gpu_scopes = 0, cpu_idx, cpu, numa_id;

	// TODO: loop over domains, get valid subcomponent indexes, create
	// scopes
	for (i = 0; i < n_signals; i++) {
		int num_domains = geopm_topo_num_domain(DomainTypes[i]);
		assert(num_domains >= 0);
		suffix = DomainTokens[i];

		err = nrm_extra_create_name_ssu("nrm.geopm", suffix, 0,
		                                &scope_name); // what index
		                                              // should we use?
		scope = nrm_scope_create(scope_name);

		if (strcmp(suffix, "CPU")) {
			// lets use hwloc CPU indexes instead
			numanode = hwloc_get_obj_by_type(
			        topology, HWLOC_OBJ_NUMANODE, numa_id);
			cpus = numanode->cpuset;
			hwloc_bitmap_foreach_begin(cpu, cpus)
			        cpu_idx = get_cpu_idx(topology, cpu);
			nrm_scope_add(scope, NRM_SCOPE_TYPE_CPU, cpu_idx);
			hwloc_bitmap_foreach_end();
		} else if (strcmp(suffix, "GPU")) {
			for (j = 0; j < num_domains; j++) {
				nrm_scope_add(scope, NRM_SCOPE_TYPE_GPU, j);
			}
		} else if (strcmp(suffix, "MEMORY")) {
			for (j = 0; j < num_domains; j++) {
				nrm_scope_add(scope, NRM_SCOPE_TYPE_NUMA, j);
			}
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
	int domain_idx, num_domains;
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
			nrm_log_error("interupted during sleep, exiting\n");
			break;
		}

		nrm_time_gettime(&after_time);
		elapsed_time = nrm_time_diff(&before_time, &after_time);

		for (i = 0; i < n_signals; i++) {
			signal_name = SignalNames[i];
			domain_type = DomainTypes[i];
			domain_token = DomainTokens[i];

			num_domains = geopm_topo_num_domain(DomainTypes[i]);

			double total = 0;
			for (j = 0; j < num_domains; j++) { // accumulate
				                            // measurements
				double value = 0;
				err = geopm_pio_read_signal(
				        signal_name, domain_type, j, &value);
				total += value;
			}
			event_totals[i] = total;
			nrm_log_debug("%s:%s - energy measurement: %d\n",
			              domain_token, signal_name,
			              event_totals[i]);
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
		signal_name = SignalNames[i];
		domain_type = DomainTypes[i];

		num_domains = geopm_topo_num_domain(DomainTypes[i]);

		double total = 0;
		for (j = 0; j < num_domains; j++) { // accumulate measurements
			double value = 0;
			err = geopm_pio_read_signal(signal_name, domain_type, j,
			                            &value);
			total += value;
		}
		event_totals[i] = total;
		nrm_client_send_event(client, after_time, sensor, scopes[i],
		                      total);
	}

	for (i = 0; i < n_custom_scopes; i++) {
		nrm_client_remove_scope(client, custom_scopes[i]);
	}
	for (i = 0; i < n_signals; i++) {
		nrm_scope_destroy(scopes[i]);
	}

	nrm_log_debug("NRM scopes deleted.\n");

	nrm_sensor_destroy(&sensor);
	nrm_client_destroy(&client);

	nrm_finalize();
	free(event_totals);

	exit(EXIT_SUCCESS);
}
