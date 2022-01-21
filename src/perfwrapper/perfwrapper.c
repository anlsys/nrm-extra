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
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include <nrm.h>
#include <papi.h>

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

int log_level = 1;
static int verbose_flag;

char *usage =
  "usage: perfwrapper [options] -e [papi event] [command]\n"
  "   options:\n"
  "      -e, --event         PAPI preset event name. Default: PAPI_TOT_INS\n"
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
	int c, err;
	double freq = 10;
	char *event = "PAPI_TOT_INS";
  char *cmd;

	while (1) {
		static struct option long_options[] = {
			{"verbose",   no_argument,       &verbose_flag, 1},
			{"frequency", optional_argument, 0, 'f'},
			{"help",      no_argument,       0, 'h'},
      {"event",     required_argument, 0, 'e'},
			{0, 0, 0, 0}
		};

		int option_index = 0;
		c = getopt_long(argc, argv, "+vf:e:h", long_options, &option_index);

		if (c == -1) break;
		switch (c) {
			case 0:
			  if (long_options[option_index].flag != 0)
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
      case '?':
        break;
      default:
        fprintf(stderr, "%s", usage);
        exit(EXIT_FAILURE);
		}
	}

	printf("verbose=%d; freq=%f; event=%s\n", verbose_flag, freq, event);

  if (optind >= argc) {
        fprintf(stderr, "Expected command after options.\n");
        exit(EXIT_FAILURE);
  }

  if (optind < argc) {
    while (optind < argc)
      printf("%s ", argv[optind++]);
    printf("\n");
  }

  // exit(EXIT_SUCCESS);

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
