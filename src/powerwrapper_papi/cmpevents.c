#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <papi.h>

#define MAX_energy_uj_EVENTS 16

int main(int argc, char **argv)
{
  int i, cmp_id, numcmp;
  int powercap_cmp_id = -1;
  int EventSet=PAPI_NULL;
  const PAPI_component_info_t *cmpinfo = NULL;

  assert(PAPI_library_init(PAPI_VER_CURRENT) == PAPI_VER_CURRENT);

  // detecting component associated with powercap
  numcmp = PAPI_num_components();
  for(cmp_id=0; cmp_id<numcmp; cmp_id++) {

    cmpinfo = PAPI_get_component_info(cmp_id);
    assert(cmpinfo != NULL);

    if (strstr(cmpinfo->name,"powercap")) {
      powercap_cmp_id=cmp_id;
      printf("PAPI found powercap component at cmp_id %d\n", powercap_cmp_id);
      assert(!cmpinfo->disabled);
      break;
    }
  }

  assert(cmp_id != numcmp);
  assert(cmpinfo->num_cntrs != 0);
  assert(PAPI_create_eventset(&EventSet) == PAPI_OK);

  int EventCode = PAPI_NATIVE_MASK;
  int papi_retval;
  int num_cmp_events=0;
  int num_uj_events=0;
  char EventName[PAPI_MAX_STR_LEN];

  papi_retval = PAPI_enum_cmp_event(&EventCode, PAPI_ENUM_FIRST, powercap_cmp_id);
  printf("first code: %d\n\n", EventCode);

  // Iterate through compatible events, add "ENERGY_UJ" events to EventSet
  while (papi_retval == PAPI_OK) {

      assert(PAPI_event_code_to_name(EventCode, EventName) == PAPI_OK);
      printf("code: %d, event: %s\n", EventCode, EventName);

      if (strstr(EventName, "ENERGY_UJ")){
        printf("UJ code: %d, event: %s\n", EventCode, EventName);
        // assert(PAPI_add_named_event(EventSet, EventName) == PAPI_OK);
        assert(PAPI_query_named_event(EventName) == PAPI_OK);
        assert(PAPI_add_event(EventSet, EventCode) == PAPI_OK);
        num_uj_events++;
      }

      papi_retval = PAPI_enum_cmp_event(&EventCode, PAPI_ENUM_EVENTS, powercap_cmp_id);
  }

  printf("\nNUM UJ EVENTS: %d\n", num_uj_events);
  printf("\nEventSet listed PAPI event codes:\n");

  int energy_codes[MAX_energy_uj_EVENTS];
  int num_listed_events = num_uj_events;

  // Check EventSet: list Event codes into energy_codes, then print
  assert(PAPI_list_events(EventSet, energy_codes, &num_listed_events) == PAPI_OK);
  for (i=0; i<num_listed_events; i++){
    printf("%d\n", energy_codes[i]);
  }

  printf("\nEventSet listed PAPI event names:\n");

  // Check EventSet: Translate above codes into names, then print
  char energy_uj_event_names[MAX_energy_uj_EVENTS][PAPI_MAX_STR_LEN];
  for (i=0; i<num_listed_events; i++){
    assert(PAPI_event_code_to_name(energy_codes[i], energy_uj_event_names[i]) == PAPI_OK);
    printf("%s\n", energy_uj_event_names[i]);
  }

  int err;
  double sleeptime=1;
  long long before_time, after_time;
  long long *event_values;
  double elapsed_time;

  event_values = calloc(num_uj_events, sizeof(long long));

  // Start EventSet, sleep, Stop EventSet (and read into event_values), then print results
  do {

    before_time=PAPI_get_real_nsec();

    assert(PAPI_start(EventSet) == PAPI_OK);

    struct timespec req, rem;
    req.tv_sec = ceil(sleeptime);
    req.tv_nsec = sleeptime * 1e9 - ceil(sleeptime) * 1e9;

    do {
      err = nanosleep(&req, &rem);
      req = rem;
    } while (err == -1 && errno == EINTR);

    after_time=PAPI_get_real_nsec();
    elapsed_time=((double)(after_time-before_time))/1.0e9;

    assert(PAPI_stop(EventSet, event_values) == PAPI_OK);

    printf("\nscaled energy measurements:\n");

    for(i=0; i<num_uj_events; i++) {
        printf("%-45s%4.6f J (Average Power %.1fW)\n",
                energy_uj_event_names[i],
                (double)event_values[i]/1.0e6,
                ((double)event_values[i]/1.0e6)/elapsed_time);
    }
  } while (1);
}
