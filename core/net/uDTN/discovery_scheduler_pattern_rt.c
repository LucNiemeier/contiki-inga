/**
 * \addtogroup discovery
 * @{
 */

/**
 * \defgroup discovery_scheduler_pattern Pattern discovery scheduler module
 *
 * @{
 */

/**
 * \file
 * \brief Pattern discovery scheduler module, real-time
 *
 * \author André Frambach <frambach@ibr.cs.tu-bs.de>
 */
#include "logging.h"
#include "discovery_scheduler.h"
#include "discovery_scheduler_events.h"
#include "discovery.h"

#include "net/mac/discovery_aware_rdc.h"
#include "sys/rtimer.h"

/**
 * Discovery Timeslot Length in Seconds
 */
#ifdef DTN_DISCO_CONF_TIMESLOT_LENGTH
#define DTN_DISCO_TIMESLOT_LENGTH DTN_DISCO_CONF_TIMESLOT_LENGTH
#else
#define DTN_DISCO_TIMESLOT_LENGTH 1
#endif


/**
 * Discovery Pattern. Default is 1sec on, 1sec off.
 */
#ifdef DTN_DISCO_PATTERN_CONF
#define DTN_DISCO_PATTERN DTN_DISCO_PATTERN_CONF
#else
#define DTN_DISCO_PATTERN {1,1}
#endif


static struct rtimer dst;
static uint8_t schedule_index = 0;


void discovery_scheduler_pattern_rt_func(struct rtimer *t, void *ptr);

void discovery_scheduler_pattern_rt_init() {
  printf("discovery_scheduler_rt\n");
	rtimer_set(&dst, RTIMER_NOW() + DTN_DISCO_TIMESLOT_LENGTH * RTIMER_ARCH_SECOND, 1, discovery_scheduler_pattern_rt_func, NULL);
}

void discovery_scheduler_pattern_rt_func(struct rtimer *t, void *ptr)
{
	static const uint8_t schedule[] = DTN_DISCO_PATTERN;

	static const uint8_t schedule_length = sizeof(schedule) / sizeof(schedule[0]);

	/* schedule always starts with discovery on. state is switched at beginning
	 * of loop, therefore set to zero, here */
	static uint8_t sched_state = 0;

	/* Count timeslots */
	static uint8_t counter = 0;


	/* Rescheudle ourself */
	rtimer_set(t, RTIMER_TIME(t) + DTN_DISCO_TIMESLOT_LENGTH * RTIMER_ARCH_SECOND, 1, discovery_scheduler_pattern_rt_func, ptr);

	if (counter == 0) {
	  /* calculate new timeout */
	  clock_time_t newTimeout = schedule[schedule_index] * DTN_DISCO_TIMESLOT_LENGTH * RTIMER_ARCH_SECOND;

	  counter = schedule[schedule_index++];

	  /* wrap schedule if appropriate */
	  schedule_index = schedule_index % schedule_length;

	  /* switch discovery state */
	  sched_state = ~sched_state;

	  if (sched_state) {
	    LOG(LOGD_DTN, LOG_DISCOVERY_SCHEDULER, LOGL_DBG, "PATTERN DISCOVERY SCHEDULER: begin of discovery phase");
	    process_post(&discovery_aware_rdc_process, dtn_disco_start_event, 0);
	    DISCOVERY.start(newTimeout, schedule_index);
	  } else {
	    LOG(LOGD_DTN, LOG_DISCOVERY_SCHEDULER, LOGL_DBG, "PATTERN DISCOVERY SCHEDULER: end of discovery phase");
	    DISCOVERY.stop();
	    process_post(&discovery_aware_rdc_process, dtn_disco_stop_event, 0);
	  }
	}

	counter--;
}

void discovery_scheduler_pattern_rt_set_schedule_index(uint8_t index) {
  if (index != schedule_index) {
    LOG(LOGD_DTN, LOG_DISCOVERY_SCHEDULER, LOGL_DBG, "Schedule index WAS: %u      IS: %u", schedule_index, index);
    schedule_index = index;
  }
}

const struct discovery_scheduler_driver discovery_scheduler_pattern_rt = {
		.init = discovery_scheduler_pattern_rt_init,
		.set_schedule_index = discovery_scheduler_pattern_rt_set_schedule_index,
};

/** @} */
/** @} */
