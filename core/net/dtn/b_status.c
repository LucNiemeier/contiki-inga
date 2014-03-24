#include "bundle.h"
#include "status-report.h"
#include "dtn_config.h"
#include "agent.h"
#include "mmem.h"
#include "storage.h"
#include "sdnv.h"
uint8_t b_stat_send(struct bundle_t *bundle,uint8_t status, uint8_t reason)
{
	printf("STAT: send\n");
	struct mmem report;
	uint8_t size=4; //1byte admin record +1 byte status + 1byte reason + 1byte timestamp (0)
	uint8_t type=16;
	uint16_t f_len,d_len;
	uint32_t len;
	if( bundle->flags & 1){ //bundle fragment
		type +=1;
		size += bundle->offset_tab[FRAG_OFFSET][STATE];
		uint8_t off=0;
		while( *((uint8_t*)bundle->mem.ptr + bundle->offset_tab[DATA][OFFSET] + off) != 0){
			f_len=sdnv_len((uint8_t*)bundle->mem.ptr + bundle->offset_tab[DATA][OFFSET]+1+off);
			d_len=sdnv_len((uint8_t*)bundle->mem.ptr + bundle->offset_tab[DATA][OFFSET]+1+f_len+off);
			sdnv_decode((uint8_t*)bundle->mem.ptr + bundle->offset_tab[DATA][OFFSET]+1+f_len+d_len+off , d_len , &len);
			off+=f_len + d_len + len;
		}
		f_len=sdnv_len((uint8_t*)bundle->mem.ptr + bundle->offset_tab[DATA][OFFSET]+1+off);
		d_len=sdnv_len((uint8_t*)bundle->mem.ptr + bundle->offset_tab[DATA][OFFSET]+1+f_len+off);
		sdnv_decode((uint8_t*)bundle->mem.ptr + bundle->offset_tab[DATA][OFFSET]+1+f_len+d_len+off , d_len , &len);
		size+=len;
	}

	size += bundle->offset_tab[TIME_STAMP][STATE];
	size += bundle->offset_tab[TIME_STAMP_SEQ_NR][STATE];
	size += bundle->offset_tab[SRC_NODE][STATE];
	size += bundle->offset_tab[SRC_SERV][STATE];
	

	if (!mmem_alloc(&report,size)){
		printf("STAT: mmem ERROR\n");
		return 0;
	}
	*(uint8_t*) report.ptr = type;
	*(((uint8_t*) report.ptr)+1)= status;
	*(((uint8_t*) report.ptr)+2)= reason;
	uint8_t offset=3;
	if( bundle->flags & 1){ 
		memcpy(((uint8_t*) report.ptr) + offset, bundle->mem.ptr + bundle->offset_tab[FRAG_OFFSET][OFFSET],bundle->offset_tab[TIME_STAMP][STATE]);
		offset+= bundle->offset_tab[FRAG_OFFSET][STATE];
		struct mmem sdnv;
		uint8_t sdnv_len= sdnv_encoding_len(len);
		if(!mmem_alloc(&sdnv,sdnv_len)){
			printf("STAT: mmem ERROR2\n");
			mmem_free(&report);
			return 0;
		}
		sdnv_encode(len, (uint8_t*) sdnv.ptr, sdnv_len);
		memcpy(((uint8_t*) report.ptr) + offset , sdnv.ptr , sdnv_len);
		offset+= sdnv_len;
		mmem_free(&sdnv);
	}
	*(((uint8_t*) report.ptr)+offset)= 0;
	offset+=1;
	memcpy(((uint8_t*) report.ptr) + offset, bundle->mem.ptr + bundle->offset_tab[TIME_STAMP][OFFSET],bundle->offset_tab[TIME_STAMP][STATE]);
	offset+= bundle->offset_tab[TIME_STAMP][STATE];
	memcpy(((uint8_t*) report.ptr) + offset, bundle->mem.ptr + bundle->offset_tab[TIME_STAMP_SEQ_NR][OFFSET], bundle->offset_tab[TIME_STAMP_SEQ_NR][STATE]);
	offset+= bundle->offset_tab[TIME_STAMP_SEQ_NR][STATE];
	memcpy(((uint8_t*) report.ptr) + offset, bundle->mem.ptr + bundle->offset_tab[SRC_NODE][OFFSET], bundle->offset_tab[SRC_NODE][STATE]);
	offset+= bundle->offset_tab[SRC_NODE][STATE];
	memcpy(((uint8_t*) report.ptr) + offset, bundle->mem.ptr + bundle->offset_tab[SRC_SERV][OFFSET], bundle->offset_tab[SRC_SERV][STATE]);

	struct bundle_t rep_bundle;
	create_bundle(&rep_bundle);
	uint32_t tmp;
	if(status&2){
		sdnv_decode((uint8_t*)bundle->mem.ptr + bundle->offset_tab[CUST_NODE][OFFSET],bundle->offset_tab[CUST_NODE][STATE],&tmp);
	}else{
		sdnv_decode((uint8_t*)bundle->mem.ptr + bundle->offset_tab[REP_NODE][OFFSET],bundle->offset_tab[REP_NODE][STATE],&tmp);
	}
	set_attr(&rep_bundle, DEST_NODE, &tmp);
	if(status&2){
	
		sdnv_decode((uint8_t*)bundle->mem.ptr + bundle->offset_tab[CUST_SERV][OFFSET],bundle->offset_tab[CUST_SERV][STATE],&tmp);
	}else{
		sdnv_decode((uint8_t*)bundle->mem.ptr + bundle->offset_tab[REP_SERV][OFFSET],bundle->offset_tab[REP_SERV][STATE],&tmp);
	}
	set_attr(&rep_bundle, DEST_SERV, &tmp);
	set_attr(&rep_bundle, SRC_NODE, &dtn_node_id);
	tmp=2;
	set_attr(&rep_bundle, FLAGS, &tmp);
	tmp=0;
	set_attr(&rep_bundle, SRC_SERV, &tmp);
	set_attr(&rep_bundle, REP_NODE, &tmp);
	set_attr(&rep_bundle, REP_SERV, &tmp);
	set_attr(&rep_bundle, CUST_NODE, &tmp);
	set_attr(&rep_bundle, CUST_SERV, &tmp);
	set_attr(&rep_bundle, TIME_STAMP, &tmp);
	set_attr(&rep_bundle,TIME_STAMP_SEQ_NR,&dtn_seq_nr);
	dtn_seq_nr++;
	tmp=3000;
	set_attr(&rep_bundle, LIFE_TIME, &tmp);
	struct mmem tmp_mem;
	if(!mmem_alloc(&tmp_mem, rep_bundle.mem.size + size)){
		printf("STAT: mmem ERROR3\n");
		mmem_free(&report);
		mmem_free(&rep_bundle.mem);
		return 0;
	}
	memcpy(tmp_mem.ptr , rep_bundle.mem.ptr , rep_bundle.mem.size);
	memcpy(tmp_mem.ptr+ rep_bundle.mem.size, report.ptr, size);
	mmem_free(&report);
	mmem_free(&rep_bundle.mem);
	memcpy(&rep_bundle.mem, &tmp_mem, sizeof(tmp_mem));
	mmem_reorg(&tmp_mem,&rep_bundle.mem);

	int32_t saved=BUNDLE_STORAGE.save_bundle(&rep_bundle);
	if (saved >=0){
		saved_as_num= (uint16_t)saved;
		delete_bundle(&rep_bundle);
		process_post(&agent_process,dtn_bundle_in_storage_event, &saved_as_num);
		return 1;
	}else{
		delete_bundle(&rep_bundle);
		return 0;
	}




		
			


}

const struct status_report_driver b_status ={
	"B_STATUS",
	b_stat_send,
};
