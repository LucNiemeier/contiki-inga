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

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


void deliver_bundle(struct bundle_t *bundle, struct registration *n) {

	PRINTF("DELIVERY\n");
	if(n->status == APP_ACTIVE) {  
	PRINTF("DELIVERY: Service is active\n");

	

		uint32_t len;
		uint8_t *block = bundle->mem.ptr + bundle->offset_tab[DATA][OFFSET];
#if DEBUG
		uint8_t block_count=0;
#endif
		if( !REDUNDANCE.check(bundle)){ //packet was not delivert befor
			REDUNDANCE.set(bundle);
			process_post(n->application_process, submit_data_to_application_event, bundle);
			block = bundle->mem.ptr+1;
			if (*block & 0x08){
				CUSTODY.report(bundle,128);
			}
		}else{
			delete_bundle(bundle);
		}
	}			
	
	#if DEBUG_H
	uint16_t time = clock_time();
	time -= bundle->debug_time;
	PRINTF("DELIVERY: time needed to process bundle for Delivery: %i \n", time);
	#endif
}
/** @} */