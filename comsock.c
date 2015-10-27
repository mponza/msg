/**
   \file comsock.c
   \author Marco Ponza
   \brief  libreria per la comunicazione su socket
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
#include <signal.h>

typedef struct {
    char type;           /** tipo del messaggio */
    unsigned int length; /** lunghezza in byte */
    char* buffer;        /** buffer messaggio */
} message_t; 

/** lunghezza buffer indirizzo AF_UNIX */
#define UNIX_PATH_MAX    108

/** fine dello stream su socket, connessione chiusa dal peer */
#define SEOF -2
/** Error Socket Path Too Long (exceeding UNIX_PATH_MAX) */
#define SNAMETOOLONG -11 
/** numero di tentativi di connessione da parte del client */
#define  NTRIALCONN 5
/** tipi dei messaggi scambiati fra server e client */
/** richiesta di connessione utente */
#define MSG_CONNECT        'C'
/** errore */
#define MSG_ERROR          'E' 
/** OK */
#define MSG_OK             '0'
/** NO */
#define MSG_NO             'N' 
/** messaggio a un singolo utente */
#define MSG_TO_ONE         'T' 
/** messaggio in broadcast */
#define MSG_BCAST          'B' 
/** lista utenti connessi */
#define MSG_LIST           'L' 
/** uscita */
#define MSG_EXIT           'X' 

/* -= FUNZIONI =- */
/** Crea una socket AF_UNIX
 *  \param  path pathname della socket
 *
 *  \retval s    il file descriptor della socket  (s>0)
 *  \retval SNAMETOOLONG se il nome del socket eccede UNIX_PATH_MAX
 *  \retval -1   in altri casi di errore (sets errno)
 *
 *  in caso di errore ripristina la situazione inziale: rimuove eventuali socket create e chiude eventuali file descriptor rimasti aperti
 */
int createServerChannel(char* path)
{
		struct sockaddr_un sa;
		int fd_socket;
		
		errno = 0;
		
		if (path == NULL) {
			errno = EINVAL;
			return -1;
		}

		if (strlen (path) > UNIX_PATH_MAX) {
			return SNAMETOOLONG;
		}
		
		/* Se il socket c'è già, viene rimosso */
		unlink (path);
		
		/* Settaggio indirizzo socket */
		strncpy (sa.sun_path, path, UNIX_PATH_MAX);
		sa.sun_family = AF_UNIX;
		
		
		/***************************************************************/
		/** ==================== Creazione socket ==================== */
		/***************************************************************/
		
		fd_socket = socket (AF_UNIX, SOCK_STREAM, 0);
		if (fd_socket == -1) {
			/* errno settato all'interno della funzione socket */
			return -1;
		}	
		if (bind (fd_socket, (struct sockaddr *) &sa, sizeof (sa)) == -1) { 
			/* errno viene settato all'interno della funzione bind */
			unlink (path);
			close(fd_socket);
			return -1;
		}	
		if(listen (fd_socket, SOMAXCONN) == -1) {
			/* errno viene settato all'interno della funzione listen */
			unlink (path);
			close(fd_socket);
			return -1;
		}
		
		return fd_socket;
}

/** Chiude una socket
 *   \param s file descriptor della socket
 *
 *   \retval 0  se tutto ok, 
 *   \retval -1  se errore (sets errno)
 */
int closeSocket(int s)
{	
		if (close (s) == -1) {
			/* errno settato da close */
			return -1;
		}
		
		return 0;
}

/** accetta una connessione da parte di un client
 *  \param  s socket su cui ci mettiamo in attesa di accettare la connessione
 *
 *  \retval  c il descrittore della socket su cui siamo connessi
 *  \retval  -1 in casi di errore (sets errno)
 */
int acceptConnection(int s)
{
	int fd_c;
	
	errno = 0;
	
	if (s < 0) {
		errno = EINVAL;
		return -1;
	}
	
	fd_c = accept (s, NULL, 0);
	if (fd_c == -1) {
		/* errno settata da accept */
		return -1;
	}
	
	return fd_c;
}

/** legge un messaggio dalla socket
 *  \param  sc  file descriptor della socket
 *  \param msg  struttura che conterra' il messagio letto 
 *		(deve essere allocata all'esterno della funzione,
 *		tranne il campo buffer)
 *
 *  \retval lung  lunghezza del buffer letto, se OK 
 *  \retval SEOF  se il peer ha chiuso la connessione 
 *                   (non ci sono piu' scrittori sulla socket)
 *  \retval  -1    in tutti gl ialtri casi di errore (sets errno)
 *      
 */
int receiveMessage(int sc, message_t * msg)
{
	/* acquisizione della lunghezza della stringa
	 * settaggio del campo type
	 * eventuale allocazione del buffer e inserimento in esso della stringa
	 */
	
	unsigned int lungtot; /* lunghezza complessiva del messaggio */
	int lr; /* intero per gestire il valore di ritorno della funzione read */
	char type;
	
	errno = 0;

	lr = read (sc, &lungtot, sizeof (unsigned int)); /* lettura di un unsigned int */

	if (lr == 0) { /* errno settata da read */
		return SEOF;
	}
	if (lr < 0) {
		return -1;
	}

	/* lettura della tipologia del messaggio (lettura di un carrattere) */
	lr = read (sc, &type, sizeof (char));

	if (lr == 0) { /* errno settata da read */
		return SEOF;
	}
	if (lr < 0) {
		return -1;
	}
	
	msg->type = type; /* settaggio del tipo di messaggio */
	
	if ( (lungtot == 1) ) { /* ovvero type può essere solo un tipo dei seguenti: MSG_OK, MSG_ NO, MSG_EXIT */
			msg->buffer = NULL;
			msg->length = 0;
			return 0;	
	}
	
	/* Altrimenti type è un tipo dei seguenti: MSG_CONNECT, MSG_ERROR, MSG_LIST, MSG_TO_ONE, MSG_BCAST */
	if ( (msg->type == MSG_CONNECT) || (msg->type == MSG_ERROR) || (msg->type == MSG_LIST) || 
		(msg->type == MSG_TO_ONE) || (msg->type == MSG_BCAST) ) {
			
		msg->buffer = malloc (sizeof (char) * (lungtot - 1)); /* legge (lungtot - 1) in quanto 1 carattere è gia stato letto */
		if (msg->buffer == NULL) { /* errno settata da malloc */
			return -1;
		}
		
		lr = read (sc, msg->buffer, sizeof (char) * (lungtot - 1));

		if (lr == 0) {
			return SEOF;
		}
		if (lr < 0) {
			return -1;
		}
		
		/* altrimenti err contiene la lunghezza del buffer */
		msg->length = lr;	
		return lr;
	}

	/* se non è nessuno dei tipi visti sopra */
	errno = EINVAL;
	return -1;
}

/** scrive un messaggio sulla socket
 *   \param  sc file descriptor della socket
 *   \param msg struttura che contiene il messaggio da scrivere 
 *   
 *   \retval  n    il numero di caratteri inviati (se scrittura OK)
 *   \retval  SEOF se il peer ha chiuso la connessione 
 *                   (non ci sono piu' lettori sulla socket) 
 *   \retval -1   in tutti gl ialtri casi di errore (sets errno)
 
 */
int sendMessage(int sc, message_t *msg)
{	
	/* Invio della lunghezza totale della stringa (lunghezza buffer + 1 per il carattere che identifica la tipologia del messaggio), 
	 * eventuale concatenzione del buffer con il tipo di messaggio da inviare e invio della stringa concatenata .
	 */

	int n;
	int i;
	char * str_msg;
	
	unsigned int lungtot = 1; /* la lunghezza totale è almeno 1 in quanto si deve sempre inviare il carattere che identifica il tipo di messaggio */
	
	errno = 0;
	
	if (msg == NULL) {
		errno = EINVAL;
		return -1;
	}
	
	if (msg->length > 0)
	{ /* il buffer non è vuoto */
		lungtot += msg->length;
		
		n = write (sc, &lungtot , sizeof (unsigned int));
		if (n == -1) {
			return SEOF;
		}
		if (n < -1) {
			return -1;
		}

		/* creazione della stringa da inviare sulla socket (il primo carattere sarà il tipo di messaggio) */
		str_msg = malloc (sizeof (char) * ((msg->length) + 1)); /* msg->message + il carattere string connect */

		/* qui non c arriva :( */
		
		if (str_msg == NULL) {
			return -1;
		}
				
		str_msg [0] = msg->type;

		for (i = 1; i <= (msg->length); i++) {

			str_msg [i] = (msg->buffer) [i - 1];
		}
		
		n = write (sc, str_msg, lungtot);
		
		free (str_msg);
		
		if (n == -1) {
			return SEOF;
		}
		if (n < -1) {
			return -1;
		}
		
		return n;
	}
	
	
	/** la lunghezza del buffer è = 0 quindi può essere: MSG_EXIT, MSG_LIST, MSG_OK */

	n = write (sc, &lungtot , sizeof (unsigned int));
	if (n == -1) {
		/* il peer si è disconnesso */
		return SEOF;
	}
	if (n < -1) {
		return -1;
	}
	
	n = write (sc, (char *) &(msg->type), 1);

	if (n == -1) {
		return SEOF;
	}
	if (n < -1) {
		return -1;
	}
	
	return n;
	
	/* caso in cui non corrisponda a nessun tipo di messaggio tra quelli elencati */
	errno = EINVAL;
	return -1;
}

/** crea una connessione alla socket del server. In caso di errore funzione tenta NTRIALCONN volte la connessione (a distanza di 1 secondo l'una dall'altra) prima di ritornare errore.
 *   \param  path  nome del socket su cui il server accetta le connessioni
 *   
 *   \return fd il file descriptor della connessione
             se la connessione ha successo
 *   \retval SNAMETOOLONG se il nome del socket eccede UNIX_PATH_MAX
 *   \retval -1 negli altri casi di errore (sets errno)
 *
 *  in caso di errore ripristina la situazione inziale: rimuove eventuali socket create e chiude eventuali file descriptor rimasti aperti
 */
int openConnection(char* path)
{
		int fd_skt, i = 0;
		struct sockaddr_un sa;
		
		errno = 0;
		
		if (path == NULL) {
			errno = EINVAL;
			return -1;
		}	

		if (strlen (path) > UNIX_PATH_MAX) {
			errno = EINVAL;
			return SNAMETOOLONG;
		}
		
		/* Settaggio indirizzo socket */
		strncpy (sa.sun_path, path, UNIX_PATH_MAX);
		sa.sun_family = AF_UNIX;
		
		fd_skt = socket (AF_UNIX, SOCK_STREAM, 0);

		/* Connessione alla socket del server */
		while ( (connect (fd_skt, (struct sockaddr *) &sa, sizeof (sa)) == -1) && (i < NTRIALCONN) ) {
			i++;
			sleep (1);
		}

		if (i == NTRIALCONN) {
			/* errno settato da connect */
			errno = EINVAL;
			closeSocket (fd_skt); /* se avvengono errori errno viene settato da closeSocket */
			return -1;
		}

		/* se la connect setta errno ma in seguito riesce a connettersi il valore di errno non è piu significativo */
		errno = 0;
		
		return fd_skt;
}
