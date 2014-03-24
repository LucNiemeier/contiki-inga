/**
 * \addtogroup agent
 * @{
 */
 
 /**
 * \defgroup custody Custody Transfer
 *
 * @{
 */
 
 /**
 * \file
 *         Headerfile f�r Funktionen zur persistenten Speicherung von B�ndeln, sowie Custody Transfer Funktionen
 *
 */
 
#ifndef CUSTODY_H
#define CUSTODY_H

#include "API/bundle.h"
#include "contiki.h"

/**
*   \brief Rentransmission Timer f�r gespeicherte B�ndel
*/
static struct etimer custody_etimer;


/**
*   \brief Initialisierungsfunktion f�r das Custody Modul
*
*   Wird vom Bundle Protocol beim Start ausgef�hrt
*/
void custody_init(void);

/**
*   \brief Setzt den aktuellen Knoten als Custodian im B�ndel-Dictionary
*
*   \param bundle Das B�ndel, im dem der neue Custodian eingetragen werden soll
*/
void custody_set_custodian(bundle_t *bundle);

/**
*   \brief Gibt den Inhalt der Custody Liste auf der Konsole aus -> Debuggingfunktion
*/
void check_custody_list(void);

/**
*   \brief Liest die im Flash gespeicherte Custody Liste ein
*
*   Wird von der Init-Funktion aufgerufen
*
*   \return Anzahl gelesener Bytes
*/
int read_custody_list(void);

/**
*   \brief �berpr�ft ob genug Speicher f�r ein weiteres Bundle  frei ist
*
*   \return 0 wenn kein Platz, 1 wenn noch Platz ist
*/
int custody_check_memory(void);

/**
*   \brief Speichern eins B�ndels im Flash-Speicher
*
*   \param bundle Das zu speichernde B�ndel
*
*   \return -1 wenn speichern fehlgeschlagen,  0 wenn B�ndel schon verhanden, oder die L�nge
*   der gespeicherten Daten wenn erfolgreich
*/
int custody_save_bundle(bundle_t *bundle);

/**
*   \brief Auslesen eins B�ndels aus Flash-Speicher
*
*   Es wird immer das B�ndel ausgelesen was als n�chstes gesendet werden muss
*
*   \param bundle Struktur in der B�ndel eingelesen werden soll
*
*   \return -1 wenn lesen fehlgeschlagen oder die L�nge der gelesenen Daten, wenn erfolgreich
*/
int custody_read_bundle(bundle_t *bundle);


/**
*   \brief �berpr�ft ob ein B�ndel bereits im Speicher ist
*
*   \param eid EID des zu �berpr�fenden B�ndels
*   \param timestamp Zeitstempel des zu �berpr�fenden B�ndels
*   \param timestamp_seq Zeitstempel-Sequenznummer des zu �berpr�fenden B�ndels
*
*   \return 0 wenn B�ndel noch nicht im Speicher, 1 wenn schon vorhanden
*/
int custody_check_redundancy(uint32_t src_node, uint32_t src_app, uint32_t timestamp, uint32_t timestamp_seq);

/**
*   \brief Entfernt B�ndel aus Speicher
*
*   \param eid EID des zu entfernenden B�ndels
*   \param timestamp Zeitstempel des zu entfernenden B�ndels
*   \param timestamp_seq Zeitstempel-Sequenznummer des zu entfernenden B�ndels
*
*   \return -1 wenn Datei nicht gefunden, sonst Anzahl der im Flash freigewordenen Bytes
*/
int custody_remove_bundle(uint32_t src_node, uint32_t src_app, uint32_t timestamp, uint32_t timestamp_seq);


#endif


/** @} */
/** @} */
