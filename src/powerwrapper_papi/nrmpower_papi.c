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
#include <papi.h>
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

#define MAX_powercap_EVENTS 64

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

	assert(PAPI_library_init(PAPI_VER_CURRENT) == PAPI_VER_CURRENT);
	verbose("PAPI initialized.\n");

	/* Prepare to detect powercap PAPI component */
	int component_id, powercap_component_id = -1, num_components;
	const PAPI_component_info_t *component_info = NULL;

	/* Detect powercap component by iterating through all components */
	num_components = PAPI_num_components();
	for (component_id = 0; component_id < num_components; component_id++) {
		if ((component_info = PAPI_get_component_info(component_id)) ==
		    NULL) {
			error("PAPI component identification failed: %s\n");
			exit(EXIT_FAILURE);
		}

		if (strstr(component_info->name, "powercap")) {
			powercap_component_id = component_id;
			verbose("PAPI found powercap component at component_id %d\n",
			        powercap_component_id);
			assert(!component_info->disabled);
			break;
		}
	}

	int EventSet = PAPI_NULL;
	assert(component_id != num_components); // Matching component ID not
	                                        // found
	assert(component_info->num_cntrs != 0); // Component has no hardware
	                                        // counters
	assert(PAPI_create_eventset(&EventSet) == PAPI_OK);
	verbose("PAPI EventSet created\n");

	int papi_retval, num_events = 0;
	int EventCode = PAPI_NATIVE_MASK;
	char EventNames[MAX_powercap_EVENTS][PAPI_MAX_STR_LEN]; // For substring
	                                                        // checking in
	                                                        // measurement
	                                                        // loop
	int DataTypes[MAX_powercap_EVENTS]; // For datatype checking in
	                                    // measurement loop
	PAPI_event_info_t EventInfo;

	papi_retval = PAPI_enum_cmp_event(&EventCode, PAPI_ENUM_FIRST,
	                                  powercap_component_id);
	while (papi_retval == PAPI_OK) {

		assert(PAPI_event_code_to_name(
		               EventCode, EventNames[num_events]) == PAPI_OK);
		verbose("code: %d, event: %s\n", EventCode,
		        EventNames[num_events]);
		assert(PAPI_get_event_info(EventCode, &EventInfo) == PAPI_OK);
		DataTypes[num_events] = EventInfo.data_type;
		assert(PAPI_add_event(EventSet, EventCode) == PAPI_OK);

		num_events++;
		papi_retval = PAPI_enum_cmp_event(&EventCode, PAPI_ENUM_EVENTS,
		                                  powercap_component_id);
	}

	nrm_scope_t *nrm_scopes[MAX_powercap_EVENTS];

	for (i = 0; i < num_events; i++) {
		scope = nrm_scope_create();
		nrm_scope_threadshared(scope);
		nrm_scopes[i] = scope;
	}
	verbose("NRM scopes initialized.\n");

	long long before_time, after_time;
	long long *event_values;
	double elapsed_time, watts_value;

	event_values = calloc(num_events, sizeof(long long));

	// loop until ctrl+c interrupt?
	stop = 0;
	double sleeptime = 1 / freq;

	do {

		before_time = PAPI_get_real_nsec();

		assert(PAPI_start(EventSet) == PAPI_OK);

		/* sleep for a frequency */
		struct timespec req, rem;
		req.tv_sec = ceil(sleeptime);
		req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;

		do {
			err = nanosleep(&req, &rem);
			req = rem;
		} while (err == -1 && errno == EINTR);

		after_time = PAPI_get_real_nsec();
		elapsed_time = ((double)(after_time - before_time)) / 1.0e9;

		// Stop and read EventSet measurements into "event_values"...
		assert(PAPI_stop(EventSet, event_values) == PAPI_OK);

		verbose("scaled energy measurements:\n");
		for (i = 0; i < num_events; i++) {
			if (strstr(EventNames[i], "ENERGY_UJ")) {
				if (DataTypes[i] == PAPI_DATATYPE_UINT64) {
					watts_value = ((double)event_values[i] /
					               1.0e6) /
					              elapsed_time;
					nrm_send_progress(ctxt, watts_value,
					                  nrm_scopes[i]);
					verbose("%-45s%4.6f J (Average Power %.1fW)\n",
					        EventNames[i],
					        (double)event_values[i] / 1.0e6,
					        watts_value);
				}
			}
		}
	} while (!stop);

	int papi_status;
	assert(PAPI_state(EventSet, &papi_status) == PAPI_OK);
	if (papi_status == PAPI_RUNNING) {
		assert(PAPI_stop(EventSet, event_values) == PAPI_OK);
	}

	/* final send here */
	for (i = 0; i < num_events; i++) {
		if (strstr(EventNames[i], "ENERGY_UJ")) {
			if (DataTypes[i] == PAPI_DATATYPE_UINT64) {
				watts_value =
				        ((double)event_values[i] / 1.0e6) /
				        elapsed_time;
				nrm_send_progress(ctxt, watts_value,
				                  nrm_scopes[i]);
			}
		}
	}
	verbose("Finalized PAPI-event read/send to NRM.\n");

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
