#ifndef __ROUTING_H__
#define __ROUTING_H__

#include <stdlib.h>
#include <stdio.h>
#include "net/dtn/bundle.h"
struct routing_driver {
	char *name;

	void (* init)(void);
	void (* new_neighbor)(rimeaddr_t *dest);
	void (* new_bundle)(uint16_t bundle_num);
	void (* del_bundle)(uint16_t bundle_num);
};
extern const struct routing_driver ROUTING;
#endif
