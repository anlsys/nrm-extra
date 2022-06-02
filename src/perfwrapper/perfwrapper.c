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

static struct nrm_client_t *client;
static struct nrm_scope *scope;
static struct nrm_sensor_t *sensor;

static char *upstream_uri = NRM_DEFAULT_UPSTREAM_URI;
static int pub_port = NRM_DEFAULT_UPSTREAM_PUB_PORT;
static int rpc_port = NRM_DEFAULT_UPSTREAM_RPC_PORT;

static int log_level = 0;

char *usage =
        "usage: perfwrapper [options] -e [papi event] [command]\n"
        "     options:\n"
        "            -e, --event             PAPI preset event name. Default: PAPI_TOT_INS\n"
        "            -f, --frequency         Frequency in hz to poll. Default: 10.0\n"
        "            -v, --verbose           Produce verbose output. Log messages will be displayed to stderr\n"
        "            -h, --help              Displays this help message\n";


int main(int argc, char **argv)
{
	int c, err;
	double freq = 10;
	char EventCodeStr[PAPI_MAX_STR_LEN] = "PAPI_TOT_INS";
	char EventDescr[PAPI_MAX_STR_LEN];
	char EventLabel[20];
	char *cmd;

	while (1) {
		static struct option long_options[] = {
		        {"verbose", no_argument, &log_level, 1},
		        {"frequency", optional_argument, 0, 'f'},
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
		case 'f':
			errno = 0;
			freq = strtod(optarg, NULL);
			if (errno != 0 || freq == 0) {
				nrm_log_error("Error during conversion to double: %d\n", errno);
				exit(EXIT_FAILURE);
			}
			break;
		case 'e':
			strcpy(EventCodeStr, optarg);
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

	nrm_log_debug("verbose=%d; freq=%f; event=%s\n", log_level, freq,
	        EventCodeStr);

	if (optind >= argc) {
		nrm_log_error("Expected command after options.\n");
		exit(EXIT_FAILURE);
	}

  // create client
	nrm_client_create(&client, upstream_uri, pub_port, rpc_port);
	assert(client != NULL);
	nrm_init(NULL, NULL);
	nrm_log_debug("NRM context initialized.\n");

  // create scope
	scope = nrm_scope_create();
	nrm_scope_threadshared(scope);
	nrm_log_debug("NRM scope initialized.\n");

  // create sensor
  char *name[] = "perf-wrap"
  sensor = nrm_sensor_create(name)

  //client add scope, sensor
  assert(nrm_client_add_scope(&client, &scope) == 0);
  assert(nrm_client_add_sensor(&client, &sensor) == 0);

	// initialize PAPI
	int papi_retval;
	papi_retval = PAPI_library_init(PAPI_VER_CURRENT);

	if (papi_retval != PAPI_VER_CURRENT) {
		nrm_log_error("PAPI library init error: %s\n",
		      PAPI_strnrm_log_error(papi_retval));
		exit(EXIT_FAILURE);
	}

	nrm_log_debug("PAPI initialized.\n");

	/* setup PAPI interface */
	int EventCode, EventSet = PAPI_NULL;

	err = PAPI_event_name_to_code(EventCodeStr, &EventCode);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI event_name translation error: %s\n",
		      PAPI_strnrm_log_error(err));
	}
	err = PAPI_create_eventset(&EventSet);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI eventset creation error: %s\n", PAPI_strnrm_log_error(err));
	}

	err = PAPI_add_event(EventSet, EventCode);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI eventset append error: %s\n", PAPI_strnrm_log_error(err));
	}

	nrm_log_debug("PAPI code string %s converted to PAPI code %i, and registered.\n",
	        EventCodeStr, EventCode);

	/* launch? command, sample counters */
	unsigned long long counter;

	int pid = fork();
	if (pid < 0) {
		nrm_log_error("perfwrapper fork error\n");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		/* child, needs to exec the cmd */
		err = execvp(argv[optind], &argv[optind]);
		nrm_log_error("error executing command: %s\n", strnrm_log_error(errno));
		exit(EXIT_FAILURE);
	}

	/* us, need to sample counters */
	err = PAPI_attach(EventSet, pid);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI eventset attach error: %s\n", PAPI_strnrm_log_error(err));
	}
	nrm_log_debug("PAPI attached to process with pid %i\n", pid);

	err = PAPI_start(EventSet);
	if (err != PAPI_OK) {
		nrm_log_error("PAPI start error: %s\n", PAPI_strnrm_log_error(err));
	}
	nrm_log_debug("PAPI started. Initializing event read/send to NRM\n");

	do {

		/* sleep for a frequency */
		double sleeptime = 1 / freq;
		struct timespec req, rem;
		req.tv_sec = ceil(sleeptime);
		req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;
		/* deal with signal interrupts */
		do {
			err = nanosleep(&req, &rem);
			req = rem;
		} while (err == -1 && errno == EINTR);

		/* sample and report */
		err = PAPI_read(EventSet, &counter);
		if (err != PAPI_OK) {
			nrm_log_error("PAPI event read error: %s\n",
			      PAPI_strnrm_log_error(err));
			exit(EXIT_FAILURE);
		}

    nrm_time_t time;
    nrm_time_gettime(&time);

    nrm_client_send_event(&client, time, &sensor, &scope, counter);

		/* loop until child exits */
		int status;
		err = waitpid(pid, &status, WNOHANG);
		if (err == -1) {
			nrm_log_error("error during wait: %s\n", strnrm_log_error(errno));
			exit(EXIT_FAILURE);
		}
	} while (err != pid);
	nrm_log_debug("Finalizing PAPI-event read/send to NRM.\n");

	/* final send here */
	PAPI_stop(EventSet, &counter);
	nrm_client_send_event(&client, time, &sensor, &scope, counter);

	nrm_log_debug("Finalizing NRM context. Exiting.\n");
	/* finalize program */
	nrm_client_destroy(client);
  nrm_finalize();
	exit(EXIT_SUCCESS);
}
