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

/**
*   \brief Treiber f�r das Protokoll
*
*   Notwendig f�r die Anbindung an den Netstack
*
*/
extern const struct network_driver dtn_network_driver;

extern const struct mac_driver *dtn_network_mac;


/**
*   \brief Sendet ein B�ndel
*
*    Ein B�ndel muss ich Bundlebuffer stehen um gesendet zu werden
*
*/
int dtn_network_send(uint8_t *payload_ptr, uint8_t payload_len, rimeaddr_t dest);

int dtn_discover(void);

/**
*	\brief send node discovery
*/
int dtn_discover(void);
#endif
/** @} */

