/**
 * \addtogroup bprocess
 * @{
 */

/**
 * \file
 *        
 */
 
#include "cfs.h"
#include "cfs-coffee.h"

#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "timer.h"
#include "net/netstack.h"
#include "mmem.h"
#include "net/rime/rimeaddr.h"

#include "API_registration.h"
#include "registration.h"
#include "API_events.h"
#include "bundle.h"
#include "agent.h"
#include "storage.h"
#include "sdnv.h"
#include "redundance.h"
#include "dispatching.h"
#include "routing.h"
#include "dtn-network.h"
#include "node-id.h"
#include "custody.h"
#include "status-report.h"
#include "lib/memb.h"
#include "discovery.h"
#include "statistics.h"
#include "convergence_layer.h"

// #define ENABLE_LOGGING 1
#include "logging.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

static struct mmem * bundleptr;

uint32_t dtn_node_id;
uint32_t dtn_seq_nr;
PROCESS(agent_process, "AGENT process");
AUTOSTART_PROCESSES(&agent_process);

void agent_init(void) {
	// if the agent process is already running, to nothing
	if( process_is_running(&agent_process) ) {
		return;
	}

	// Otherwise start the agent process
	process_start(&agent_process, NULL);
}

/*  Bundle Protocol Prozess */
PROCESS_THREAD(agent_process, ev, data)
{
	PROCESS_BEGIN();
	
	dtn_node_id=node_id;
	dtn_seq_nr=0;
	
	mmem_init();
	BUNDLE_STORAGE.init();
	BUNDLE_STORAGE.reinit();
	ROUTING.init();
	REDUNDANCE.init();
	registration_init();
	convergence_layer_init();

	dtn_application_remove_event  = process_alloc_event();
	dtn_application_registration_event = process_alloc_event();
	dtn_application_status_event = process_alloc_event();
	dtn_receive_bundle_event = process_alloc_event();
	dtn_send_bundle_event = process_alloc_event();
	submit_data_to_application_event = process_alloc_event();
	dtn_beacon_event = process_alloc_event();
	dtn_send_admin_record_event = process_alloc_event();
	dtn_bundle_in_storage_event = process_alloc_event();
	dtn_send_bundle_to_node_event = process_alloc_event();
	dtn_processing_finished = process_alloc_event();
	dtn_bundle_stored = process_alloc_event();
	
	CUSTODY.init();
	DISCOVERY.init();
	PRINTF("starting DTN Bundle Protocol \n");
		
	struct registration_api *reg;
	
	while(1) {
		PROCESS_WAIT_EVENT_UNTIL(ev);

		if(ev == dtn_application_registration_event) {
			reg = (struct registration_api *) data;
			registration_new_app(reg->app_id, reg->application_process);
			PRINTF("BUNDLEPROTOCOL: Event empfangen, Registration, Name: %lu\n", reg->app_id);
			continue;
		}
					
		if(ev == dtn_application_status_event) {

			int status;
			reg = (struct registration_api *) data;
			PRINTF("BUNDLEPROTOCOL: Event empfangen, Switch Status, Status: %i \n", reg->status);
			if(reg->status == APP_ACTIVE)
				status = registration_set_active(reg->app_id);
			else if(reg->status == APP_PASSIVE)
				status = registration_set_passive(reg->app_id);
			
			if(status == -1) {
				PRINTF("BUNDLEPROTOCOL: no registration found to switch \n");
			}

			continue;
		}
		
		if(ev == dtn_application_remove_event) {
			reg = (struct registration_api *) data;
			PRINTF("BUNDLEPROTOCOL: Event empfangen, Remove, Name: %lu \n", reg->app_id);
			registration_remove_app(reg->app_id);
			continue;
		}
		
		if(ev == dtn_send_bundle_event) {
			uint32_t * bundle_number;
			uint8_t n = 0;
			struct bundle_t * bundle = NULL;
			struct process * source_process = NULL;

			bundleptr = (struct mmem *) data;
			if( bundleptr == NULL ) {
				LOG(LOGD_DTN, LOG_AGENT, LOGL_ERR, "dtn_send_bundle_event with invalid pointer");
				continue;
			}

			bundle = (struct bundle_t *) MMEM_PTR(bundleptr);
			if( bundle == NULL ) {
				LOG(LOGD_DTN, LOG_AGENT, LOGL_ERR, "dtn_send_bundle_event with invalid MMEM structure");
				continue;
			}

			LOG(LOGD_DTN, LOG_AGENT, LOGL_DBG, "dtn_send_bundle_event(%p) with seqNo %lu", bundleptr, dtn_seq_nr);

			// Set the outgoing sequence number
			set_attr(bundleptr, TIME_STAMP_SEQ_NR, &dtn_seq_nr);
			dtn_seq_nr++;

			// Copy the sending process, because 'bundle' will not be accessible anymore afterwards
			source_process = bundle->source_process;

			// Save the bundle in storage
			n = BUNDLE_STORAGE.save_bundle(bundleptr, &bundle_number, 0);

			/* Saving the bundle failed... */
			if( !n ) {
				/* Decrement the sequence number */
				dtn_seq_nr--;
			}

			// Reset our pointers to avoid using invalid ones
			bundle = NULL;
			bundleptr = NULL;

			// If a sender process exists, notify it
			if( source_process != NULL) {
				if( n ) {
					/* Bundle has been successfully saved, send event to service */
					process_post(source_process, dtn_bundle_stored, bundleptr);
				} else {
					/* Bundle could not be saved, notify service */
					process_post(source_process, dtn_bundle_store_failed, NULL);
				}
			}

			// Now emulate the event to our agent
			if( n ) {
				data = bundle_number;
				ev = dtn_bundle_in_storage_event;
			}
		}
		
		if(ev == dtn_send_admin_record_event) {
			PRINTF("BUNDLEPROTOCOL: send admin record \n");
			continue;
		}

		if(ev == dtn_beacon_event){
			rimeaddr_t* src =(rimeaddr_t*) data;
			ROUTING.new_neighbor(src);
			LOG(LOGD_DTN, LOG_AGENT, LOGL_DBG, "dtn_beacon_event for %u.%u", src->u8[0], src->u8[1]);
			continue;
		}

		if(ev == dtn_bundle_in_storage_event){
			uint32_t bundle_number = *(uint32_t *) data;

			LOG(LOGD_DTN, LOG_AGENT, LOGL_DBG, "bundle %lu in storage", bundle_number);

			if(ROUTING.new_bundle(bundle_number) < 0){
				LOG(LOGD_DTN, LOG_AGENT, LOGL_ERR, "routing reports error when announcing new bundle %lu", bundle_number);
				continue;
			}

			continue;
		}
		
	    if(ev == dtn_processing_finished) {
	    	// data should contain the bundlemem ptr
	    	struct mmem * bundlemem = (struct mmem *) data;

	    	// Notify routing, that service has finished processing a bundle
	    	ROUTING.locally_delivered(bundlemem);
	    }
	}
	PROCESS_END();
}

void agent_del_bundle(uint32_t bundle_number){
	convergence_layer_delete_bundle(bundle_number);
	ROUTING.del_bundle(bundle_number);
	CUSTODY.del_from_list(bundle_number);
}
/** @} */
