/**
   \file funserv.h
   \author Marco Ponza
   \brief  header per la libreria di funzioni per msgserver
   Si dichiara che ogni singolo bit presente in questo file è solo ed esclusivamente "farina del sacco" del rispettivo autore :D
 */

#include "genHash.h"
#include "genList.h"
#include "comsock.h"


/** La stringa [MTX] sta ad indicare che la rispettiva funzione/procedura opera in mutua esclusione */


typedef struct field {
	/* struttura a cui punteranno i payload degli elementi della tabella hash*/
	int skt;
	pthread_mutex_t mtx;
} field_t;

/** Funzione che restituisce un puntatore alla copia di un intero
 *  
 *  \param a, intero da copiare
 *  \retval _a, puntatore alla copia 
 */
void * copy_int(void *a);

/** Funzione che confronta due interi
 *  
 *  \param a, primo intero da confrontare
 *  \param b, secondo intero da confrontare
 *
 *  \retval 0, se sono uguali
 *  \retvale != 0, se sono diversi
 */
int compare_int(void *a, void *b);

/** Funzione che confronta due stringhe
 *  
 *  \param a, primo stringa da confrontare
 *  \param b, secondo stringa da confrontare
 *
 *  \retval 0, se sono uguali
 *  \retvale != 0, se sono diversi
 */
int compare_string(void *a, void *b);

/** Funzione che restituisce un puntatore alla copia di una stringa
 *  
 *  \param a, stringa da copiare
 *  \retval _a, puntatore alla copia 
 */
void * copy_string(void * a);

/** Funzione che restituisce un puntatore alla copia di un pthread_t
 *  
 *  \param a, pthread_t da copiare
 *  \retval _a, puntatore alla copia 
 */
void * copy_pthread_t (void *a);

/** Funzione che confronta due pthread_t
 *  
 *  \param a, primo pthread_t da confrontare
 *  \param b, secondo pthread_t da confrontare
 *
 *  \retval 0, se sono uguali
 *  \retvale != 0, se sono diversi
 */
int compare_pthread_t (void *a, void *b);

/** Funzione che restituisce un puntatore alla copia di un field_t
 *  
 *  \param a, field_t da copiare
 *  \retval _a, puntatore alla copia 
 */
void * copy_field (void * a);

/** Esegue il locking sulla variabile mtx

    \param mtx variabile per la mutua esclusione
 */
void Lock (pthread_mutex_t * mtx);

/** Esegue l'unlocking sulla variabile mtx

    \param mtx variabile per la mutua esclusione
 */
void Unlock (pthread_mutex_t * mtx);

/** Procedura che inserisce una stringa nella variabile users_list
 * 
 * 	\param str, stringa da inserire
 */
void Add_user (char * str);

/** Procedura che elimina una stringa nella variabile users_list
 * 	
 * 	\param str, stringa da rimuovere
 */
void Remove_user (char * str);

/** Procedura che ritorna un puntatore alla copia delle variabile users_list
 * 
 * 	\retval users_list, lista degli utenti connessi
 */
char * Listing ();

/** [MTX] Procedura che prende in ingresso 3 stringhe e le concatena
 *  nel formato "mittente:destinatario:messaggio\n" alla variabile to_write
 * 	
 * 	\param mit, mittente
 * 	\param dest, destinatario
 *  \param mess, messaggio
 */
void Add_string (char * mit, char * dest, char * mess);

/** Procedura che ridimensiona al valore NWRITE e resetta la variabile to_write
 */
void Reset_string ();

/** [MTX] Aggiunge un thread alla lista dei thread attivi

    \param thread_id identificatore del thread
    \param type tipolologia del thread (Dispatcher, Worker)
 */
void Add_thread_list (pthread_t thread_id, char * type);

/** [MTX] Rimuove un thread alla lista dei thread attivi (usato esclusivamente dai thread worker)

    \param thread_id identificatore del thread
 */
void Remove_thread_list (pthread_t thread_id);

/**  Chiude la socket gestendo l'errore
 *   \param skt, socket da chiudere
 */
void Close_skt ( int skt);

/** Lettura dalla socket gestendo l'errore 
 * 
 *  \param skt, socket da cui leggere
 *  \param msg, messaggio da leggere
 * 
 *   \retval n, numero di byte letti
 */
int Receive_skt (int skt, message_t * msg);

/** Scrittura sulla socket gestendo l'errore 
 * 	
 *  \param skt, socket su cui scrivere
 *  \param msg, messaggio da scrivere
 * 
 *  \retval n, numero di byte scritti
 */
int Send_skt (int skt, message_t * msg);

/** Funzione restituisce un puntatore al payload di un elemento
 *  della tabella hash (hash_table) con key == username
 * 	
 *  \param username, nome utente da cercare
 *  \retval payload, puntatore al payload
 *  \retval NULL, se non è presente nessun elemento nella tabella hash con chiave username
 */
field_t * Field_hash_element (char * key);

/** [MTX] Procedura che disconnette un thread da un client che desidera
 *  disconnettersi o che si è gia disconnesso, aggiornando la tabella hash
 *  e la lista dei thread attivi.
 * 
 *  \param thread_id, id del thread che chiama la procedura
 *  \param client, username del client da disconnettere
 * 
 * */
void Disconnect (pthread_t thread_id, char * client, int skt);

/** Procedura che distrugge la tabella hash ed evenutali variabili
 *  pthread_mutex_t presenti nel campo payload
 * 
 */
void Destroy_hash ();

/** [MTX] Funzione restituisce un puntatore ad una stringa contenente le chiavi degli
 *  degli elementi di hash_table, i cui payload.skt > -1, separati da uno spazio
 * 	
 *  \retval str, stringa di concatenazione delle chiavi
 */
char * List_connected ();

/** Funzione che restituisce un puntatore alla stringa (destinatario) a cui spedire il messaggio.
 * 	A ritorno della funzione, msg è la struttura che deve essere effettivamente 
 *  inviata al destinatario.
 * 
 * 	\param msg, struttura da manipolare per ottenere il destinatario e da modificare
 * 				affinche, al ritorno dalla funzione, msg contenga la struttura da inviare
 * 				tramite una Send_skt
 *  \param mit, mittente del messaggio
 * 
 *	\reval dest, puntatore alla stringa che continene l username del destinatario
 * 				 (allocato all interno della funzione)
 */
char * Divide_to_one ( message_t * msg, char * mit );

/** Procedura che manipola msg in modo che al ritorno della procedura la
 *  variabile msg sia la struttura che deve essere effettivamente inviata
 * 	come messaggio di broadcast.
 * 
 * 	\param msg, struttura da manipolare per ottenere il destinatario e da modificare
 * 				affinche, al ritorno dalla funzione, msg contenga la struttura da inviare
 * 				tramite una Send_skt
 *  \param mit, mittente del messaggio
 * 
 */
void Divide_bcast ( message_t * msg, char * mit );

/** [MTX] Procedura che invia un messaggio ad un utente, se questo è connesso al server
 *  o invia al mittente un messaggio d'errore se il destinatario non è conneesso.
 * 
 * 	\param mit, mittente del messaggio
 * 	\param dest, destinatario del messaggio
 *  \param msg, messaggio da inviare
 * 	\param this_cli_mtx, variabile per la mutua esclusione con il mittente
 * 						(in questo modo la complessità dell'invio è O(1) )
 * 	\retval 1, se è andato tutto a buon fine
 *  \retval 0, se è stato inviato un messaggio d'errore ed buffer "vecchio" (contenuto in msg) è gia stato deallocato
 */
int Send_to_one (char * mit, char * dest, message_t * msg, int mit_skt, pthread_mutex_t * mit_mtx);

/** Funzione che restituisce una copia della n-esima stringa contenuta in
 * 	users_list (separata l una dalle altra da uno spazio).
 * 	Per essere usata correttamente n <= al numero di stringhe separate da
 * 	spazio presenti in users_list.
 * 
 * 	\param n, n-esima stringa da restituire come copia
 * 	\retval str, puntatore alla stringa da restituire (allocata all interno della funzione)
 */
char * User (int n);

/** Procedura che invia msg a tutti gli utenti connessi
 * 	\param msg, messaggio da inviare
 * 	\param mit, mittente del messaggio da utilizzare per l'aggiornamento
 * 				della variabile to_write
 */
void Bcast (message_t * msg, char * mit);

/** [MTX] Funzione che abilita o meno un utente alla connessione sul server
 *	se abilitato, viene aggiornato il socket (associato a quel client) sulla tabella hash
 *	e ritornato un puntatore alla variabile mutex dell elemento sulla tabella hash con
 *  key == username
 *	
 *	\param skt socket del client
 *	
 *	\retval mtx se il client non è abilitato
 *	\retval NULL se il client non viene abilitato, chiude eventuali socket aperte
 */
pthread_mutex_t * Enable_connect (int skt, char * username);
