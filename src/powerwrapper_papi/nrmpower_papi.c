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

#define MAX_powercap_EVENTS 64

// handler for interrupt?
void interrupt(int signum) {
  verbose("Interrupt caught. Exiting loop.\n");
  stop = 1;
}

int main(int argc, char **argv)
{
  int c, err;
  int num_cmp_events=0;
  int num_uj_events=0;
  double freq = 1;
  const PAPI_component_info_t *cmpinfo = NULL;
  PAPI_event_info_t evinfo;
  char all_event_names[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN];

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

  code = PAPI_NATIVE_MASK;

  // Get compatible events for component
  papi_retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, powercap_cid);

  while (papi_retval == PAPI_OK) {

      // Translate all compatible events to strings
      err = PAPI_event_code_to_name(code, all_event_names[num_cmp_events]);
      if (err != PAPI_OK) {
        error("PAPI translation error: %s\n", PAPI_strerror(err));
        exit(EXIT_FAILURE);
      }

      if (strstr(all_event_names[num_cmp_events], "ENERGY_UJ")){
        num_uj_events++;
      }

      num_cmp_events++;

      papi_retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, powercap_cid);
  }

  // temporary printing of detected papi info
  verbose("detected PAPI events:\n");
  int i;
  for (i=0; i<num_cmp_events; i++){
    verbose("%s\n", all_event_names[i]);
  }

  verbose("NUM UJ EVENTS: %d\n", num_uj_events);

  // Iterate through event_names, if ENERGY_UJ, then copy to new array, add to EventSet
  int uj_index=0;
  int energy_uj_event_codes[num_uj_events];
  char energy_uj_event_names[num_uj_events][PAPI_MAX_STR_LEN];
  char energy_uj_event_descrs[num_uj_events][PAPI_MAX_STR_LEN];
  int energy_uj_datatypes[num_uj_events];
  int EventCode = PAPI_NULL;
  char EventName[PAPI_MAX_STR_LEN];

  for (i=0; i<num_cmp_events; i++){
    if (strstr(all_event_names[i], "ENERGY_UJ")){
      memcpy(energy_uj_event_names[uj_index], all_event_names[i], PAPI_MAX_STR_LEN);

      err = PAPI_event_name_to_code(all_event_names[i], &EventCode);
      if (err != PAPI_OK) {
        error("PAPI translation error: %s\n", PAPI_strerror(err));
        exit(EXIT_FAILURE);
      }

      err = PAPI_get_event_info(EventCode ,&evinfo);
      if (err != PAPI_OK){
        error("PAPI event info obtain error: %s\n", PAPI_strerror(err));
        exit(EXIT_FAILURE);
      }

      strncpy(energy_uj_event_descrs[uj_index],evinfo.long_descr,sizeof(energy_uj_event_descrs[0])-1);
      energy_uj_datatypes[uj_index] = evinfo.data_type;

      uj_index++;

      err = PAPI_add_event(EventSet, EventCode);
      if (err != PAPI_OK) {
        error("PAPI EventSet append error: %s\n", PAPI_strerror(err));
        exit(EXIT_FAILURE);
      }
    }
  }

  verbose("detected ENERGY_UJ events:\n");
  for (i=0; i<num_uj_events; i++){
    verbose("%s\n", energy_uj_event_names[i]);
  }

  verbose("detected ENERGY_UJ descriptions:\n");
  for (i=0; i<num_uj_events; i++){
    verbose("%s\n", energy_uj_event_descrs[i]);
  }

  verbose("detected ENERGY_UJ units:\n");
  for (i=0; i<num_uj_events; i++){
    verbose("%d\n", energy_uj_datatypes[i]);
  }

  verbose("checking PAPI EventSet once again:\n");
  verbose("EventSet listed PAPI event codes:\n");

  int number = 0;
  err = PAPI_list_events(EventSet, energy_uj_event_codes, &number);
  for (i=0; i<number; i++){
    verbose("%d\n", energy_uj_event_codes[i]);
  }

  verbose("EventSet listed PAPI event names:\n");

  for (i=0; i<num_uj_events; i++){
    err = PAPI_event_code_to_name(energy_uj_event_codes[i], EventName);
    verbose("%s %d\n", EventName);
  }

  nrm_scope_t *nrm_scopes[num_uj_events];

  // Create an NRM scope for each PAPI event with ENERGY_UJ in its name
  for (i=0; i<num_uj_events; i++){
    scope = nrm_scope_create();
    nrm_scope_threadshared(scope);
    nrm_scopes[i] = scope;
  }
  verbose("NRM scopes initialized.\n");

  /* launch? command, sample counters */
  long long before_time, after_time;
  long long event_values[num_uj_events];
  double elapsed_time, watts_value;

  err = PAPI_start(EventSet);
  if (err != PAPI_OK) {
    error("PAPI start error: %s\n", PAPI_strerror(err));
    exit(EXIT_FAILURE);
  }
  verbose("PAPI started.\n");

  // loop until ctrl+c interrupt?
  stop = 0;
  do {

    before_time=PAPI_get_real_nsec();

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
    err = PAPI_read(EventSet, event_values);
    if (err != PAPI_OK ){
      error("PAPI stop error: %s\n", PAPI_strerror(err));
      exit(EXIT_FAILURE);
    }
    verbose("PAPI stopped and read EventSet into event_values.\n");

    verbose( "scaled energy measurements:\n" );

    for(i=0; i<num_uj_events; i++) {
        verbose("%-45s%-20s%4.6f J (Average Power %.1fW)\n",
                energy_uj_event_names[i], energy_uj_event_descrs[i],
                (double)event_values[i]/1.0e6,
                ((double)event_values[i]/1.0e6)/elapsed_time);
    }

    // for each event, send progress using matching scope
    for(i=0; i<num_uj_events; i++){
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

  } while (!stop);

  PAPI_stop(EventSet, event_values);

  /* final send here */
  for(i=0; i<num_uj_events; i++){
    watts_value = ((double)event_values[i]/1.0e6)/elapsed_time;
    nrm_send_progress(ctxt, watts_value, nrm_scopes[i]);
  }
  verbose("Finalized PAPI-event read/send to NRM.\n");

  /* finalize program */
  nrm_fini(ctxt);
  verbose("Finalized NRM context.\n");

  for(i=0; i<num_uj_events; i++){
    nrm_scope_delete(nrm_scopes[i]);
  }
  verbose("NRM scopes deleted.\n");

  nrm_ctxt_delete(ctxt);
  verbose("NRM context deleted. Exiting.\n");

  exit(EXIT_SUCCESS);
}
