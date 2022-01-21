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
  * Description:    Implements middleware between papi and the NRM
  *                 downstream interface.
  */

#define _GNU_SOURCE
#include <assert.h>
#include <sched.h>
#include <stdio.h> // printf
#include <stdlib.h> // exit, atoi
#include <time.h>
#include <unistd.h> // getopt
#include <getopt.h> // getopt_long

#include <nrm.h>
#include <papi.h>

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

int log_level = 1;
char *usage =
  "usage: perfwrapper [options] -e [papi event]\n"
  "   options:\n"
  "      -e, --events        PAPI preset event names. Default: PAPI_TOT_INS\n"
  "      -f, --frequency     Frequency in hz to poll. Default: 10\n"
  "      -v, --verbose       Produce verbose output. Log messages will be displayed to stderr\n"
  "      -h, --help          Displays this help message\n";

int logging(FILE *stream, const char *fmt, int level, va_list ap)
{
	if (level < log_level) {
	 	return vfprintf(stream, fmt, ap);
	}
	else
		return 0;
}

int main(int argc, char **argv)
{
	int c, err, verb = 0;
	double freq = 10;
	char *event = "PAPI_TOT_INS";

	/* TODO getopt_long is better */

	while (1) {

		static struct option long_options[] = {
			{"verbose",   optional_argument, 0, 'v'},
			{"frequency", optional_argument, 0, 'f'},
			{"events",    required_argument, 0, 'e'},
			{"help",      optional_argument, 0, 'h'},
			{0,           0,                 0,  0}
		};

		int option_index = 0;
		c = getopt_long(argc, argv, "v:f:e:h:", long_options, &option_index);

		if (c == -1) break;
		switch (c) {
			case 'v':
			  verb = 1;
			  break;
			case 'f':
			  freq = atoi(optarg);
			  break;
			case 'e':
			  event = optarg;
        break;
      case 'h':
        fprintf(stderr, "%s", usage);
        break;
      default:
        fprintf(stderr, "%s", usage);
        break;
		}
	}
	printf("verbose=%d; freq=%f; event=%s\n", verb, freq, event);

	// double frequency = 10;
	// char *event_name = "PAPI_TOT_INS";
	char *cmd = "ls -al";
	int rank = 0, cpu;
	cpu = sched_getcpu();

	/* initializes the ctxt for communication with NRM through libnrm */
	ctxt = nrm_ctxt_create();
	assert(ctxt != NULL);
	nrm_init(ctxt, "nrm-perfwrapper", rank, cpu);
	scope = nrm_scope_create();
	nrm_scope_threadshared(scope);

	/* setup PAPI interface */
	int eventcode, eventset;
	err = PAPI_event_name_to_code(event_name, &eventcode);
	assert(err == PAPI_OK);

	PAPI_create_eventset(&eventset);
	PAPI_add_event(eventset, eventcode);

	/* launch? command, sample counters */
	unsigned long long counter;

	int pid = fork();
	if(pid < 0) {
		/* error */
	}
	else if(pid == 0) {
		/* TODO child, needs to exec the cmd */
	}
	else {
		/* us, need to sample counters */
		PAPI_attach(eventset, pid);
		PAPI_start(eventset);

		/* TODO: sleep for a frequency */

		/* sample and report */
		PAPI_read(eventset, &counter);
		nrm_send_progress(ctxt, counter, scope);

		/* TODO loop until child exits */
	}

	/* final send here */

	/* finalize program */
	nrm_fini(ctxt);
	nrm_scope_delete(scope);
	nrm_ctxt_delete(ctxt);
	exit(EXIT_SUCCESS);
}
