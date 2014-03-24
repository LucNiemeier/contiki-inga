
 /**
 * \addtogroup bprocess
 *
 * @{
 */

/**
 * \file 
 * implementation of forwarding
 * \author Georg von Zengen (vonzeng@ibr.cs.tu-bs.de) 
 */
#include "bundle.h"
#include "agent.h"
#include "custody.h"
#include "storage.h"
#include "mmem.h"


#define DEBUG 0 
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

uint16_t *saved_as_num;
uint16_t *forwarding_bundle(struct bundle_t *bundle)
{
	PRINTF("FORWARDING:1 bundle->mem.ptr %p\n",bundle->mem.ptr);
	//uint32_t flags;
	int32_t saved;
	//sdnv_decode(bundle->mem.ptr + bundle->offset_tab[FLAGS][OFFSET], bundle->offset_tab[FLAGS][STATE], &flags);
	if (bundle->flags & 0x08){ // bundle is custody
		
		saved =CUSTODY.decide(bundle);
	}else{
		saved = BUNDLE_STORAGE.save_bundle(bundle);
		PRINTF("FORWARDING:2 bundle->mem.ptr %p\n",bundle->mem.ptr);
	}
	PRINTF("FORWARDING saved in %ld\n", saved);
	if( saved >=0){
		
		saved_as_num=memb_alloc(saved_as_mem);
		if(saved_as_num==NULL){
			delete_bundle(bundle);
			return NULL;
		}
		*saved_as_num= (uint16_t)saved;
//		printf("FORWARDING: %u %p %p\n", *saved_as_num,saved_as_num, saved_as_mem);
		PRINTF("FORWARDING: bundle_num %u\n",*saved_as_num);
		delete_bundle(bundle);
		PRINTF("FORWARDING\n");
		return saved_as_num;
	}else{
		delete_bundle(bundle);
		printf("FORWARDING: bundle not saved\n");
		delete_bundle(bundle);
		return NULL;
	}

}
