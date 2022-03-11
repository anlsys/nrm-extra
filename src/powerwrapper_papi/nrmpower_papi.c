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
#include <papi.h>

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

static int log_level = 0;

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

int main(int argc, char **argv)
{
  int c, err;
  int num_events=0;
  double freq = 10;
  long long *values;
  const PAPI_component_info_t *cmpinfo = NULL;
  PAPI_event_info_t evinfo;
  char event_names[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN];
  char event_descrs[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN];
  char units[MAX_powercap_EVENTS][PAPI_MIN_STR_LEN];
  int data_type[MAX_powercap_EVENTS];

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

  verbose("verbose=%d; freq=%f; measurement=%s\n", log_level, freq);

  ctxt = nrm_ctxt_create();
  assert(ctxt != NULL);
  nrm_init(ctxt, "nrm-power", 0, 0);
  verbose("NRM context initialized.\n");

  scope = nrm_scope_create();
  nrm_scope_threadshared(scope);
  verbose("NRM scope initialized.\n");

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
  for( cid=0; cid<numcmp; cid++ ) {
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
  papi_retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, powercap_cid);
  while (papi_retval == PAPI_OK) {
      err = PAPI_event_code_to_name(code, event_names[num_events]);
      if (err != PAPI_OK) {
        error("PAPI translation error: %s\n", PAPI_strerror(err));
        exit(EXIT_FAILURE);
      }

      err = PAPI_get_event_info( code,&evinfo );
      if (err != PAPI_OK){
        error("PAPI event info obtain error: %s\n", PAPI_strerror(err));
        exit(EXIT_FAILURE);
      }

      strncpy(event_descrs[num_events],evinfo.long_descr,sizeof(event_descrs[0])-1);
      strncpy( units[num_events],evinfo.units,sizeof(units[0])-1);
      // buffer must be null terminated to safely use strstr operation on it below
      units[num_events][sizeof( units[0] )-1] = '\0';
      data_type[num_events] = evinfo.data_type;
      err = PAPI_add_event( EventSet, code );

      if (err != PAPI_OK)
          break; /* We've hit an event limit */
      num_events++;

      papi_retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, powercap_cid);
  }

  // temporary printing of detected papi info
  verbose("detected PAPI events:\n")
  int length = sizeof(event_names) / sizeof(event_names[0]);
  int i;
  for (i=0; i<length; i++){
    verbose("%s\n", event_names[i]);
  }

  verbose("detected PAPI descriptions:\n")
  int length = sizeof(event_descrs) / sizeof(event_descrs[0]);
  for (i=0; i<length; i++){
    verbose("%s\n", event_descrs[i]);
  }

  verbose("detected PAPI units:\n")
  int length = sizeof(units) / sizeof(units[0]);
  for (i=0; i<length; i++){
    verbose("%s\n", units[i]);
  }

  /* launch? command, sample counters */
  unsigned long long counter;
  long long before_time,after_time;
  double elapsed_time;

  values=calloc(num_events,sizeof(long long));
  if (values==NULL){
      error("No memory?!\n");
      exit(EXIT_FAILURE);
  }

  do {

    err = PAPI_start(EventSet);
    if (err != PAPI_OK) {
      error("PAPI start error: %s\n", PAPI_strerror(err));
      exit(EXIT_FAILURE);
    }

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
    err = PAPI_stop(EventSet, values);
    if (err != PAPI_OK ){
      error("PAPI stop error: %s\n", PAPI_strerror(err));
      exit(EXIT_FAILURE);
    }

    elapsed_time=( ( double )( after_time-before_time ) )/1.0e9;

    // calculate total watts across values, set to counter. but for now...
    // from powercap_basic.c as usual, in PAPI
    printf("took %.3fs\n", elapsed_time);
    printf( "scaled energy measurements:\n" );
    for( i=0; i<num_events; i++ ) {
        if ( strstr( event_names[i],"ENERGY_UJ" ) ) {
            if ( data_type[i] == PAPI_DATATYPE_UINT64 ) {
                printf( "%-45s%-20s%4.6f J (Average Power %.1fW)\n",
                        event_names[i], event_descrs[i],
                        ( double )values[i]/1.0e6,
                        ( ( double )values[i]/1.0e6 )/elapsed_time );
            }
        }
    }

    nrm_send_progress(ctxt, counter, scope);

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
