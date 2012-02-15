/*
 * Copyright (c) 2006, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 * $Id: hello-world.c,v 1.1 2006/10/02 21:46:46 adamdunkels Exp $
 */

/**
 * \file
 *         A very simple Contiki application showing how Contiki programs look
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contiki.h"

#include "net/netstack.h"
#include "net/packetbuf.h"

#include "net/uDTN/bundle.h"
#include "net/uDTN/agent.h"
#include "net/uDTN/API_events.h"
#include "net/uDTN/API_registration.h"

//#include "net/dtn/realloc.h"

#include "net/uDTN/dtn_config.h"
#include "net/uDTN/storage.h"
#include "mmem.h"
#include "sys/test.h"
#include "sys/profiling.h"
#include "watchdog.h"

/*---------------------------------------------------------------------------*/
PROCESS(hello_world_process, "Hello world process");
AUTOSTART_PROCESSES(&hello_world_process);
static struct registration_api reg;
struct bundle_t bundle;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(hello_world_process, ev, data)
{
	uint8_t i;
	static struct etimer timer;
	static uint16_t bundles_sent = 0;
	static uint32_t time_start, time_stop;
	uint8_t userdata[80];
	uint32_t tmp;

	PROCESS_BEGIN();
	profiling_init();
	profiling_start();
	agent_init();
	reg.status=1;
	reg.application_process=&hello_world_process;
	reg.app_id=25;
	printf("MAIN: event= %u\n",dtn_application_registration_event);
	printf("main app_id %lu process %p\n", reg.app_id, &agent_process);
	etimer_set(&timer,  CLOCK_SECOND*5);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
	process_post(&agent_process, dtn_application_registration_event,&reg);

	/* Profile initialization separately */
	profiling_stop();
	watchdog_stop();
	profiling_report("init", 0);
	watchdog_start();
	etimer_set(&timer,  CLOCK_SECOND/10);
	printf("Init done, starting test\n");

	profiling_init();
	profiling_start();

	time_start = clock_seconds();
	while(1) {

		PROCESS_WAIT_UNTIL(etimer_expired(&timer) ||
				ev == submit_data_to_application_event);

		/* We received a bundle - check if it is the sink telling us to
		 * stop sending */
		if (ev == submit_data_to_application_event) {
			struct bundle_t *bun;

			bun = (struct bundle_t *) data;
			delete_bundle(bun);

			profiling_stop();
			watchdog_stop();
			profiling_report("send-1000", 0);
			watchdog_start();
			TEST_REPORT("throughput", 1000, time_stop-time_start, "bundles/s");
			TEST_PASS();
			PROCESS_EXIT();
		}

		/* Check for timeout */
		if (clock_seconds()-time_start > 600) {
			profiling_stop();
			watchdog_stop();
			profiling_report("timeout", 0);
			watchdog_start();
			TEST_FAIL("Didn't receive ack from sink");
			PROCESS_EXIT();
		}

		/* Stop profiling if we've sent 1000 bundles. We still need to send
		 * more since some might have been lost on the way */
		if (bundles_sent == 1000) {
			profiling_stop();
			time_stop = clock_seconds();
		}

		create_bundle(&bundle);

		/* Source and destination */
		tmp=0x0001;
		set_attr(&bundle, DEST_NODE, &tmp);
		tmp=25;
		set_attr(&bundle, DEST_SERV, &tmp);
		tmp=dtn_node_id;
		set_attr(&bundle, SRC_NODE, &tmp);
		set_attr(&bundle, SRC_SERV,&tmp);
		set_attr(&bundle, CUST_NODE, &tmp);
		set_attr(&bundle, CUST_SERV, &tmp);

		tmp=0;
		set_attr(&bundle, FLAGS, &tmp);
		tmp=1;
		set_attr(&bundle, REP_NODE, &tmp);
		set_attr(&bundle, REP_SERV, &tmp);

		/* Set the sequence number to the number of budles sent */
		tmp = bundles_sent;
		set_attr(&bundle, TIME_STAMP_SEQ_NR, &tmp);

		tmp=2000;
		set_attr(&bundle, LIFE_TIME, &tmp);
		tmp=4;
		set_attr(&bundle, TIME_STAMP, &tmp);

		/* Add the payload */
		for(i=0; i<80; i++)
			userdata[i] = i;
		add_block(&bundle, 1, 2, userdata, 80);

		process_post(&agent_process, dtn_send_bundle_event, (void *) &bundle);

		bundles_sent++;
		/* Show progress every 50 bundles */
		if (bundles_sent%50 == 0)
			printf("%i\n", bundles_sent);

		etimer_set(&timer, CLOCK_SECOND/10);
	}
	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
