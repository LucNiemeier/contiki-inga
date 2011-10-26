#include <stdint.h>


//primary block defines
#define VERSION 0
#define FLAGS 1
#define LENGTH 2
#define DEST_NODE 3 
#define DEST_SERV 4
#define SRC_NODE 5 
#define SRC_SERV 6
#define REP_NODE 7
#define REP_SERV 8
#define CUST_NODE 9
#define CUST_SERV 10 
#define TIME_STAMP 11
#define TIME_STAMP_SEQ_NR 12
#define LIFE_TIME 13
#define FRAG_OFFSET 14
#define APP_DATA_LEN 15

//payload block defines
#define TYPE 16
#define P_FLAGS 17 
#define P_LENGTH 18
#define PAYLOAD 19

#define OFFSET 0
#define STATE 1

struct bundle_t{
	char offset_tab[20][2];
	uint8_t *block;	
	uint8_t size;
};

uint8_t recover_bundel(struct bundle_t *bundle, uint8_t *block);
uint8_t set_attr(struct bundle_t *bundle, uint8_t attr, uint64_t *val);
uint8_t create_bundle(struct bundle_t *bundle, uint8_t *payload, uint8_t len);
