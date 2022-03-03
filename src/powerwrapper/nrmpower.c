/*******************************************************************************
 * Copyright 2021 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

/* Filename: nrmpower.c
 *
 * Description: Implements middleware between variorum and the NRM
 *              downstream interface.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <nrm.h>
#include <jansson.h>
#include <variorum.h>

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

static int log_level = 0;

char *usage =
        "usage: nrm-power [options] -m [variorium measurement] [command]\n"
        "     options:\n"
        "            -m, --measurement       Variorium power measurement name. Default: ...\n"
        "            -f, --frequency         Frequency in hz to poll. Default: 10.0\n"
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

int main(int argc, char **argv)
{
	int c, err;
	double freq = 10;
	char PowerLimit[24] = "_PACKAGE_POWER_LIMITS";
	char *cmd;

	while (1) {
		static struct option long_options[] = {
		        {"verbose", no_argument, &log_level, 1},
		        {"frequency", optional_argument, 0, 'f'},
		        {"help", no_argument, 0, 'h'},
		        {"measurement", required_argument, 0, 'm'},
		        {0, 0, 0, 0}};

		int option_index = 0;
		c = getopt_long(argc, argv, "+vf:m:h", long_options,
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
				error("Error during conversion to double: %s\n",
				      strerror(errno));
				exit(EXIT_FAILURE);
			}
			break;
		case 'm':
			strcpy(PowerLimit, optarg);
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

	verbose("verbose=%d; freq=%f; measurement=%s\n", log_level, freq,
	        PowerLimit);

	if (optind >= argc) {
		error("Expected command after options.\n");
		exit(EXIT_FAILURE);
	}

	ctxt = nrm_ctxt_create();
	assert(ctxt != NULL);
	nrm_init(ctxt, "nrm-perfwrapper", 0, 0);
	verbose("NRM context initialized.\n");

	scope = nrm_scope_create();
	nrm_scope_threadshared(scope);
	verbose("NRM scope initialized.\n");

	// initialize PAPI
	// int papi_retval;
	// papi_retval = PAPI_library_init(PAPI_VER_CURRENT);
  //
	// if (papi_retval != PAPI_VER_CURRENT) {
	// 	error("PAPI library init error: %s\n",
	// 	      PAPI_strerror(papi_retval));
	// 	exit(EXIT_FAILURE);
	// }
  //
	// verbose("PAPI initialized.\n");
  //
	// /* setup PAPI interface */
	// int EventCode, EventSet = PAPI_NULL;
  //
	// err = PAPI_event_name_to_code(EventCodeStr, &EventCode);
	// if (err != PAPI_OK) {
	// 	error("PAPI event_name translation error: %s\n",
	// 	      PAPI_strerror(err));
	// }
	// err = PAPI_create_eventset(&EventSet);
	// if (err != PAPI_OK) {
	// 	error("PAPI eventset creation error: %s\n", PAPI_strerror(err));
	// }
  //
	// err = PAPI_add_event(EventSet, EventCode);
	// if (err != PAPI_OK) {
	// 	error("PAPI eventset append error: %s\n", PAPI_strerror(err));
	// }
  //
	// verbose("PAPI code string %s converted to PAPI code %i, and registered.\n",
	//         EventCodeStr, EventCode);

	/* launch? command, sample counters */
	unsigned long long counter;

	// int pid = fork();
	// if (pid < 0) {
	// 	error("perfwrapper fork error\n");
	// 	exit(EXIT_FAILURE);
	// } else if (pid == 0) {
	// 	/* child, needs to exec the cmd */
	// 	err = execvp(argv[optind], &argv[optind]);
	// 	error("error executing command: %s\n", strerror(errno));
	// 	exit(EXIT_FAILURE);
	// }

	// /* us, need to sample counters */
	// err = PAPI_attach(EventSet, pid);
	// if (err != PAPI_OK) {
	// 	error("PAPI eventset attach error: %s\n", PAPI_strerror(err));
	// }
	// verbose("PAPI attached to process with pid %i\n", pid);
  //
	// err = PAPI_start(EventSet);
	// if (err != PAPI_OK) {
	// 	error("PAPI start error: %s\n", PAPI_strerror(err));
	// }
	// verbose("PAPI started. Initializing event read/send to NRM\n");

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

		// /* sample and report */
		// err = PAPI_read(EventSet, &counter);
		// if (err != PAPI_OK) {
		// 	error("PAPI event read error: %s\n",
		// 	      PAPI_strerror(err));
		// 	exit(EXIT_FAILURE);
		// }

    // err = variorum_monitoring(&counter);
    err = variorum_print_verbose_power_limit(void);
    assert(err == 0);

		// nrm_send_progress(ctxt, counter, scope);

		/* loop until child exits */

	} while (1);
	verbose("Finalizing PAPI-event read/send to NRM.\n");

	/* final send here */
	// PAPI_stop(EventSet, &counter);
	nrm_send_progress(ctxt, counter, scope);

	verbose("Finalizing NRM context. Exiting.\n");
	/* finalize program */
	nrm_fini(ctxt);
	nrm_scope_delete(scope);
	nrm_ctxt_delete(ctxt);
	exit(EXIT_SUCCESS);
}
