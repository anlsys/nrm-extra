/*******************************************************************************
 * Copyright 2021 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

/* Filename: nrmpower_apmidg.c
 *
 * Description: Implements power measurements via APMIDG
 *               and the NRM downstream interface.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <libapmidg.h>
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

#include <nrm.h>

#include "extra.h"

#define MAX_MEASUREMENTS 16

static int log_level = NRM_LOG_DEBUG;

static nrm_client_t *client;
static nrm_scope_t *scope;
static nrm_sensor_t *sensor;
static nrm_actuator_t *actuator;

int apmidg_actuator_callback(nrm_uuid_t *uuid, double value)
{
	(void)uuid;
	int n_gpus = apmidg_getndevs();
	for (int i = 0; i < n_gpus; i++) {
		apmidg_setpwrlim(i, 0, value);
	}
	return 0;
}

static char *upstream_uri = "tcp://127.0.0.1";
static int pub_port = 2345;
static int rpc_port = 3456;

char *usage =
        "usage: nrm-power [options] \n"
        "     options:\n"
        "            -v, --verbose           Produce verbose output. Log messages will be displayed to stderr\n"
        "            -h, --help              Displays this help message\n";

int main(int argc, char **argv)
{
	int char_opt, err;
	double freq = 1;

	while (1) {
		static struct option long_options[] = {
		        {"verbose", no_argument, &log_level, 1},
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
	assert(nrm_log_init(stderr, "nrm.log.power-apmidg") == 0);

	nrm_log_setlevel(log_level);
	nrm_log_debug("NRM logging initialized.\n");

	nrm_client_create(&client, upstream_uri, pub_port, rpc_port);
	nrm_log_debug("NRM client initialized.\n");
	assert(client != NULL);

	// create sensor
	const char *name = "nrm.sensor.power-apmidg";
	sensor = nrm_sensor_create(name);

	// client add sensor
	assert(nrm_client_add_sensor(client, sensor) == 0);

	nrm_scope_t *nrm_gpu_scopes[MAX_MEASUREMENTS];
	int nrm_gpu_scope_added[MAX_MEASUREMENTS];
	int n_gpus = 0;

	apmidg_init(0);
	n_gpus = apmidg_getndevs();
	for (int i = 0; i < n_gpus; i++) {
		char *scope_name;
		int added;
		err = nrm_extra_create_name_ssu("nrm.apmidg", "gpu", i,
		                                &scope_name);
		nrm_scope_t *scope = nrm_scope_create(scope_name);
		nrm_scope_add(scope, NRM_SCOPE_TYPE_GPU, i);
		nrm_extra_find_scope(client, &scope, &added);
		free(scope_name);
		nrm_gpu_scopes[i] = scope;
		nrm_gpu_scope_added[i] = added;
	}

	// setup actuator
	actuator = nrm_actuator_create("nrm.apmidg.global");
	int dft, min, max;
	apmidg_getpwrprops(0, 0, 0, 0, 0, &dft, &min, &max);
	double choices[10];
	for (int i = 0; i < 10; i++) {
		// values are in milliwatt, cut it in 10
		double delta = (max - min) / 10;
		choices[i] = min + i * delta;
	}
	nrm_actuator_set_choices(actuator, 10, choices);
	nrm_actuator_set_value(actuator, dft);
	err = nrm_client_add_actuator(client, actuator);
	if (err) {
		nrm_log_error("error during client request\n");
		return EXIT_FAILURE;
	}

	nrm_log_info("starting actuate callback\n");
	nrm_client_set_actuate_listener(client, apmidg_actuator_callback);
	nrm_client_start_actuate_listener(client);

	double sleeptime = 1 / freq;

	nrm_time_t before_time, after_time;
	int64_t elapsed_time;

	while (1) {
		/* sleep for a frequency */
		struct timespec req, rem;
		req.tv_sec = ceil(sleeptime);
		req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;

		err = nanosleep(&req, &rem);
		if (err == -1 && errno == EINTR) {
			nrm_log_error("interupted during sleep, exiting\n");
			break;
		}

		for (int i = 0; i < n_gpus; i++) {
			int npwrdoms = apmidg_getnpwrdoms(i);
			uint64_t energy = 0;
			uint64_t ts = 0;
			apmidg_readenergy(i, 0, &energy, &ts);
			nrm_time_t time = nrm_time_fromns(ts);
			nrm_client_send_event(client, time, sensor,
			                      nrm_gpu_scopes[i], energy);
		}
	}

	/* finalize program */
	for (int i = 0; i < n_gpus; i++) {
		if (nrm_gpu_scope_added[i])
			nrm_client_remove_scope(client, nrm_gpu_scopes[i]);
		nrm_scope_destroy(nrm_gpu_scopes[i]);
	}
	nrm_client_remove_sensor(client, sensor);
	nrm_sensor_destroy(&sensor);
	nrm_client_destroy(&client);
	apmidg_finish();
	nrm_finalize();
	exit(EXIT_SUCCESS);
}
