#ifndef __G_STORAGE_H__
#define __G_STORAGE_H__

#define BUNDLE_STORAGE_SIZE 10 
#define BUNDLE_STARAGE_FILE_NAME "list_file"
#include "bundle.h"
#include "net/rime/rimeaddr.h"

extern const struct storage_driver g_storage_driver;

struct file_list_entry_t{
	uint16_t bundle_num;
	uint16_t file_size;
	uint32_t lifetime;
	uint32_t src;
	uint32_t time_stamp;
	uint32_t time_stamp_seq;
	uint32_t fraq_offset;
	uint32_t rec_time;
	uint8_t  custody;
	rimeaddr_t msrc;
};

extern struct file_list_entry_t file_list[BUNDLE_STORAGE_SIZE];


void init(void);
void reinit(void);
/**
returns bundle_num
*/
int32_t save_bundle(struct mmem *bundlemem);

uint16_t del_bundle(uint16_t bundle_num,uint8_t reason);
void g_store_reduce_lifetime();

struct mmem *read_bundle(uint16_t bundle_num);
uint16_t free_space(struct mmem *bundlemem);
#endif
