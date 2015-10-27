/**
   \file funcli.h
   \author Marco Ponza
   \brief  header per la libreria di funzioni per msgcli
   Si dichiara che ogni singolo bit presente in questo file è solo ed esclusivamente "farina del sacco" del rispettivo autore :D
 */

#ifndef __FUNCLI__H
#define __FUNCLI__H

/** ========== Variabili globali ========== */
extern pthread_t handler; /* variabile globale per far terminare l handler in caso di %EXIT */
extern pthread_t sender; /* variabile globale per far terminare il sender in caso di SIGINT o SIGTERM */
extern pthread_t receiver; /* variabile globale per far terminare il receiver in caso di SIGINT o SIGTERM */

/** Esegue il locking sulla variabile mtx

    \param mtx variabile per la mutua esclusione
 */
void Lock (pthread_mutex_t * mtx);

/** Esegue l'unlocking sulla variabile mtx

    \param mtx variabile per la mutua esclusione
 */
void Unlock (pthread_mutex_t * mtx);

/**  Chiude la socket gestendo l'errore
 * 	 \param skt, socket da chiudere
 */
void Close_skt (int skt);
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

/** Funzione che permette di valutare se una stringa contiene solo
 * 	caratteri definiti dal nostro "standard" per il progetto
 * 	
 * 	\param str, stringa da valutare
 * 	\retval 1, se la stringa è ben formata
 * 	\retval	0, se non lo è
 */
int Is_good_str (char * str);	

/** Funzione che permette di valutare se una stringa ripsetta la sintassi
 * 	per l'invio di un messaggio MSG_TO_ONE
 * 
 * 	\param str, stringa da valutare
 * 	\retval 1, se rispetta la sintassi "destinatario messaggio"
 * 	\retval 0, se non la rispetta
 */
int To_one_good_str (char * str);

/** Funzione che shifta una stringa di n posizioni verso sinistra
 * 
 * 	\param str, stringa da shiftare
 * 	\param n, valore che indica di quanto shiftare str
 */
void Left_shift (char * str, int n);

#endif
