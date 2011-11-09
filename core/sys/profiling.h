#ifndef __PROFILING_H__
#define __PROFILING_H__

#include <stdint.h>

#define PROFILING_STARTED 1

/* The structure that holds the callsites */
struct profile_site_t {
	void *from;
	void *addr;
	uint32_t calls;
	uint32_t time_accum;
	uint16_t time_start;
};

struct profile_t {
	int status;
	uint16_t max_sites;
	uint16_t num_sites;
	struct profile_site_t *sites;
};



void profiling_init(void) __attribute__ ((no_instrument_function));
void profiling_start(void) __attribute__ ((no_instrument_function));
void profiling_stop(void) __attribute__ ((no_instrument_function));
void profiling_report(uint8_t pretty) __attribute__ ((no_instrument_function));
struct profile_t *profiling_get(void) __attribute__ ((no_instrument_function));

#endif /* __PROFILING_H__ */
