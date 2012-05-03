/**
 * \addtogroup bnet
 * @{
 */

/**
 * \file
 *
 * \author Georg von Zengen (vonzeng@ibr.cs.tu-bs.de) 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "dtn_config.h"
#include "dtn-network.h"
#include "net/netstack.h"
#include "net/packetbuf.h"
#include "net/rime/rimeaddr.h"
#include "bundle.h"
#include "agent.h"
#include "dispatching.h"
#include "routing.h"
#include "sdnv.h"
#include "mmem.h"
#include "discovery.h"
#include "stimer.h"
#include "leds.h"
#if CONTIKI_TARGET_AVR_RAVEN
	#include <stings.h>
#endif

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

const struct mac_driver *dtn_network_mac;

uint16_t *output_offset_ptr;

//static uint8_t bundle_seqno = 0;


static void packet_sent(void *ptr, int status, int num_tx);

static struct bundle_t bundle;	
static rimeaddr_t beacon_src;
//static struct stimer wait_timer;
static uint16_t last_send,cnt,cnt2;

static void dtn_network_init(void) 
{
	last_send= 0;
	cnt=0;
	packetbuf_clear();
//	input_buffer_clear();
	dtn_network_mac = &NETSTACK_MAC;
//	stimer_set(&wait_timer, 1);
	PRINTF("DTN init\n");
}


/**
*called for incomming packages
*/
#define SUFFIX_LENGTH	2
static void dtn_network_input(void) 
{
//	printf("DTN-NETWORK: got packet\n");
	uint8_t input_packet[114];
	int size = packetbuf_copyto(input_packet);
	rimeaddr_t dest = *packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
	rimeaddr_t bsrc = *packetbuf_addr(PACKETBUF_ADDR_SENDER);
	int16_t rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
	//printf("NET: rssi = %d\n", rssi-45);
	PRINTF("%x%x: dtn_network_input\n",dest.u8[0],dest.u8[1]);
	if((*input_packet==0x08) & (*(input_packet+1)==0x80)) { //broadcast message

		uint8_t * discovery_data = input_packet + 2;
		uint8_t discovery_length = (uint8_t) (size - 2 - SUFFIX_LENGTH);

		PRINTF("Broadcast\n");

		leds_on(LEDS_ALL);

		DISCOVERY.receive(&bsrc, discovery_data, discovery_length);
		packetbuf_clear();

		leds_off(LEDS_ALL);
	} else {
		uint8_t * payload_data = input_packet + 1;
		uint8_t payload_length = (uint8_t) (size - 1 - SUFFIX_LENGTH);

		leds_on(LEDS_GREEN);

		packetbuf_clear();
		PRINTF("%p  %p\n", &bundle, &payload_data);

		struct mmem mem;
		// FIXME: Wuerde es hier nicht reichen, payload_length zu allozieren?
		mmem_alloc(&mem, 114 - 1 - SUFFIX_LENGTH);
		if (!MMEM_PTR(&mem)){
			PRINTF("DTN: MMEM ERROR\n");
			leds_off(LEDS_GREEN);
			return;
		}

		memcpy(MMEM_PTR(&mem), payload_data, 114 - 1 - SUFFIX_LENGTH);
		memset(&bundle, 0, sizeof(struct bundle_t));
		if ( !recover_bundel(&bundle, &mem, payload_length)){
			PRINTF("DTN: recover ERROR\n");	
			mmem_free(&mem);
			leds_off(LEDS_GREEN);
			return;
		}
		mmem_free(&mem);

		if (bundle.flags&2){
			//printf("NET: %u\n",*((uint8_t *)bundle.mem.ptr + bundle.offset_tab[DATA][OFFSET]));
		}
		bundle.rec_time=(uint32_t) clock_seconds();
#if DEBUG_H
		bundle.debug_time=clock_time();
#endif
		bundle.size= payload_length;
#if DEBUG
		uint8_t i;
		printf("NETWORK: input ");
		for (i=0; i<bundle.size; i++){
			printf("%x:",*((uint8_t *)bundle.mem.ptr + i));
		}
		printf("\n");
#endif
		rimeaddr_copy(&bundle.msrc, &bsrc);

		DISCOVERY.alive(&bsrc);
		//printf("NETWORK: %u:%u\n", bundle.msrc.u8[0],bundle.msrc.u8[1]);
		PRINTF("NETWORK: size of received bundle: %u block pointer %p\n",bundle.size, bundle.mem.ptr);

		dispatch_bundle(&bundle);			

		leds_off(LEDS_GREEN);
	}
		
}


static void packet_sent(void *ptr, int status, int num_tx) 
{
	switch(status) {
	  case MAC_TX_COLLISION:
	    PRINTF("DTN: collision after %d tx\n", num_tx);
	    break;
	  case MAC_TX_NOACK:
	    PRINTF("DTN: noack after %d tx\n", num_tx);
	    break;
	  case MAC_TX_OK:
	    PRINTF("DTN: sent after %d tx\n", num_tx);
	    break;
	  case MAC_TX_DEFERRED:
	    PRINTF("DTN: error MAC_TX_DEFERRED after %d tx\n", num_tx);
	    break;
	  case MAC_TX_ERR:
	    PRINTF("DTN: error MAC_TX_ERR after %d tx\n", num_tx);
	    break;
	  case MAC_TX_ERR_FATAL:
	    PRINTF("DTN: error MAC_TX_ERR_FATAL after %d tx\n", num_tx);
	    break;
	  default:
	    PRINTF("DTN: error %d after %d tx\n", status, num_tx);
	  }
	last_send--;
	if (!last_send){
	}
	if(ptr){
		struct route_t *route= (struct route_t *)ptr;
		PRINTF("DTN: bundle_num : %u    %p\n",route->bundle_num,ptr);
		//printf("sent to %u:%u\n",route->dest.u8[0],route->dest.u8[1]);

		if( status == MAC_TX_OK ) {
			// Notify discovery that peer is still alive
			DISCOVERY.alive(&(route->dest));
		}
		ROUTING.sent(route,status,num_tx);
	}
		
}

int dtn_network_send(struct bundle_t *bundle, struct route_t *route) 
{
	leds_on(LEDS_YELLOW);

	uint8_t *payload = bundle->mem.ptr;
	uint8_t len = bundle->size;
	uint32_t i, time;
	last_send++;
	sdnv_decode(bundle->mem.ptr+bundle->offset_tab[TIME_STAMP_SEQ_NR][OFFSET],bundle->offset_tab[TIME_STAMP_SEQ_NR][STATE],&i);
	sdnv_decode(bundle->mem.ptr+bundle->offset_tab[LIFE_TIME][OFFSET],bundle->offset_tab[LIFE_TIME][STATE],&time);

	PRINTF("seq_num %lu lifetime %lu bundle pointer %p bundel->block %p \n ",i,time,bundle,bundle->mem.ptr);
#if DEBUG
	printf("NETWORK: send ");
	for (i=0; i<bundle->mem.size; i++){
		printf("%x:",*((uint8_t*)bundle->mem.ptr + i));
	}
	printf("\n");
#endif
	/* kopiere die Daten in den packetbuf(fer) */
	packetbuf_ext_copyfrom(payload, len,0x30,0);
	/*setze Zieladresse und �bergebe das Paket an die MAC schicht */
	packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &route->dest);
	packetbuf_set_attr(PACKETBUF_ADDRSIZE, 2);
	cnt++;
	//printf("send: %u  %u\n",cnt,i);
	NETSTACK_MAC.send(&packet_sent, route); 
//	while( clock_time()- last_trans < 80){
//		watchdog_periodic();
//	}
//	last_trans=clock_time();
	
	leds_off(LEDS_YELLOW);

	return 1;
}

int dtn_send_discover(uint8_t *payload,uint8_t len, rimeaddr_t *dst)
{
	packetbuf_ext_copyfrom(payload, len,0x08,0x80);
	packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, dst);
	packetbuf_set_attr(PACKETBUF_ADDRSIZE, 2);
	NETSTACK_MAC.send(NULL, NULL); 

	return 1;
}


const struct network_driver dtn_network_driver = 
{
  "DTN",
  dtn_network_init,
  dtn_network_input
};
