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
 *               and the NRM downstream interface.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <getopt.h>
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
#include <variorum.h>
#include <jansson.h>

static int log_level = 0;
volatile sig_atomic_t stop;

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

char *usage =
        "usage: nrm-power [options] \n"
        "     options:\n"
        "            -f, --frequency         Frequency in hz to poll. Default: 1.0\n"
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

#define MAX_MEASUREMENTS 64

// handler for interrupt?
void interrupt(int signum)
{
	verbose("Interrupt caught. Exiting loop.\n");
	stop = 1;
}

int main(int argc, char **argv)
{
	int i, char_opt, err;
	double freq = 1;

	// register callback handler for interrupt
	signal(SIGINT, interrupt);

	ctxt = nrm_ctxt_create();
	assert(ctxt != NULL);
	nrm_init(ctxt, "nrm-power", 0, 0);
	verbose("NRM context initialized.\n");

	// TODO: fix "-v" not being parsed as verbose
	while (1) {
		static struct option long_options[] = {
		        {"verbose", no_argument, &log_level, 1},
		        {"frequency", optional_argument, 0, 'f'},
		        {"help", no_argument, 0, 'h'},
		        {0, 0, 0, 0}};

		int option_index = 0;
		char_opt = getopt_long(argc, argv, "+vf:m:h", long_options,
		                       &option_index);

		if (char_opt == -1)
			break;
		switch (char_opt) {
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


	nrm_scope_t *nrm_scopes[MAX_MEASUREMENTS];

	for (i = 0; i < MAX_MEASUREMENTS; i++) {
		scope = nrm_scope_create();
		nrm_scope_threadshared(scope);
		nrm_scopes[i] = scope;
	}
	verbose("NRM scopes initialized.\n");

	long long *event_values;

	event_values = calloc(MAX_MEASUREMENTS, sizeof(long long));

	// loop until ctrl+c interrupt?
	stop = 0;
	double sleeptime = 1 / freq;

	do {

		/* sleep for a frequency */
		struct timespec req, rem;
		req.tv_sec = ceil(sleeptime);
		req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;

		do {
			err = nanosleep(&req, &rem);
			req = rem;
		} while (err == -1 && errno == EINTR);

    assert(variorum_print_power() == 0);

		verbose("scaled energy measurements:\n");
	} while (!stop);

	/* final send here */

	/* finalize program */
	nrm_fini(ctxt);
	verbose("Finalized NRM context.\n");

	for (i = 0; i < num_events; i++) {
		nrm_scope_delete(nrm_scopes[i]);
	}
	verbose("NRM scopes deleted.\n");

	nrm_ctxt_delete(ctxt);
	verbose("NRM context deleted. Exiting.\n");

	exit(EXIT_SUCCESS);
}
