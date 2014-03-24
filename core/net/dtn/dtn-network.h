/**
 * \defgroup interface das Network-Layer-Interface
 *
 * @{
 */
 /**
 * \file
 *         Headerfile f�r das Netzwerk-Interface
 *
 */

#ifndef DTN_NETWORK_H
#define DTN_NETWORK_H

#include "contiki-conf.h"
#include "rime.h"
#include "net/rime/rimeaddr.h"
#include "bundle.h"
#include "routing.h"
/**
*   \brief Treiber f�r das Protokoll
*
*   Notwendig f�r die Anbindung an den Netstack
*
*/
extern const struct network_driver dtn_network_driver;

extern const struct mac_driver *dtn_network_mac;

process_event_t dtn_beacon_event;

/**
*   \brief Sendet ein B�ndel
*
*    Ein B�ndel muss ich Bundlebuffer stehen um gesendet zu werden
*
*/
int dtn_network_send(struct bundle_t *bundle, struct route_t *route);


#endif
/** @} */

