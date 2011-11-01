 /**
 * \addtogroup sdnv SDNV
 *
 * @{
 */
/**
 * \file 
 * implementation of sdnv functions
 * \author Georg von Zengen (vonzeng@ibr.cs.tu-bs.de) 
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/** maximum sdnv length */
#define MAX_LENGTH 8 

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

int sdnv_encode(uint32_t val, uint8_t* bp, size_t len)
{
	size_t val_len = 0;
	uint32_t tmp = val;

	do {
		tmp = tmp >> 7;
		val_len++;
	} while (tmp != 0);


	if (len < val_len) {
		return -1;
	}

	bp += val_len;
	uint8_t high_bit = 0; // for the last octet
	do {
		--bp;
		*bp = (uint8_t)(high_bit | (val & 0x7f));
		high_bit = (1 << 7); // for all but the last octet
		val = val >> 7;
	} while (val != 0);

	return val_len;
}

size_t sdnv_encoding_len(uint32_t val)
{
	size_t val_len = 0;
//	printf("val %lu ptr %p\n ",val,&val);
	do {
		val = val >> 7;
		val_len++;
	} while (val != 0);
	
	return val_len;
}

int sdnv_decode(const uint8_t* bp, size_t len, uint32_t* val)
{
	PRINTF("sdnv_decode\n");
	const uint8_t* start = bp;
	if (!val) {
		PRINTF("SDNV: NULL pointer\n");
		return -1;
	}
	size_t val_len = 0;
	*val = 0;
	do {
		PRINTF("SDNV: len: %u\n", len);
		if (len == 0){
			PRINTF("SDNV: buffer too short\n");
			return -1; // buffer too short
		}
		*val = (*val << 7) | (*bp & 0x7f);
		++val_len;

		if ((*bp & (1 << 7)) == 0){
			break; // all done;
		}

		++bp;
		--len;
	} while (1);

	if ((val_len > MAX_LENGTH) || ((val_len == MAX_LENGTH) && (*start != 0x81)))
		PRINTF("SDNV: val_len >= %u\n",MAX_LENGTH);
		return -1;
	
	PRINTF("SDNV: val: %lu\n", *val);
	return val_len;
}

size_t sdnv_len(const uint8_t* bp)
{
	size_t val_len = 1;
	for ( ; *bp++ & 0x80; ++val_len )
		;
	return val_len;
}
/** @} */



