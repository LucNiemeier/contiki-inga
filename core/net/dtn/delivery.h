/**
 * \addtogroup agent
 * @{
 */
 
 /**
 * \defgroup delivery Auslieferung von B�ndeln
 *
 * @{
 */
 
 /**
 * \file
 *         Headerfile �bergabefunktion an Anwendungen
 *
 */
 
#ifndef DELIVERY_H
#define DELIVERY_H

/**
*   \brief �bergibt Nutzdaten eines B�ndels an Anwendung
*
*   \param bundle Das empfangene B�ndel
*   \param registration der empfangenden prozesses
*/
void deliver_bundle(bundle_t *bundle, struct registration *n);

#endif

/** @} */
/** @} */
