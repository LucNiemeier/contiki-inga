/**
 * \addtogroup bstorage
 * @{
 */

 /**
 * \defgroup r_storage RAM storage modules
 *
 * @{
 */

/**
 * \file 
 * \author Georg von Zengen (vonzeng@ibr.cs.tu-bs.de)
 */

#include "contiki.h"

#include "storage.h"
#include "bundle.h"
#include "sdnv.h"
#include "agent.h"
#include "lib/mmem.h"
#include "lib/list.h"
#include "r_storage.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "dtn_config.h"
#include "status-report.h"
#include "forwarding.h"
#include "profiling.h"
#include "statistics.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

// defined in mmem.c, no function to access it though
extern unsigned int avail_memory;

LIST(bundle_list);
MEMB(bundle_mem, struct file_list_entry_t, BUNDLE_STORAGE_SIZE);

static uint16_t bundles_in_storage;
static struct ctimer r_store_timer;
struct memb *saved_as_mem;
static struct bundle_t bundle_str;
static uint16_t bundle_number = 0;

void r_store_prune();

/**
 * \brief internal function to send statistics to statistics module
 */
void rs_update_statistics() {
	statistics_storage_bundles(bundles_in_storage);
	statistics_storage_memory(avail_memory);
}

/**
* /brief called by agent at startup
*/
void rs_init(void)
{
	PRINTF("STORAGE: init r_storage\n");

	// Initialize the neighbour list
	list_init(bundle_list);

	// Initialize the neighbour memory block
	memb_init(&bundle_mem);

	// Initialize MMEM for the binary bundle storage
	mmem_init();

	bundles_in_storage = 0;
	bundle_number = 0;

	MEMB(saved_as_memb, uint16_t, 50);
	saved_as_mem = &saved_as_memb;
	memb_init(saved_as_mem);

	rs_reinit();
	rs_update_statistics();

	ctimer_set(&r_store_timer, CLOCK_SECOND*5, r_store_prune, NULL);
}
/**
* \brief deletes expired bundles from storage
*/
void r_store_prune()
{
	struct file_list_entry_t * entry;

	// Delete expired bundles from storage
	for(entry = list_head(bundle_list);
			entry != NULL;
			entry = list_item_next(entry)) {
		uint32_t elapsed_time = clock_seconds() - entry->rec_time;

		if( entry->lifetime < elapsed_time ) {
			PRINTF("STORAGE: bundle lifetime expired of bundle %u\n", entry->bundle_num);
			rs_del_bundle(entry->bundle_num, REASON_LIFETIME_EXPIRED);
		}
	}

	ctimer_restart(&r_store_timer);
}


void rs_reinit(void)
{
	struct file_list_entry_t * entry;

	// Start counting the bundle number from zero again
	bundle_number = 0;

	// Delete all bundles from storage
	for(entry = list_head(bundle_list);
			entry != NULL;
			entry = entry->next) {
		rs_del_bundle(entry->bundle_num, REASON_NO_INFORMATION);
	}
}

/**
 * This function delete as many bundles from the storage as necessary to
 * have at least one slot and the number of required of memory free
 * besides the high watermark for MMEM
 */
uint8_t rs_make_room(struct bundle_t * bundle)
{
	if( bundles_in_storage < BUNDLE_STORAGE_SIZE && (avail_memory - bundle->size) > STORAGE_HIGH_WATERMARK ) {
		// We have enough memory, no need to do anything
		return 1;
	}

	// Now delete expired bundles
	r_store_prune();

	// Keep deleting bundles until we have enough MMEM and slots
	while( bundles_in_storage >= BUNDLE_STORAGE_SIZE || (avail_memory - bundle->size) < STORAGE_HIGH_WATERMARK ) {
		struct file_list_entry_t * entry = list_head(bundle_list);

		if( entry == NULL ) {
			// We do not have bundles in storage, stop deleting them
			break;
		}

		rs_del_bundle(entry->bundle_num, REASON_DEPLETED_STORAGE);
	}

	return 1;
}

/**
* \brief saves a bundle in storage
* \param bundle pointer to the bundle
* \return the bundle number given to the bundle or <0 on errors
*/
int32_t rs_save_bundle(struct bundle_t * bundle)
{
	uint8_t *tmp = bundle->mem.ptr;
	struct file_list_entry_t * entry;

	if( bundle->size == 0 ) {
		printf("STORAGE: Bundle not saved, size is 0\n");
		return -1;
	}

	uint32_t src;
	tmp=tmp+bundle->offset_tab[SRC_NODE][OFFSET];
	sdnv_decode(tmp ,bundle->offset_tab[SRC_NODE][STATE], &src);

	uint32_t dest;
	tmp=bundle->mem.ptr+bundle->offset_tab[DEST_NODE][OFFSET];
	sdnv_decode(tmp ,bundle->offset_tab[DEST_NODE][STATE], &dest);

	uint32_t time_stamp;
	tmp=bundle->mem.ptr+bundle->offset_tab[TIME_STAMP][OFFSET];
	sdnv_decode(tmp, bundle->offset_tab[TIME_STAMP][STATE], &time_stamp);

	uint32_t time_stamp_seq;
	tmp=bundle->mem.ptr+bundle->offset_tab[TIME_STAMP_SEQ_NR][OFFSET];
	sdnv_decode(tmp, bundle->offset_tab[TIME_STAMP_SEQ_NR][STATE], &time_stamp_seq);

	uint32_t fraq_offset;
	tmp=bundle->mem.ptr+bundle->offset_tab[FRAG_OFFSET][OFFSET];
	sdnv_decode(tmp, bundle->offset_tab[FRAG_OFFSET][STATE], &fraq_offset);

	// Look for duplicates in the storage
	for(entry = list_head(bundle_list);
		entry != NULL;
		entry = list_item_next(entry)) {
		if ( time_stamp_seq == entry->time_stamp_seq &&
		    time_stamp == entry->time_stamp &&
		    src == entry->src &&
		    fraq_offset == entry->fraq_offset) {

			PRINTF("STORAGE: %u is the same bundle\n", entry->bundle_num);
			return (int32_t) entry->bundle_num;
		}
	}

	if( !rs_make_room(bundle) ) {
		printf("STORAGE: Cannot store bundle, no room\n");
		return -1;
	}

	entry = memb_alloc(&bundle_mem);
	if( entry == NULL ) {
		printf("STORAGE: unable to allocate struct, cannot store bundle\n");
		return -1;
	}

	// Clear the memory area
	memset(entry, 0, sizeof(struct file_list_entry_t));

	// Allocate some memory
	int mem = mmem_alloc(&entry->ptr, bundle->size);
	if( !mem ) {
		printf("STORAGE: write of %u bytes failed\n", bundle->size);
		memb_free(&bundle_mem, entry);
		return -1;
	}

	bundles_in_storage++;

	// Set all required fields
	entry->bundle_num = bundle_number ++;
	entry->file_size = bundle->size;
	entry->time_stamp_seq = time_stamp_seq;
	entry->time_stamp = time_stamp;
	entry->src = src;
	entry->dest = dest;
	entry->fraq_offset = fraq_offset;
	entry->rec_time = bundle->rec_time;
	entry->lifetime = bundle->lifetime;
	rimeaddr_copy(&entry->msrc, &bundle->msrc);

	// Copy bundle into memory storage
	memcpy(entry->ptr.ptr, bundle->mem.ptr, bundle->size);

	PRINTF("STORAGE: New Bundle %u, Src %lu, Dest %lu, Seq %lu, Size %u\n", entry->bundle_num, src, dest, time_stamp_seq, entry->file_size);

	// Notify the statistics module
	rs_update_statistics();

	// Add bundle to the list
	list_add(bundle_list, entry);

	return (int32_t) entry->bundle_num;
}

/**
* \brief delets a bundle form storage
* \param bundle_num bundle number to be deleted
* \param reason reason code
* \return 1 on succes or 0 on error
*/
uint16_t rs_del_bundle(uint16_t bundle_num, uint8_t reason)
{
	struct file_list_entry_t * entry;

	PRINTF("STORAGE: Deleting Bundle %u with reason %u\n", bundle_num, reason);

	// Look for the bundle we are talking about
	for(entry = list_head(bundle_list);
			entry != NULL;
			entry = list_item_next(entry)) {
		if( entry->bundle_num == bundle_num ) {
			break;
		}
	}

	if( entry == NULL ) {
		PRINTF("STORAGE: Could not find bundle %u\n", bundle_num);
		return 0;
	}

	// Figure out the source to send status report
	if(rs_read_bundle(bundle_num, &bundle_str)){
		bundle_str.del_reason = reason;

		if( ((bundle_str.flags & 8 ) || (bundle_str.flags & 0x40000)) && (reason !=0xff )){
			if (entry->src != dtn_node_id){
				STATUS_REPORT.send(&bundle_str, 16, bundle_str.del_reason);
			}
		}
	}
	delete_bundle(&bundle_str);

	// Free the MMEM block inside the bundle
	if (MMEM_PTR(&entry->ptr) != 0){
		mmem_free(&entry->ptr);
	}

	// Remove the bundle from the list
	list_remove(bundle_list, entry);

	bundles_in_storage--;

	// Notified the agent, that a bundle has been deleted
	agent_del_bundle(bundle_num);

	// Notify the statistics module
	rs_update_statistics();

	// Free the storage struct
	memb_free(&bundle_mem, entry);

	return 1;
}

/**
* \brief reads a bundle from storage
* \param bundle_num bundle nuber to read
* \param bundle empty bundle struct, bundle will be accessable here
* \return 1 on succes or 0 on error
*/
uint16_t rs_read_bundle(uint16_t bundle_num,struct bundle_t *bundle)
{
	struct file_list_entry_t * entry;

	// Look for the bundle we are talking about
	for(entry = list_head(bundle_list);
			entry != NULL;
			entry = list_item_next(entry)) {
		if( entry->bundle_num == bundle_num ) {
			break;
		}
	}

	if( entry == NULL ) {
		printf("STORAGE: Could not find bundle %u\n", bundle_num);
		return 0;
	}

	if( entry->file_size == 0 ) {
		printf("STORAGE: Found bundle %u but file size is %u\n", bundle_num, entry->file_size);
		return 0;
	}

	if(MMEM_PTR(&entry->ptr) == NULL ) {
		printf("STORAGE: bundle contains no MMEM ptr\n");
		return 0;
	}

	recover_bundel(bundle, &entry->ptr, (int) entry->file_size);

	bundle->rec_time = entry->rec_time;
	bundle->custody = entry->custody;
	rimeaddr_copy(&bundle->msrc, &entry->msrc);

	return entry->file_size;
}


/**
* \brief checks if there is space for a bundle
* \param bundle pointer to a bundel struct (not used here)
* \return number of free solts
*/
uint16_t rs_free_space(struct bundle_t *bundle)
{
	return BUNDLE_STORAGE_SIZE - bundles_in_storage;
}

/**
* \returns the number of saved bundles
*/
uint16_t rs_get_g_bundel_num(void){
	return bundles_in_storage;
}

/**
 * \returns pointer to first bundle list entry
 */
struct storage_entry_t * rs_get_bundles(void)
{
	return (struct storage_entry_t *) list_head(bundle_list);
}


const struct storage_driver r_storage = {
	"R_STORAGE",
	rs_init,
	rs_reinit,
	rs_save_bundle,
	rs_del_bundle,
	rs_read_bundle,
	rs_free_space,
	rs_get_g_bundel_num,
	rs_get_bundles,
};
