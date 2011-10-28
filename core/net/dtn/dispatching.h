/**
 * \addtogroup agent
 * @{
 */
 
 /**
 * \defgroup dispatching Abfertigung empfangener B�ndel
 *
 * @{
 */
 
 /**
 * \file
 *         Headerfile f�r Funktion zur Abfertig
 *
 */
#ifndef DISPATCHING_H
#define DISPATCHING_H

/**
*   \brief Abfertigung des B�ndels, Eintscheidung ob Weiterleitung oder Auslieferung an Anwendungen
*
*   \param  bundle das zu verarbeitende B�ndel
*/
void dispatch_bundle(bundle_t *bundle);

#endif

/** @} */
/** @} */
