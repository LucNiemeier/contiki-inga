/*
 * Copyright (c) 2012, Daniel Willmann <daniel@totalueberwachung.de>
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
 */

/**
 * \file
 *         A uDTN ping-pong example testing end-to-end latency
 * \author
 *         Daniel Willmann <daniel@totalueberwachung.de>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contiki.h"

#include "net/netstack.h"
#include "net/packetbuf.h"

#include "net/uDTN/bundle.h"
#include "net/uDTN/agent.h"
#include "net/uDTN/sdnv.h"
#include "net/uDTN/API_events.h"
#include "net/uDTN/API_registration.h"

#include "net/uDTN/dtn_config.h"
#include "net/uDTN/storage.h"

/* Needed for profiling/testing */
#include "watchdog.h"
#include "sys/test.h"
#include "sys/profiling.h"

#define MODE_PASSIVE 0
#define MODE_ACTIVE 1
#define MODE_LOOPBACK 2

#ifndef CONF_MODE
#warning "CONF_MODE not defined, this node will operate in loopback mode"
#define CONF_MODE MODE_LOOPBACK
#endif

#if CONF_MODE == MODE_LOOPBACK
#undef CONF_DEST_NODE
#define CONF_DEST_NODE dtn_node_id
#endif

#ifndef CONF_DEST_NODE
#error "I need a destination node - set CONF_DEST_NODE"
#endif

#ifdef CONF_PAYLOAD_LEN
#if CONF_PAYLOAD_LEN < 4
#error "Payload must be at least 4 bytes big"
#endif
#define PAYLOAD_LEN CONF_PAYLOAD_LEN
#else
#define PAYLOAD_LEN 80
#endif

/*---------------------------------------------------------------------------*/
PROCESS(ping_process, "Ping");
PROCESS(pong_process, "Pong");
PROCESS(coordinator_process, "Coordinator");

AUTOSTART_PROCESSES(&coordinator_process);

static struct registration_api reg_ping;
static struct registration_api reg_pong;
struct bundle_t bundle;
/*---------------------------------------------------------------------------*/

static clock_time_t get_time()
{
	return clock_time();
}

/* Convenience function to populate a bundle */
static inline struct mmem *bundle_convenience(uint16_t dest, uint16_t dst_srv, uint16_t src_srv,  uint8_t *data, size_t len)
{
	uint32_t tmp;
	struct mmem *bundlemem;

	bundlemem = create_bundle();
	if (!bundlemem) {
		printf("create_bundle failed\n");
		return NULL;
	}

	/* Source and destination */
	tmp=dest;
	set_attr(bundlemem, DEST_NODE, &tmp);
	tmp=dst_srv;
	set_attr(bundlemem, DEST_SERV, &tmp);
	tmp=dtn_node_id;
	set_attr(bundlemem, SRC_NODE, &tmp);
	set_attr(bundlemem, CUST_NODE, &tmp);
	set_attr(bundlemem, CUST_SERV, &tmp);

	tmp=src_srv;
	set_attr(bundlemem, SRC_SERV,&tmp);

	tmp=0;
	set_attr(bundlemem, FLAGS, &tmp);
	tmp=1;
	set_attr(bundlemem, REP_NODE, &tmp);
	set_attr(bundlemem, REP_SERV, &tmp);

	tmp = 0;
	set_attr(bundlemem, TIME_STAMP_SEQ_NR, &tmp);

	tmp=2000;
	set_attr(bundlemem, LIFE_TIME, &tmp);
	tmp=4;
	set_attr(bundlemem, TIME_STAMP, &tmp);

	add_block(bundlemem, 1, 0, data, len);

	return bundlemem;
}

PROCESS_THREAD(coordinator_process, ev, data)
{
	static struct etimer timer;
	PROCESS_BEGIN();

	profiling_init();
	profiling_start();
	agent_init();

	printf("Starting tests\n");

#if CONF_MODE == MODE_ACTIVE
	process_start(&ping_process, NULL);
#elif CONF_MODE == MODE_PASSIVE
	process_start(&pong_process, NULL);
#else
	process_start(&ping_process, NULL);
	process_start(&pong_process, NULL);
#endif

	/* The pong process doesn't collect anything, so PASS device directly.
	 * Otherwise wait for ping to finish. */
	PROCESS_WAIT_UNTIL(!process_is_running(&ping_process));

	etimer_set(&timer, CLOCK_SECOND*10);
	PROCESS_WAIT_UNTIL(etimer_expired(&timer));

	profiling_stop();
	watchdog_stop();
	profiling_report("pingpong", 0);
	watchdog_start();
	TEST_PASS();

	PROCESS_END();
}

PROCESS_THREAD(ping_process, ev, data)
{
	static uint16_t timeouts = 0, bundle_sent = 0, bundle_recvd = 0;
	static uint32_t diff = 0, latency = 0;
	static uint8_t synced = 0;
	static struct etimer timer;
	struct mmem *bundlemem, *recv;
	struct bundle_block_t *block;
	uint8_t i;
	uint8_t userdata[PAYLOAD_LEN];
	uint32_t *u32_ptr;

	PROCESS_BEGIN();

	reg_ping.status=1;
	reg_ping.application_process=&ping_process;
	reg_ping.app_id=5;
	process_post(&agent_process, dtn_application_registration_event, &reg_ping);
	etimer_set(&timer,  CLOCK_SECOND);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

	printf("Init done, starting test\n");

	/* Transfer */
	while(1) {
		etimer_set(&timer, CLOCK_SECOND*5);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer) ||
				ev == submit_data_to_application_event);

		if (ev != submit_data_to_application_event) {
			u32_ptr = (uint32_t *)&userdata[0];
			/* Sync pattern */
			if (!synced) {
				printf("Timeout waiting for sync\n");
				*u32_ptr = 0xfdfdfdfd;
			} else {
				timeouts++;
				printf("Timeouts: %u\n", timeouts);
				*u32_ptr = get_time();
			}
			for (i=4;i<PAYLOAD_LEN;i++) {
				userdata[i] = i;
			}
			bundlemem = bundle_convenience(CONF_DEST_NODE, 7, 5, userdata, PAYLOAD_LEN);
			if (bundlemem)
				process_post(&agent_process, dtn_send_bundle_event, (void *) bundlemem);
			continue;
		}
		/* We received a bundle - handle it */
		recv = (struct mmem *) data;

		diff = get_time();

		/* Check receiver */
		block = get_block(recv, 0);
		u32_ptr = (uint32_t *)block->payload;

		if (!synced) {
			/* We're synced */
			if (*u32_ptr == 0xfdfdfdfd) {
				synced = 1;
			} else {
				delete_bundle(recv);
				continue;
			}
		} else {
			/* Calculate RTT */
			bundle_recvd++;
			diff -= *u32_ptr;
//			printf("Latency: %lu\n", diff);

			if (bundle_recvd % 50 == 0)
				printf("%u\n", bundle_recvd);

			latency += diff;
		}

		delete_bundle(recv);

		/* We're done */
		if (bundle_recvd >= 1000)
			break;

		/* Send PING */
		u32_ptr = (uint32_t *)&userdata[0];
		*u32_ptr = get_time();
		for (i=4;i<PAYLOAD_LEN;i++) {
			userdata[i] = i;
		}
		bundlemem = bundle_convenience(CONF_DEST_NODE, 7, 5, userdata, PAYLOAD_LEN);
		if (bundlemem) {
			process_post(&agent_process, dtn_send_bundle_event, (void *) bundlemem);
			bundle_sent++;
		}
	}

	TEST_REPORT("timeout", timeouts, bundle_sent, "lost/sent");
	TEST_REPORT("average latency", latency*1000/bundle_recvd, CLOCK_SECOND, "ms");

	PROCESS_END();
}
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(pong_process, ev, data)
{
	static struct etimer timer;
	static uint16_t bundle_sent = 0;
	struct mmem *bundlemem, *recv;
	struct bundle_block_t *block;
	uint32_t *u32_ptr, tmp;

	PROCESS_BEGIN();

	reg_pong.status=1;
	reg_pong.application_process=&pong_process;
	reg_pong.app_id=7;
	process_post(&agent_process, dtn_application_registration_event, &reg_pong);
	etimer_set(&timer,  CLOCK_SECOND);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

	printf("Init done, starting responder\n");

	/* Transfer */
	while(1) {
		PROCESS_WAIT_EVENT_UNTIL(ev == submit_data_to_application_event);

		/* We received a bundle - bounce it back */
		recv = (struct mmem *) data;

		get_attr(recv, SRC_NODE, &tmp);

		/* Check receiver */
		if (tmp != CONF_DEST_NODE) {
			printf("Bundle from different node.\n");
			delete_bundle(recv);
			continue;
		}

		block = get_block(recv, 0);
		u32_ptr = (uint32_t *)block->payload;

		/* Send PONG */
		bundlemem = bundle_convenience(CONF_DEST_NODE, 5, 7, (uint8_t *) u32_ptr, 4);
		if (bundlemem)
			process_post(&agent_process, dtn_send_bundle_event, (void *) bundlemem);

		delete_bundle(recv);

		bundle_sent++;
		if (bundle_sent % 50 == 0)
			printf("%u\n", bundle_sent);
	}
	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
