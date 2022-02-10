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

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

static int log_level = 0;

char *usage =
        "usage: perfwrapper [options] -e [papi event] [command]\n"
        "     options:\n"
        "            -e, --event             PAPI preset event name. Default: PAPI_TOT_INS\n"
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
        error("Error during conversion to double: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
      }
      break;
    case 'e':
      strcpy(EventCodeStr, optarg);
      break;
    case 'h':
      normal("%s", usage);
      break;
    case '?':
    default:
      error("Wrong option argument\n");
      error("%s", usage);
      exit(EXIT_FAILURE);
    }
  }

  verbose("verbose=%d; freq=%f; event=%s\n", log_level, freq,
          EventCodeStr);

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
  int papi_retval;
  papi_retval = PAPI_library_init(PAPI_VER_CURRENT);

  if (papi_retval != PAPI_VER_CURRENT) {
    error("PAPI library init error\n");
    exit(EXIT_FAILURE);
  }
  verbose("PAPI v%f initialized.\n", papi_retval);

  /* setup PAPI interface */
  int EventCode, EventSet = PAPI_NULL;
  PAPI_event_info_t info;

  err = PAPI_event_name_to_code(EventCodeStr, &EventCode);
  assert(err == PAPI_OK);

  err = PAPI_create_eventset(&EventSet);
  assert(err == PAPI_OK);

  err = PAPI_add_event(EventSet, EventCode);
  assert(err == PAPI_OK);
  verbose("%s translated as PAPI code %i, registered to PAPI.\n", EventCodeStr, EventCode);

  /* launch? command, sample counters */
  unsigned long long counter;

  int pid = fork();
  if (pid < 0) {
    error("perfwrapper fork error\n");
    exit(EXIT_FAILURE);
  } else if (pid == 0) {
    /* child, needs to exec the cmd */
    err = execvp(argv[optind], &argv[optind]);
    error("error executing command: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  /* us, need to sample counters */
  PAPI_attach(EventSet, pid);
  verbose("PAPI attached to process with pid %i", pid);
  PAPI_start(EventSet);
  verbose("Initializing PAPI-event read/send to NRM.\n");
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
    PAPI_read(EventSet, &counter);
    nrm_send_progress(ctxt, counter, scope);

    /* loop until child exits */
    int status;
    err = waitpid(pid, &status, WNOHANG);
    if (err == -1) {
      error("error during wait: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
  } while (err != pid);
  verbose("Finalizing PAPI-event read/send to NRM.\n");

  /* final send here */
  PAPI_stop(EventSet, &counter);
  nrm_send_progress(ctxt, counter, scope);

  verbose("Finalizing NRM context.\n");
  /* finalize program */
  nrm_fini(ctxt);
  nrm_scope_delete(scope);
  nrm_ctxt_delete(ctxt);
  exit(EXIT_SUCCESS);
}
