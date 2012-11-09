/**
 * \addtogroup bprocess
 * @{
 */
 
 
 /**
 * \file
 *         
 */
#ifndef DISPATCHING_H
#define DISPATCHING_H

#include "contiki.h"
#include "mmem.h"

/**
*   \brief decides if bundle must be delivered or forwarded
*
*   \param  bundle bundle to be processed
*   \returns <= 0 on error >0 on success
*/
int dispatching_dispatch_bundle(struct mmem *bundlemem);

#endif

/** @} */
/** @} */
