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
#include <signal.h>

#include <nrm.h>
#include <papi.h>

static int log_level = 0;

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

volatile sig_atomic_t stop;

char *usage =
        "usage: nrm-power [options] \n"
        "     options:\n"
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

#define MAX_powercap_EVENTS 16

// handler for interrupt?
void interrupt(int signum) {
  verbose("Interrupt caught. Exiting loop.\n");
  stop = 1;
}

int main(int argc, char **argv)
{
  int c, err;
  int num_matching_events=0;
  double freq = 1;
  const PAPI_component_info_t *cmpinfo = NULL;
  PAPI_event_info_t evinfo;
  char event_names[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN];
  char event_descrs[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN];
  char units[MAX_powercap_EVENTS][PAPI_MIN_STR_LEN];
  int data_type[MAX_powercap_EVENTS];

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

  verbose("verbose=%d; freq=%f;\n", log_level, freq);

  // initialize PAPI
  int papi_retval;
  papi_retval = PAPI_library_init(PAPI_VER_CURRENT);

  if (papi_retval != PAPI_VER_CURRENT) {
    error("PAPI library init error: %s\n",
          PAPI_strerror(papi_retval));
    exit(EXIT_FAILURE);
  }
  verbose("PAPI initialized.\n");

  /* setup PAPI interface */
  int cid, powercap_cid=-1, numcmp, code;
  int EventSet = PAPI_NULL;

  /* detecting PAPI components, cmp associated with powercap */
  numcmp = PAPI_num_components();
  for(cid=0; cid<numcmp; cid++) {
    if ((cmpinfo = PAPI_get_component_info(cid)) == NULL){
      error("PAPI component identification failed: %s\n");
      exit(EXIT_FAILURE);
    }

    if (strstr(cmpinfo->name,"powercap")) {
      powercap_cid=cid;
      verbose("PAPI found powercap component at cid %d\n", powercap_cid);
      if(cmpinfo->disabled) {
        error("powercap component disabled: %s\n",
                cmpinfo->disabled_reason);
        exit(EXIT_FAILURE);
      }
      break;
    }
  }

  if (cid == numcmp){
    error("PAPI could not find powercap component\n");
    exit(EXIT_FAILURE);
  }

  if (cmpinfo->num_cntrs==0){
    error("powercap component has no counters\n");
    exit(EXIT_FAILURE);
  }

  err = PAPI_create_eventset(&EventSet);
  if (err != PAPI_OK) {
    error("PAPI eventset creation error: %s\n", PAPI_strerror(err));
    exit(EXIT_FAILURE);
  }

  verbose("PAPI EventSet created\n");

  char EventCodeStr[PAPI_MAX_STR_LEN];
  code = PAPI_NATIVE_MASK;

  // Get compatible events for component
  papi_retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, powercap_cid);

  while (papi_retval == PAPI_OK) {

      // Translate all compatible events to strings
      err = PAPI_event_code_to_name(code, EventCodeStr);
      if (err != PAPI_OK) {
        error("PAPI translation error: %s\n", PAPI_strerror(err));
        exit(EXIT_FAILURE);
      }

      // if ENERGY_UJ in event name, append to event_names, get info
      // if (strstr(EventCodeStr,"ENERGY_UJ")) {

      err = PAPI_get_event_info(code,&evinfo);
      if (err != PAPI_OK){
        error("PAPI event info obtain error: %s\n", PAPI_strerror(err));
        exit(EXIT_FAILURE);
      }

      strncpy(event_names[num_matching_events], EventCodeStr, sizeof(event_names[0])-1);
      strncpy(event_descrs[num_matching_events],evinfo.long_descr,sizeof(event_descrs[0])-1);
      strncpy(units[num_matching_events],evinfo.units,sizeof(units[0])-1);
      // buffer must be null terminated to safely use strstr operation on it below
      units[num_matching_events][sizeof(units[0] )-1] = '\0';
      data_type[num_matching_events] = evinfo.data_type;
      err = PAPI_add_event(EventSet,code);
      num_matching_events++;

      if (err != PAPI_OK)
          break; /* We've hit an event limit */
      // }
      papi_retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, powercap_cid);
  }

  // temporary printing of detected papi info
  verbose("detected PAPI events:\n");
  int i;
  for (i=0; i<num_matching_events; i++){
    verbose("%s\n", event_names[i]);
  }

  // all blank for powercap on chimera for some reason
  verbose("detected PAPI descriptions:\n");
  for (i=0; i<num_matching_events; i++){
    verbose("%s\n", event_descrs[i]);
  }

  // also all blank for powercap on chimera for some reason
  verbose("detected PAPI units:\n");
  for (i=0; i<num_matching_events; i++){
    verbose("%s\n", units[i]);
  }

  nrm_scope_t *nrm_scopes[num_matching_events];

  // Create an NRM scope for each chosen PAPI event
  for (i=0; i<num_matching_events; i++){
    scope = nrm_scope_create();
    nrm_scope_threadshared(scope);
    nrm_scopes[i] = scope;
  }
  verbose("NRM scopes initialized.\n");

  /* launch? command, sample counters */
  long long before_time, after_time, *event_values;
  double elapsed_time, watts_value;

  // loop until ctrl+c interrupt?
  stop = 0;
  do {

    // allocate "event_values" memory space...
    event_values=calloc(num_matching_events,sizeof(long long));
    if (event_values==NULL){
        error("No memory?!\n");
        exit(EXIT_FAILURE);
    }
    verbose("event_values memory allocated.\n");

    before_time=PAPI_get_real_nsec();

    err = PAPI_start(EventSet);
    if (err != PAPI_OK) {
      error("PAPI start error: %s\n", PAPI_strerror(err));
      exit(EXIT_FAILURE);
    }
    verbose("PAPI started.\n");

    /* sleep for a frequency */
    double sleeptime = 1 / freq;
    struct timespec req, rem;
    req.tv_sec = ceil(sleeptime);
    req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;

    do {
      err = nanosleep(&req, &rem);
      req = rem;
    } while (err == -1 && errno == EINTR);

    after_time=PAPI_get_real_nsec();

    elapsed_time=((double)(after_time-before_time))/1.0e9;

    // Stop and read EventSet measurements into "event_values"...
    err = PAPI_stop(EventSet, event_values);
    if (err != PAPI_OK ){
      error("PAPI stop error: %s\n", PAPI_strerror(err));
      exit(EXIT_FAILURE);
    }

    verbose("PAPI stopped and read EventSet into event_values.\n");

    // from powercap_basic.c as usual, in PAPI
    // print "event_values"
    verbose("took %.3fs\n", elapsed_time);
    verbose( "scaled energy measurements:\n" );
    for(i=0; i<num_matching_events; i++) {
      if ( strstr(event_names[i],"ENERGY_UJ")) {
        if (data_type[i] == PAPI_DATATYPE_UINT64) {
          verbose("%-45s%-20s%4.6f J (Average Power %.1fW)\n",
                  event_names[i], event_descrs[i],
                  (double)event_values[i]/1.0e6,
                  ((double)event_values[i]/1.0e6)/elapsed_time);
        }
      }
    }

    // for each event, send progress using matching scope
    for(i=0; i<num_matching_events; i++){
      watts_value = ((double)event_values[i]/1.0e6)/elapsed_time;
      nrm_send_progress(ctxt, watts_value, nrm_scopes[i]);
    }
    verbose("NRM progress sent.\n");

    // reset EventSet measurements?...
    err = PAPI_reset(EventSet);
    if (err != PAPI_OK ){
      error("PAPI reset error: %s\n", PAPI_strerror(err));
      exit(EXIT_FAILURE);
    }
    verbose("PAPI reset.\n");

    // presumably free "event_values" to reset it...?
    free(event_values);

  } while (!stop);

  /* final send here */
  for(i=0; i<num_matching_events; i++){
    watts_value = ((double)event_values[i]/1.0e6)/elapsed_time;
    nrm_send_progress(ctxt, watts_value, nrm_scopes[i]);
  }
  verbose("Finalized PAPI-event read/send to NRM.\n");

  /* finalize program */
  nrm_fini(ctxt);
  verbose("Finalized NRM context.\n");

  for(i=0; i<num_matching_events; i++){
    nrm_scope_delete(nrm_scopes[i]);
  }
  verbose("NRM scopes deleted.\n");

  nrm_ctxt_delete(ctxt);
  verbose("NRM context deleted. Exiting.\n");

  exit(EXIT_SUCCESS);
}
