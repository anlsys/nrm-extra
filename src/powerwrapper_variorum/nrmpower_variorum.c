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

#define MAX_MEASUREMENTS 16

// handler for interrupt?
void interrupt(int signum)
{
  verbose("Interrupt caught. Exiting loop.\n");
  stop = 1;
}

int main(int argc, char **argv)
{
  int char_opt, err;
  double freq = 1;
  json_t *json_measurements = json_object();

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

  // 1st measure, only to determine viable measurements
  // without delta, watts values should all be zero
  // but since we're not reporting yet, that's ok
  assert(variorum_get_node_power_json(json_measurements) == 0);

  nrm_scope_t *nrm_scopes[MAX_MEASUREMENTS];
  int i=0, nrm_tot_scopes=0, nrm_type=-1;
  const char *key;
  json_t *value;

  json_object_foreach(json_measurements, key, value){
    // variorum inits un-measureable as -1.0, measureable as 0.0
    if (strstr(key, "socket") && (json_real_value(value) != -1.0)){
      scope = nrm_scope_create();
      nrm_scope_threadshared(scope);

      if (strstr(key, "power_cpu_watts")){
        nrm_type = NRM_SCOPE_TYPE_CPU;
      } else if (strstr(key, "power_mem_watts")){
        nrm_type = NRM_SCOPE_TYPE_NUMA;
      } else if (strstr(key, "power_gpu_watts")){
        nrm_type = NRM_SCOPE_TYPE_GPU;
      }

      nrm_scope_add(scope, nrm_type, i);
      nrm_scopes[i] = scope;
      i++;
    }
  }
  nrm_tot_scopes = i;

  verbose("Variorum socket measurements detected.\n");
  verbose("NRM scopes initialized.\n");

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

    assert(variorum_get_node_power_json(json_measurements) == 0);

    i=0;
    json_object_foreach(json_measurements, key, value){
      // should match each nrm scope created earlier
      if (strstr(key, "socket") && (json_real_value(value) != -1.0)){
        nrm_send_progress(ctxt, json_real_value(value), nrm_scopes[i]);
        i++;
      }
    }

    // Some verbose output just to look at numbers
    if (log_level >= 1){
      char *json_soutput = json_dumps(json_measurements, JSON_INDENT(4));
      verbose("%s\n", json_soutput);
      free(json_soutput);
    }
  } while (!stop);

  /* final send here */
  /* finalize program */
  nrm_fini(ctxt);
  verbose("Finalized NRM context.\n");

  for (i = 0; i < nrm_tot_scopes; i++) {
    nrm_scope_delete(nrm_scopes[i]);
  }
  verbose("NRM scopes deleted.\n");

  nrm_ctxt_delete(ctxt);
  verbose("NRM context deleted. Exiting.\n");

  exit(EXIT_SUCCESS);
}
