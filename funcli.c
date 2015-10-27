/**
   \file funcli.c
   \author Marco Ponza
   \brief  libreria di funzioni per msgcli
   Si dichiara che ogni singolo bit presente in questo file è solo ed esclusivamente "farina del sacco" del rispettivo autore :D
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/un.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

#include "comsock.h"

#define ERROR_SEND_MSG "Errore nell invio del messaggio"
#define ERROR_RECEIVE_MSG "Client Errore nella ricezione del messaggio"

/** ========== Variabili globali ========== */
extern pthread_t handler; /* variabile globale per far terminare l handler in caso di %EXIT */
extern pthread_t sender; /* variabile globale per far terminare il sender in caso di SIGINT o SIGTERM */
extern pthread_t receiver; /* variabile globale per far terminare il receiver in caso di SIGINT o SIGTERM */

/** Esegue il locking sulla variabile mtx

    \param mtx variabile per la mutua esclusione
 */
void Lock (pthread_mutex_t * mtx) {
	if (pthread_mutex_lock (mtx) != 0) {
		/* lo stato dell'applicazione non è piu consistente */
		fprintf (stderr, "Errore durante il locking di una variabile mutex");
		exit (EXIT_FAILURE);
	}
}

/** Esegue l'unlocking sulla variabile mtx

    \param mtx variabile per la mutua esclusione
 */
void Unlock (pthread_mutex_t * mtx) {
	if (pthread_mutex_unlock (mtx) != 0) {
		/* lo stato dell'applicazione non è piu consistente */
		fprintf (stderr, "Errore durante l'unlocking di una variabile mutex");
		exit (EXIT_FAILURE);
	}
}

/**  Chiude la socket gestendo l'errore
 * 	 \param skt, socket da chiudere
 */
void Close_skt (int skt) {
	if (closeSocket (skt) == -1) {
		perror ("Errore durante la chiusura della socket");
		exit (EXIT_FAILURE);
	}
}

/** Lettura dalla socket gestendo l'errore 
 * 
 *  \param skt, socket da cui leggere
 *  \param msg, messaggio da leggere
 * 
 *   \retval n, numero di byte letti
 */
int Receive_skt (int skt, message_t * msg) {
	int n;
	
	n = receiveMessage (skt, msg);
	if (n == -1) {
		perror (ERROR_RECEIVE_MSG);
		Close_skt (skt);
		exit (EXIT_FAILURE);
	}
	return n;
}

/** Scrittura sulla socket gestendo l'errore 
 * 	
 *  \param skt, socket su cui scrivere
 *  \param msg, messaggio da scrivere
 * 
 *  \retval n, numero di byte scritti
 */
int Send_skt (int skt, message_t * msg) {
	int n;
	
	n = sendMessage (skt, msg);
	if (n == -1) {
		perror (ERROR_SEND_MSG);
		Close_skt (skt);
		exit (EXIT_FAILURE);
	}
	return n;
}

/** Funzione che permette di valutare se una stringa contiene solo
 * 	caratteri definiti dal nostro "standard" per il progetto
 * 	
 * 	\param str, stringa da valutare
 * 	\retval 1, se la stringa è ben formata
 * 	\retval	0, se non lo è
 */
int Is_good_str (char * str) {
	int i;
	
	for (i = 0; i < strlen (str); i++) { /* controllo i caratteri inseriti in tutta la stringa */
		if ( (isprint (str [i]) == 0) || (str [i] == '%') ) {
			return 0;
		}
	}
	return 1;
	
}

/** Funzione che permette di valutare se una stringa ripsetta la sintassi
 * 	per l'invio di un messaggio MSG_TO_ONE
 * 
 * 	\param str, stringa da valutare
 * 	\retval 1, se rispetta la sintassi "destinatario messaggio"
 * 	\retval 0, se non la rispetta
 */
int To_one_good_str (char * str) {
	int i = 0;
	int space = 0; /* variabile che indica se è stato trovato un carattere ' ' */
	
	while (i < strlen (str)) {
		if ( str [i] == ' ' ) { /* scorro la stringa fino al primo spazio trovato */
			space = 1;
			break;
		}
		i++;
	}
	
	if (space == 0) { /* non è presente lo spazio che separa destinatario da messaggio
					   * ovvero è stata digitata una stringa del tipo "%ONE destinatario"
					   */
		return 0;
	}
	
	/* space == 1 */
	if (str [i + 1] == '\0') { /* non è presente il messaggio da inviare */
		return 0;
	}
	
	return ( Is_good_str (str) );
	
}

/** Funzione che shifta una stringa di n posizioni verso sinistra
 * 
 * 	\param str, stringa da shiftare
 * 	\param n, valore che indica di quanto shiftare str
 */
void Left_shift (char * str, int n) {
	int i;
	int dim = strlen (str);
	
	for (i = 0; i <= (dim - n); i++) { /* viene eseguito lo shifting a partire da str [n] verso sinistra */
		str [i] = str [i + n];
	}
}
