/**
 * \addtogroup bprocess
 * @{
 */

/**
 * \file
 *
 */
 
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "custody-signal.h"

#include "lib/list.h"
#include "bundle.h"
#include "API_events.h"
#include "registration.h"
#include "dtn_config.h"
#include "status-report.h"
#include "sdnv.h"
#include "process.h"
#include "agent.h"
#include "custody.h"
#include "redundance.h"
#include "dev/leds.h"
#include "statistics.h"
#define ENABLE_LOGGING
#include "logging.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


void deliver_bundle(struct mmem *bundlemem, struct registration *n) {
	struct bundle_t *bundle = MMEM_PTR(bundlemem);

	if(n->status == APP_ACTIVE) {
		LOG(LOGD_DTN, LOG_BUNDLE, LOGL_DBG, "DELIVERY: Service is active");

		if (!REDUNDANCE.check(bundlemem)) { //Bundle was not delivered before
			REDUNDANCE.set(bundlemem);
			statistics_bundle_delivered(1);
			process_post(n->application_process, submit_data_to_application_event, bundlemem);
			if (bundle->flags & BUNDLE_FLAG_CUST_REQ) {
				CUSTODY.report(bundlemem,128);
			}
		} else {
			delete_bundle(bundlemem);
		}
	}

	#if DEBUG_H
	uint16_t time = clock_time();
	time -= bundle->debug_time;
	PRINTF("DELIVERY: time needed to process bundle for Delivery: %i \n", time);
	#endif
}
/** @} */
