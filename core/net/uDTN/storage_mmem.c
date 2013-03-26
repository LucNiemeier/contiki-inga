/**
 * \addtogroup bundle_storage
 * @{
 */

/**
 * \defgroup bundle_storage_mmem MMEM-based temporary Storage
 *
 * @{
 */

/**
 * \file 
 * \author Georg von Zengen <vonzeng@ibr.cs.tu-bs.de>
 * \author Daniel Willmann <daniel@totalueberwachung.de>
 * \author Wolf-Bastian Poettner <poettner@ibr.cs.tu-bs.de>
 * \author Julian Heinbokel <j.heinbokel@tu-bs.de>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "contiki.h"
#include "lib/mmem.h"
#include "lib/list.h"
#include "logging.h"

#include "bundle.h"
#include "sdnv.h"
#include "agent.h"
#include "statusreport.h"
#include "profiling.h"
#include "statistics.h"

#include "storage.h"
#include "cache.h"

//FIXME dummy index block
//static struct bundle_index_entry_t temp_index_array[BUNDLE_STORAGE_INDEX_ARRAY_ENTRYS] = { 0 };
//static uint16_t temp_index_array_collision_check[BUNDLE_STORAGE_INDEX_ARRAY_ENTRYS] = { 0 };

//      * index_newest_block
//      storage_cached_get_index_block()
static uint8_t last_index_entry = 0;
/** Last written block with index data, initialized with invalid value */  //FIXME this replaces temp_index_array
//static uint8_t last_index_block_address = CACHE_PARTITION_B_INDEX_INVALID_TAG;
static uint8_t index_block_num = 0;

// defined in mmem.c, no function to access it though
extern unsigned int avail_memory;

/**
 * Internal representation of a bundle
 *
 * The layout is quite fixed - the next pointer and the bundle_num have to go first because this struct
 * has to be compatible with the struct storage_entry_t in storage.h!
 */
struct bundle_list_entry_t {  //FIXME nicht durch bundle_index_entry_t ersetzen
	/** pointer to the next list element */
	struct bundle_list_entry_t * next;

	/** copy of the bundle number - necessary to have
	 * a static address that we can pass on as an
	 * argument to an event
	 */
	uint32_t bundle_num;  //FIXME das weg?

    /** Cache flags */
    uint16_t cache_flags; //FIXME Dirty, Use, Tag
                          // 14 Bit Tag = 16384 data blocks are addressable

	/** pointer to the actual bundle stored in MMEM */
	struct mmem *bundle;
};

// List and memory blocks for the bundles
LIST(bundle_list);
MEMB(bundle_mem, struct bundle_list_entry_t, BUNDLE_STORAGE_SIZE);

// global, internal variables
/** Counts the number of bundles in storage */
static uint16_t bundles_in_storage;

/** Is used to periodically traverse all bundles and delete those that are expired */
static struct ctimer r_store_timer;

/**
 * "Internal" functions
 */
void storage_mmem_prune();
uint8_t storage_mmem_flush(void);
uint8_t storage_mmem_delete_bundle_by_bundle_number(uint32_t bundle_number);
void storage_mmem_update_statistics();
uint8_t storage_mmem_create_index_block();
struct mmem *storage_mmem_get_index_block(uint8_t blocknr);
uint32_t storage_mmem_get_free_space();
/**
 * \brief internal function to send statistics to statistics module
 */
void storage_mmem_update_statistics() {
	statistics_storage_bundles(bundles_in_storage);
	statistics_storage_memory(avail_memory);
}

//FIXME verschieben nach cache.c ?
/**
 * \brief checks if cache_flags contain a index tag
 */
uint8_t is_index_tag(uint16_t cache_flags) {
    uint16_t cache_tag;
    cache_tag = cache_flags & CACHE_TAG_MASK;
    if(cache_tag == CACHE_PARTITION_B_INDEX_INVALID_TAG || ( cache_tag >= CACHE_PARTITION_B_INDEX_START && cache_tag <= CACHE_PARTITION_B_INDEX_END)){
        return 1;
    }
    return 0;
}
/**
 * \brief called by agent at startup
 */
uint8_t storage_mmem_init(void)
{
	LOG(LOGD_DTN, LOG_STORE, LOGL_INF, "storage_mmem init");
    printf("storage_mmem init\n");

	// Initialize the bundle list
	list_init(bundle_list);

	// Initialize the bundle memory block
	memb_init(&bundle_mem);

	// Initialize MMEM for the binary bundle storage
	mmem_init();

	bundles_in_storage = 0;

	//FIXME
	//STORAGE_PERSISTENT.delete_blocks(CACHE_PARTITION_B_INDEX_START, CACHE_PARTITION_B_INDEX_END);
	if (!storage_mmem_create_index_block()){
	    printf("storage_mmem init: storage_mmem_create_index_block failed");
	}

	storage_mmem_flush();  //FIXME das sollte hier später raus. clean != leer...
	storage_mmem_update_statistics();

	ctimer_set(&r_store_timer, CLOCK_SECOND*5, storage_mmem_prune, NULL);

	return 1;
}

/**
 * \brief deletes expired bundles from storage
 */
void storage_mmem_prune()
{
	uint32_t elapsed_time;
	struct bundle_list_entry_t * entry = NULL;
	struct bundle_t *bundle = NULL;

	// Delete expired bundles from storage
	for(entry = list_head(bundle_list);
			entry != NULL;
			entry = list_item_next(entry)) {

        if(is_index_tag(entry->cache_flags)){ //FIXME
            continue;
        }

		bundle = (struct bundle_t *) MMEM_PTR(entry->bundle);
        elapsed_time = clock_seconds() - bundle->rec_time;

        if( bundle->lifetime < elapsed_time ) {
            LOG(LOGD_DTN, LOG_STORE, LOGL_INF, "bundle lifetime expired of bundle %lu", entry->bundle_num);
            storage_mmem_delete_bundle_by_bundle_number(bundle->bundle_num);
        }
	}

	ctimer_restart(&r_store_timer);
}

/**
 * \brief deletes all stored bundles + index blocks
 */
uint8_t storage_mmem_flush(void)
{
    //FIXME testcode für STORAGE_PERSISTENT.delete_blocks
//    printf("STORAGE_PERSISTENT.delete_blocks(0,4095)\n");
//    STORAGE_PERSISTENT.delete_blocks(0,4095);

	struct bundle_list_entry_t * entry = NULL;
	struct bundle_t *bundle = NULL;

	// Delete all bundles from storage
	for(entry = list_head(bundle_list);
			entry != NULL;
			entry = list_item_next(entry)) {

        if(is_index_tag(entry->cache_flags)){ //FIXME
            continue;
        }

		bundle = (struct bundle_t *) MMEM_PTR(entry->bundle);

		storage_mmem_delete_bundle_by_bundle_number(bundle->bundle_num);
	}
	//FIXME delete index blocks

	return 1;
}

/**
 * \brief This function delete as many bundles from the storage as necessary to have at least one slot and the number of required of memory free
 * \param bundlemem Pointer to the MMEM struct containing the bundle
 * \return 1 on success, 0 if no room could be made free
 */
uint8_t storage_mmem_make_room(struct mmem * bundlemem)
{
    //printf("mmem_make_room: start, free_space: %lu\n",storage_mmem_get_free_space());
	struct bundle_list_entry_t * entry = NULL;
	struct bundle_t * bundle_new = NULL;
	struct bundle_t * bundle_old = NULL;

	/* Now delete expired bundles */  //FIXME not what we want...
	storage_mmem_prune();
    //printf("mmem_make_room: prune done, free_space: %lu\n",storage_mmem_get_free_space());

	/* If we do not have a pointer, we cannot compare - do nothing */
	if( bundlemem == NULL ) {
		return 0;
	}

	/* Keep deleting bundles until we have enough slots */
	while( bundles_in_storage >= (BUNDLE_STORAGE_SIZE -1)) {
		/* Obtain the new pointer each time, since the address may change */
		bundle_new = (struct bundle_t *) MMEM_PTR(bundlemem);

		/* We need this double-loop because otherwise we would be modifying the list
		 * while iterating through it
		 */
		for( entry = list_head(bundle_list);
			 entry != NULL;
			 entry = list_item_next(entry) ) {

	        if(is_index_tag(entry->cache_flags)){ //FIXME
	            continue;
	        }

			bundle_old = (struct bundle_t *) MMEM_PTR(entry->bundle);

		    //FIXME
		    //printf("mmem_make_room: RecTime: %lu , NumBlocks: %u , SrcNode: %lu , SrcSrv: %lu , DestNode: %lu , DestSrv: %lu , SeqNr: %lu , Lifetime: %lu, ID: %lu\n",
		    //        bundle_old->rec_time, bundle_old->num_blocks, bundle_old->src_node, bundle_old->src_srv, bundle_old->dst_node, bundle_old->dst_srv, bundle_old->tstamp_seq, bundle_old->lifetime, bundle_old->bundle_num);

			/* If the new bundle has a longer lifetime than the bundle in our storage,
			 * delete the bundle from storage to make room
			 */
			if( bundle_new->lifetime - (clock_seconds() - bundle_new->rec_time) >= bundle_old->lifetime - (clock_seconds() - bundle_old->rec_time) ) {
			    //FIXME
			    //printf("mmem_make_room DELETE: RecTime: %lu , NumBlocks: %u , SrcNode: %lu , SrcSrv: %lu , DestNode: %lu , DestSrv: %lu , SeqNr: %lu , Lifetime: %lu, ID: %lu\n",
			    //                 bundle_old->rec_time, bundle_old->num_blocks, bundle_old->src_node, bundle_old->src_srv, bundle_old->dst_node, bundle_old->dst_srv, bundle_old->tstamp_seq, bundle_old->lifetime, bundle_old->bundle_num);
				break;
			}
		}

		/* Either the for loop did nothing or did not break */
		if( entry == NULL ) {
		    //printf("mmem_make_room: nope, can't help you man, free_space: %lu\n",storage_mmem_get_free_space());
			/* We do not have deletable bundles in storage, stop deleting them */
			return 0;
		}

		/* Delete Bundle */
		storage_mmem_delete_bundle_by_bundle_number(entry->bundle_num);
	}
    //printf("mmem_make_room: done, free_space: %lu\n",storage_mmem_get_free_space());

	return 1;
}

/**
 * \brief create empty index block
 */
uint8_t storage_mmem_create_index_block(){
    //printf("storage_mmem_create_index_block()\n"); //FIXME
    int ret;
    struct bundle_slot_t *bs;
    struct bundle_index_entry_t *index_entry;
    struct bundle_list_entry_t * entry = NULL;

    bs = bundleslot_get_free();

    if( bs == NULL ) {
        LOG(LOGD_DTN, LOG_BUNDLE, LOGL_ERR, "Could not allocate slot for a index block");
        return 0;
    }

    //FIXME
    //ret = mmem_alloc(&bs->bundle, sizeof(struct bundle_t));
    ret = mmem_alloc(&bs->bundle, MIN_BUNDLESLOT_SIZE);
    if (!ret) {
        bundleslot_free(bs);
        LOG(LOGD_DTN, LOG_BUNDLE, LOGL_ERR, "Could not allocate memory for a index block");
        return 0;
    }

    index_entry = (struct bundle_index_entry_t *) MMEM_PTR(&bs->bundle);
    //memset(index_block, 0, sizeof(struct bundle_t));
    memset(index_entry, 0, MIN_BUNDLESLOT_SIZE);
    //memcpy(index_entry, temp_index_array, MIN_BUNDLESLOT_SIZE);

    entry = memb_alloc(&bundle_mem);
    if( entry == NULL ) {
        LOG(LOGD_DTN, LOG_STORE, LOGL_ERR, "unable to allocate struct, cannot store index block");
        bundle_decrement(&bs->bundle);
        return 0;
    }

    // we copy the reference to the list
    entry->bundle = &bs->bundle;

    //FIXME
    ++index_block_num;

    // Set bundle number //FIXME deprecated
    entry->bundle_num = index_block_num;

    // Mark as index block
    entry->cache_flags = CACHE_PARTITION_B_INDEX_INVALID_TAG;

    // Set dirty flag
    entry->cache_flags |= CACHE_DIRTY_FLAG;

    // Add block to the list
    list_add(bundle_list, entry);

    return 1;
}

/**
 * \brief adds index entry
 */
uint8_t storage_mmem_add_index_entry(uint32_t ID, uint32_t TargetNode){
    //printf("storage_mmem_add_index_entry: ID: %lu, Target: %lu\n", ID, TargetNode); //FIXME
    //FIXME in dem Moment, in dem die gültige Adresse feststeht

    static struct mmem *indexmem;
    static struct bundle_index_entry_t *index_entry;
    indexmem = storage_mmem_get_index_block(1);
    index_entry = (struct bundle_index_entry_t *) MMEM_PTR(indexmem);

    uint8_t i;
    for(i=last_index_entry; i<BUNDLE_STORAGE_INDEX_ARRAY_ENTRYS; ++i){
        if(index_entry[i].bundle_num == 0 && index_entry[i].dst_node == 0){
            index_entry[i].bundle_num = ID;
            index_entry[i].dst_node = TargetNode;
            last_index_entry = i;
            //printf("Index[%u] is now: (ID: %lu, Node: %lu)\n",i,index_entry[i].bundle_num,index_entry[i].dst_node); //FIXME
            //bundle_decrement(indexmem);
            return 1;
        }
    }
    printf("storage_mmem_add_index_entry failed\n");
    //bundle_decrement(indexmem);
    return 0;
}

/**
 * \brief finds index entry for ID, overwrites it with last_index_entry
 */
uint8_t storage_mmem_del_index_entry(uint32_t ID){
    //printf("storage_mmem_del_index_entry: ID: %lu\n", ID); //FIXME

    static struct mmem *indexmem;
    static struct bundle_index_entry_t *index_entry;
    indexmem = storage_mmem_get_index_block(1);
    index_entry = (struct bundle_index_entry_t *) MMEM_PTR(indexmem);

    uint8_t i;
    for(i=0; i<BUNDLE_STORAGE_INDEX_ARRAY_ENTRYS; ++i){
        if(index_entry[i].bundle_num == ID){
            //printf("Deleting Index[%u] (ID: %lu, Node: %lu)\n",i,index_entry[i].bundle_num,index_entry[i].dst_node); //FIXME
            index_entry[i].bundle_num = index_entry[last_index_entry].bundle_num;
            index_entry[i].dst_node = index_entry[last_index_entry].dst_node;
            index_entry[last_index_entry].bundle_num = 0;
            index_entry[last_index_entry].dst_node = 0;
            if(last_index_entry != 0){
                --last_index_entry;
            }
            //bundle_decrement(indexmem);
            return 1;
        }
    }
    printf("storage_mmem_del_index_entry failed\n");
    //bundle_decrement(indexmem);
    return 0;
}

/**
 * \brief Get the bundle list
 * \returns pointer to first bundle list entry
 */
struct mmem *storage_mmem_get_index_block(uint8_t blocknr){
    //printf("storage_mmem_get_index_block: NR: %u\n", blocknr); //FIXME

    //FIXME nur 1 block...
    if(blocknr != 1){
        return NULL;
    }

    struct bundle_list_entry_t * entry = NULL;
    struct bundle_index_entry_t * index_entry = NULL;

    // Look for the index_entry we are talking about
    for(entry = list_head(bundle_list);
            entry != NULL;
            entry = list_item_next(entry)) {

        //printf("storage_mmem_get_index_block: ID: %lu, Flags: %u\n",entry->bundle_num,entry->cache_flags);

        if(is_index_tag(entry->cache_flags)){ //FIXME
            index_entry = (struct bundle_index_entry_t *) MMEM_PTR(entry->bundle);
            break; //FIXME
        }

    }

    //get "next" index block from cache
    //struct cache_entry_t cache_block = BUNDLE_CACHE.cache_access_partition(CACHE_PARTITION_NEXT_BLOCK, CACHE_PARTITION_B_INDEX_START, last_index_block_address);

//    uint8_t i;
//    for(i=0; i<BUNDLE_STORAGE_INDEX_ENTRYS; ++i){
//      temp_index_array[i].bundle_num=i+1; //FIXME ID
//      temp_index_array[i].dst_node=i+1;  //FIXME Zielnode
//    }
//
//    //FIXME nur bei RAM-Block nötig
//    for(i=0; i<BUNDLE_STORAGE_INDEX_ENTRYS; ++i){
//          if(temp_index_array[i].bundle_num == 0 && temp_index_array[i].dst_node == 0){
//              *index_array_entrys = i-1;
//              break;
//          }
//    }
//
//
// TEST
//  int main(void){
//          struct storage_index_entry_t *array_ptr = NULL;
//          int array_length = 0;
//          array_ptr = storage_cached_get_bundles(STORAGE_CACHED_GET_BUNDLES_HEAD, &array_length);
//
//          int i;
//          for(i=0; i<array_length; ++i){
//                  printf("Bundle[%d] : [ID] %d : [Dest] %d\n",i,array_ptr[i].bundle_num,array_ptr[i].dst_node);
//          }
//          return 0;
//  }

    //return temp_index_array;

    //FIXME Beim 2. - n. Aufruf: Array 2 - n
    //      Dann 1mal länge 0
    //      Dann wieder von vorne
    //      Blockiert bis zum nächsten read den entsprechenden Cacheblock, d.h. Liste muss komplett durchlaufen werden
    //      state pro aufrufer?
    //      freigeben von liste?
    //      Kümmert sich um das Nachladen vom Flash

    if(index_entry != NULL) //FIXME
        return entry->bundle;

    return NULL;
}

//FIXME
void same_bundle(){
    printf("same bundle\n");
}

/**
 * \brief saves a bundle in storage
 * \param bundlemem pointer to the bundle
 * \param bundle_number_ptr pointer where the bundle number will be stored (on success)
 * \return 0 on error, 1 on success
 */
uint8_t storage_mmem_save_bundle(struct mmem * bundlemem, uint8_t flags)
{
    //FIXME testcode für STORAGE_PERSISTENT.write_block
//    uint16_t rb_address = 500, rb_length = 10, rb_prev = 0xFFFF, rb_next = 501;
//    uint8_t rb_data[10] = {0,1,2,3,4,5,6,7,8,9};
//    STORAGE_PERSISTENT.write_block(rb_address,rb_data,rb_length,rb_prev,rb_next);

	struct bundle_t *entrybdl = NULL,
					*bundle = NULL;
	struct bundle_list_entry_t * entry = NULL;

	if( bundlemem == NULL ) {
		LOG(LOGD_DTN, LOG_STORE, LOGL_WRN, "storage_mmem_save_bundle with invalid pointer %p", bundlemem);
		return 0;
	}

	// Get the pointer to our bundle
	bundle = (struct bundle_t *) MMEM_PTR(bundlemem);

	if( bundle == NULL ) {
		LOG(LOGD_DTN, LOG_STORE, LOGL_ERR, "storage_mmem_save_bundle with invalid MMEM structure");
		return 0;
	}

	// Look for duplicates in the storage
	for(entry = list_head(bundle_list);
		entry != NULL;
		entry = list_item_next(entry)) {

        if(is_index_tag(entry->cache_flags)){ //FIXME
            continue;
        }

		entrybdl = (struct bundle_t *) MMEM_PTR(entry->bundle);

		if( bundle->bundle_num == entrybdl->bundle_num ) {
			LOG(LOGD_DTN, LOG_STORE, LOGL_DBG, "%lu is the same bundle", entry->bundle_num);
			same_bundle(); //FIXME
			bundle_decrement(bundlemem);
			return 1;
		}
	}

	if( !storage_mmem_make_room(bundlemem) ) {
		LOG(LOGD_DTN, LOG_STORE, LOGL_ERR, "Cannot store bundle, no room");
		return 0;
	}

	// Now we have to update the pointer to our bundle, because MMEM may have been modified (freed) and thus the pointer may have changed
	bundle = (struct bundle_t *) MMEM_PTR(bundlemem);

	entry = memb_alloc(&bundle_mem);
	if( entry == NULL ) {
		LOG(LOGD_DTN, LOG_STORE, LOGL_ERR, "unable to allocate struct, cannot store bundle");
		bundle_decrement(bundlemem);
		return 0;
	}

	// Clear the memory area
	memset(entry, 0, sizeof(struct bundle_list_entry_t));

	// we copy the reference to the bundle, therefore we have to increase the reference counter
	entry->bundle = bundlemem;
	bundle_increment(bundlemem);
	bundles_in_storage++;

	// Set bundle number //FIXME deprecated
	entry->bundle_num = bundle->bundle_num;

	// Mark as bundle, dirty
	entry->cache_flags = CACHE_PARTITION_BUNDLES_INVALID_TAG | CACHE_DIRTY_FLAG;

	LOG(LOGD_DTN, LOG_STORE, LOGL_INF, "New Bundle %lu (%lu), Src %lu, Dest %lu, Seq %lu", bundle->bundle_num, entry->bundle_num, bundle->src_node, bundle->dst_node, bundle->tstamp_seq);

	// Notify the statistics module
	storage_mmem_update_statistics();

	// Add bundle to the list
	list_add(bundle_list, entry);

	// Now we have to (virtually) free the incoming bundle slot
	// This should do nothing, as we have incremented the reference counter before
	bundle_decrement(bundlemem);

	LOG(LOGD_DTN, LOG_STORE, LOGL_DBG, "save_done: RecTime: %lu , NumBlocks: %u , SrcNode: %lu , SrcSrv: %lu , DestNode: %lu , DestSrv: %lu , SeqNr: %lu , Lifetime: %lu, ID: %lu",
            bundle->rec_time, bundle->num_blocks, bundle->src_node, bundle->src_srv, bundle->dst_node, bundle->dst_srv, bundle->tstamp_seq, bundle->lifetime, bundle->bundle_num);

	//FIXME für letztes storage segment, agent mitteilen, dass wir alles haben
    if( flags == STORAGE_NO_SEGMENT || flags == STORAGE_LAST_SEGMENT ) {
        /* Add index entry*/
        storage_mmem_add_index_entry(bundle->bundle_num, bundle->dst_node);
        //FIXME !!!
#ifdef TEST_NO_NETWORK
        LOG(LOGD_DTN, LOG_STORE, LOGL_WRN, "save_bundle: TEST_NO_NETWORK\n");
#else
        process_post(&agent_process, dtn_bundle_in_storage_event, &entry->bundle_num);  //FIXME stattdessen hier routing aufrufen und das an service schicken?
#endif
    }

	return 1;
}

uint8_t storage_mmem_delete_bundle_by_index_entry(struct bundle_index_entry_t *index_entry){
    storage_mmem_delete_bundle_by_bundle_number(index_entry->bundle_num); //FIXME oder so
    storage_mmem_del_index_entry(index_entry->bundle_num);
    //FIXME indexeintrag löschen
    return 1;
}
/**
 * \brief deletes a bundle from storage
 * \param bundle_number bundle number to be deleted
 * \param reason reason code
 * \return 1 on success or 0 on error
 */
uint8_t storage_mmem_delete_bundle_by_bundle_number(uint32_t bundle_number)
{
	struct bundle_t * bundle = NULL;
	struct bundle_list_entry_t * entry = NULL;

	LOG(LOGD_DTN, LOG_STORE, LOGL_INF, "Deleting Bundle %lu", bundle_number);

	// Look for the bundle we are talking about
	for(entry = list_head(bundle_list);
		entry != NULL;
		entry = list_item_next(entry)) {

        if(is_index_tag(entry->cache_flags)){ //FIXME
            continue;
        }

		bundle = (struct bundle_t *) MMEM_PTR(entry->bundle);

		if( bundle->bundle_num == bundle_number ) {
			break;
		}
	}

	if( entry == NULL ) {
		LOG(LOGD_DTN, LOG_STORE, LOGL_ERR, "Could not find bundle %lu on storage_mmem_delete_bundle", bundle_number);
		return 0;
	}

	//FIXME das machen wir woanders...
	// Figure out the source to send status report
//	bundle = (struct bundle_t *) MMEM_PTR(entry->bundle);
//	bundle->del_reason = reason;
//
//	if( reason != REASON_DELIVERED ) {
//		if( (bundle->flags & BUNDLE_FLAG_CUST_REQ ) || (bundle->flags & BUNDLE_FLAG_REP_DELETE) ){
//			if (bundle->src_node != dtn_node_id){
//				STATUSREPORT.send(entry->bundle, 16, bundle->del_reason);
//			}
//		}
//	}

	//FIXME das auch
	// Notified the agent, that a bundle has been deleted
	agent_delete_bundle(bundle_number);

	//FIXME übergangsweise auch hier, eigentlich nur in gc oder storage_mmem_delete_bundle_by_index_entry()
    storage_mmem_del_index_entry(bundle->bundle_num);

	//FIXME slot trotzdem belegen, evtl. "valid"-flag
	bundle_decrement(entry->bundle);
	bundle = NULL;

	//FIXME naja
	// Remove the bundle from the list
	list_remove(bundle_list, entry);

	bundles_in_storage--;

	// Notify the statistics module
	storage_mmem_update_statistics();

	//FIXME mal schaun
	// Free the storage struct
	memb_free(&bundle_mem, entry);

	return 1;
}

//static int delete_block_once = 1; //FIXME testcode für STORAGE_PERSISTENT.delete_blocks

/**
 * \brief reads a bundle from storage
 * \param bundle_num bundle number to read
 * \return pointer to the MMEM struct, NULL on error
 */
struct mmem *storage_mmem_read_bundle(uint32_t bundle_num, uint32_t block_data_start_offset, uint16_t block_data_length)
{
    //FIXME testcode für STORAGE_PERSISTENT.read_block
//    uint16_t rb_address = 500, rb_length = 10, rb_prev_read, rb_next_read;
//    uint8_t rb_data_read[10];
//    STORAGE_PERSISTENT.read_block(rb_address,rb_data_read,rb_length,&rb_prev_read,&rb_next_read);
//
//    printf("READ_FROM_FLASH: Page(%u), Bytes(%u), Prev(%u), Next(%u)\n", rb_address, rb_length, rb_prev_read, rb_next_read);
//    int ii;
//    printf("READ_FROM_FLASH: Data: ");
//    for(ii=0; ii<rb_length; ++ii){
//        printf("%u",rb_data_read[ii]);
//    }
//    printf("\n");

    //FIXME testcode für STORAGE_PERSISTENT.delete_blocks
//    if(delete_block_once){
//        delete_block_once = 0;
//        printf("deleting block 500\n");
//        STORAGE_PERSISTENT.delete_blocks(500,500);
//    }

	struct bundle_list_entry_t * entry = NULL;
	struct bundle_t * bundle = NULL;

	// Look for the bundle we are talking about
	for(entry = list_head(bundle_list);
			entry != NULL;
			entry = list_item_next(entry)) {

        if(is_index_tag(entry->cache_flags)){ //FIXME
            continue;
        }

		bundle = (struct bundle_t *) MMEM_PTR(entry->bundle);

		if( bundle->bundle_num == bundle_num ) {
			break;
		}
	}
	LOG(LOGD_DTN, LOG_STORE, LOGL_DBG, "read_done: RecTime: %lu , NumBlocks: %u , SrcNode: %lu , SrcSrv: %lu , DestNode: %lu , DestSrv: %lu , SeqNr: %lu , Lifetime: %lu, ID: %lu",
            bundle->rec_time, bundle->num_blocks, bundle->src_node, bundle->src_srv, bundle->dst_node, bundle->dst_srv, bundle->tstamp_seq, bundle->lifetime, bundle->bundle_num);

	if( entry == NULL ) {
		LOG(LOGD_DTN, LOG_STORE, LOGL_WRN, "Could not find bundle %lu in storage_mmem_read_bundle", bundle_num);
		return 0;
	}

	if( entry->bundle->size == 0 ) {
		LOG(LOGD_DTN, LOG_STORE, LOGL_WRN, "Found bundle %lu but file size is %u", bundle_num, entry->bundle->size);
		return 0;
	}

	// Someone requested the bundle, he will have to decrease the reference counter again
	bundle_increment(entry->bundle);

	/* How long did this bundle rot in our storage? */
	uint32_t elapsed_time = clock_seconds() - bundle->rec_time;

	/* Update lifetime of bundle */
	if( bundle->lifetime < elapsed_time ) {
		bundle->lifetime = 0;
		bundle->rec_time = clock_seconds();
	} else {
		bundle->lifetime = bundle->lifetime - elapsed_time;
		bundle->rec_time = clock_seconds();
	}

	return entry->bundle;
}

/**
 * \brief checks if there is space for a bundle
 * \param bundlemem pointer to a bundle struct (not used here)
 * \return number of free slots
 */
uint32_t storage_mmem_get_free_space()
{
	return BUNDLE_STORAGE_SIZE - bundles_in_storage - 1; //FIXME das sollte erstmal tun, berechnung später prinzipiell falsch (BUNDLE_STORAGE_SIZE = BUNDLE_STORAGE_CACHE_SIZE - INDEX_CACHE_SIZE)
}

/**
 * \brief Get the number of slots available in storage
 * \returns the number of free slots
 */
uint16_t storage_mmem_get_bundle_count(void){
	return bundles_in_storage;
}

uint8_t storage_mmem_housekeeping(uint16_t time){
    return 1;
}
uint8_t storage_mmem_release_bundleslots(uint16_t size){
    return 0;
}
uint8_t storage_mmem_add_segment_to_bundle(struct mmem *bundlemem, uint16_t min_size){
    return 0;
}

const struct storage_driver storage_mmem = {
	"STORAGE_MMEM",
	storage_mmem_init,
	storage_mmem_flush,
    storage_mmem_housekeeping,
    storage_mmem_release_bundleslots,
	storage_mmem_save_bundle,
	storage_mmem_add_segment_to_bundle,
    storage_mmem_delete_bundle_by_bundle_number,
	storage_mmem_delete_bundle_by_index_entry,
    storage_mmem_read_bundle,
	storage_mmem_get_free_space,
	storage_mmem_get_bundle_count,
	storage_mmem_get_index_block,
};

/** @} */
/** @} */
