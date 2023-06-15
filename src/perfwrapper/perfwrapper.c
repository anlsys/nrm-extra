/*******************************************************************************
 * Copyright 2021 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

/* Filename: perfwrapper.c
 *
 * Description: Implements middleware between papi and the NRM
 *              downstream interface.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <papi.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <nrm.h>

#include "extra.h"

#define UPSTREAM_URI "tcp://127.0.0.1"
#define PUB_PORT 2345
#define RPC_PORT 3456

int main(int argc, char **argv)
{
	int c, err;
	double freq = 1;
	char EventCodeStr[PAPI_MAX_STR_LEN] = "PAPI_TOT_INS";
	int ret = EXIT_FAILURE;

	nrm_client_t *client;
	nrm_scope_t *scope;
	nrm_sensor_t *sensor;
	int custom_scope = 0;

	int log_level = NRM_LOG_QUIET;

	const char *usage =
		"Usage: nrm-perfwrapper [options] [command]\n"
		"     options:\n"
		"            -e, --event             PAPI preset event name. Default: PAPI_TOT_INS\n"
		"            -f, --frequency         Frequency in hz to poll. Default: 10.0\n"
		"            -v, --verbose           Produce verbose output. Log messages will be displayed to stderr\n"
		"            -h, --help              Displays this help message\n";

	while (1) {
		static struct option long_options[] = {
		        {"verbose", no_argument, 0, 'v'},
		        {"frequency", required_argument, 0, 'f'},
		        {"help", no_argument, 0, 'h'},
		        {"event", required_argument, 0, 'e'},
		        {0, 0, 0, 0}};

		int option_index = 0;
		c = getopt_long(argc, argv, "+vf:e:h", long_options,
		                &option_index);

		if (c == -1)
			break;
		switch (c) {
		case 0:
			break;
		case 'v':
			log_level = NRM_LOG_DEBUG;
			break;
		case 'f':
			errno = 0;
			freq = strtod(optarg, NULL);
			if (errno != 0 || freq <= 0) {
				fprintf(stderr,
					"Error parsing the frequency\n");
				goto cleanup_preinit;
			}
			break;
		case 'e':
			strcpy(EventCodeStr, optarg);
			break;
		case 'h':
			fprintf(stderr, "%s", usage);
			goto cleanup_preinit;
		case '?':
		default:
			fprintf(stderr, "Wrong option argument\n");
			fprintf(stderr, "%s", usage);
			goto cleanup_preinit;
		}
	}

	if (nrm_init(NULL, NULL) != 0) {
		fprintf(stderr, "nrm_init failed\n");
		goto cleanup_preinit;
	}
	if (nrm_log_init(stderr, "nrm.extra.perf") != 0) {
		fprintf(stderr, "nrm_log_init failed\n");
		goto cleanup_postinit;
	}

	nrm_log_setlevel(log_level);
	nrm_log_debug("NRM logging initialized.\n");

	// create client
	if (nrm_client_create(&client, UPSTREAM_URI, PUB_PORT, RPC_PORT) != 0 || client == NULL) {
		nrm_log_error("Client creation failed\n");
		goto cleanup_postinit;
	}

	nrm_log_debug("NRM client initialized.\n");

	nrm_log_debug("verbose=%d; freq=%f; event=%s\n", log_level, freq,
	              EventCodeStr);

	if (optind >= argc) {
		nrm_log_error("Expected command after options.\n");
		goto cleanup_client;
	}

	if (nrm_extra_find_allowed_scope(client, "nrm.extra.perf", &scope,
					 &custom_scope) != 0) {
		nrm_log_error("Finding scope failed\n");
		goto cleanup_client;
	}
	nrm_log_debug("NRM scope initialized.\n");

	/* create our sensor and add it to the daemon */
	char *sensor_name;
	if (nrm_extra_create_name("nrm.extra.perf", &sensor_name) != 0) {
		nrm_log_error("Name creation failed");
		goto cleanup_scope;
	}
	if ((sensor = nrm_sensor_create(sensor_name)) == NULL) {
		nrm_log_error("Sensor creation failed");
		goto cleanup_scope;
	}
	free(sensor_name);
	if (nrm_client_add_sensor(client, sensor) != 0) {
		nrm_log_error("Adding sensor failed");
		goto cleanup_sensor;
	}

	// initialize PAPI
	int papi_retval;
	papi_retval = PAPI_library_init(PAPI_VER_CURRENT);

	if (papi_retval != PAPI_VER_CURRENT) {
		nrm_log_error("PAPI library init error: %s\n",
		              PAPI_strerror(papi_retval));
		goto cleanup;
	}

	nrm_log_debug("PAPI initialized.\n");

	/* setup PAPI interface */
	int EventCode, EventSet = PAPI_NULL;

	err = PAPI_event_name_to_code(EventCodeStr, &EventCode);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI event_name translation error: %s\n",
		              PAPI_strerror(err));
		goto cleanup;
	}
	err = PAPI_create_eventset(&EventSet);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI eventset creation error: %s\n",
		              PAPI_strerror(err));
		goto cleanup;
	}

	err = PAPI_add_event(EventSet, EventCode);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI eventset append error: %s\n",
		              PAPI_strerror(err));
		goto cleanup;
	}

	nrm_log_debug(
	        "PAPI code string %s converted to PAPI code %i, and registered.\n",
	        EventCodeStr, EventCode);

	/* launch? command, sample counters */
	long long counter;

	int pid = fork();
	if (pid < 0) {
		nrm_log_error("perfwrapper fork error\n");
		goto cleanup;
	} else if (pid == 0) {
		/* child, needs to exec the cmd */
		err = execvp(argv[optind], &argv[optind]);
		nrm_log_error("Error executing command: %s\n",
		              PAPI_strerror(errno));
		_exit(EXIT_FAILURE);
	}

	/* us, need to sample counters */
	err = PAPI_attach(EventSet, pid);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI eventset attach error: %s\n",
		              PAPI_strerror(err));
		goto cleanup;
	}
	nrm_log_debug("PAPI attached to process with pid %i\n", pid);

	err = PAPI_start(EventSet);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI start error: %s\n", PAPI_strerror(err));
		goto cleanup;
	}
	nrm_log_debug("PAPI started. Initializing event read/send to NRM\n");

	nrm_time_t time;
	do {

		/* sleep for a frequency */
		long long sleeptime = 1e9 / freq;
		struct timespec req, rem;
		req.tv_sec = sleeptime / 1000000000;
		req.tv_nsec = sleeptime % 1000000000;
		/* deal with signal interrupts */
		do {
			err = nanosleep(&req, &rem);
			req = rem;
		} while (err == -1 && errno == EINTR);

		/* sample and report */
		err = PAPI_read(EventSet, &counter);
		if (err != PAPI_OK) {
			nrm_log_error("PAPI event read error: %s\n",
			              PAPI_strerror(err));
			goto cleanup;
		}
		nrm_log_debug("PAPI counter read.\n");

		nrm_time_gettime(&time);
		nrm_log_debug("NRM time obtained.\n");

		if (nrm_client_send_event(client, time, sensor, scope, counter) != 0) {
			nrm_log_error("Sending event to the daemon error\n");
			goto cleanup;
		}
		nrm_log_debug("NRM value sent.\n");

		/* loop until child exits */
		int status;
		err = waitpid(pid, &status, WNOHANG);
		if (err == -1) {
			nrm_log_error("waitpid error: %s\n",
			              strerror(errno));
			goto cleanup;
		}
	} while (err != pid);
	ret = EXIT_SUCCESS;
	nrm_log_debug("Finalizing PAPI-event read/send to NRM.\n");

	/* final send here */
	PAPI_stop(EventSet, &counter);
	nrm_client_send_event(client, time, sensor, scope, counter);

cleanup:
cleanup_sensor:
	nrm_sensor_destroy(&sensor);
cleanup_scope:
	/* if we had to add the scope to the daemon, make sure to clean up after
	 * ourselves
	 */
	if (custom_scope)
		nrm_client_remove_scope(client, scope);
	nrm_scope_destroy(scope);
cleanup_client:
	nrm_client_destroy(&client);
cleanup_postinit:
	nrm_finalize();
cleanup_preinit:
	exit(ret);
}
