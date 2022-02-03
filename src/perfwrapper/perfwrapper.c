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
#include <string.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include "nrm.h"
#include "papi.h"

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
	char EventCodeStr[PAPI_MAX_STR_LEN] = "PAPI_TOT_INS";
  char EventDescr[PAPI_MAX_STR_LEN];
  char EventLabel[20];
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
			  strcpy(EventCodeStr, optarg);
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

	printf("verbose=%d; freq=%f; event=%s\n", verbose_flag, freq, EventCodeStr);

  if (optind >= argc) {
        fprintf(stderr, "Expected command after options.\n");
        exit(EXIT_FAILURE);
  }

  if (optind < argc) { // temp: parses rest of argv, cmd + options
    while (optind < argc)
      printf("%s ", argv[optind++]);
    printf("\n");
  }

	int rank = 0, cpu;
	cpu = sched_getcpu();

	/* initializes the ctxt for communication with NRM through libnrm */
	ctxt = nrm_ctxt_create();
	assert(ctxt != NULL);
	nrm_init(ctxt, "nrm-perfwrapper", rank, cpu);
	scope = nrm_scope_create();
	nrm_scope_threadshared(scope);

  // initialize PAPI
  int papi_retval;
  papi_retval = PAPI_library_init(PAPI_VER_CURRENT);

  if (papi_retval != PAPI_VER_CURRENT){
    fprintf(stderr, "PAPI library init error\n");
    exit(EXIT_FAILURE);
  }

	/* setup PAPI interface */
	int EventCode, EventSet = PAPI_NULL;
  PAPI_event_info_t info;

	err = PAPI_event_name_to_code(EventCodeStr, &EventCode);
	assert(err == PAPI_OK);

	err = PAPI_create_eventset(&EventSet);
  assert(err == PAPI_OK);

	err = PAPI_add_event(EventSet, EventCode);
  assert(err == PAPI_OK);

	/* launch? command, sample counters */
	unsigned long long counter;

	int pid = fork();
	if(pid < 0) {
    fprintf(stderr, "perfwrapper fork error\n");
		exit(EXIT_FAILURE);
	}
	else if(pid == 0) {
		/* TODO child, needs to exec the cmd */
	}
	else {
		/* us, need to sample counters */
		PAPI_attach(EventSet, pid);
		PAPI_start(EventSet);

		/* TODO: sleep for a frequency */

		/* sample and report */
		PAPI_read(EventSet, &counter);
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
