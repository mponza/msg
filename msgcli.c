/**
   \file msgcli.c
   \author Marco Ponza
   \brief  client
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
#include "funcli.h"

/** ========== Macro ========== */
#define SOCKNAME "./tmp/msgsock"
#define NBUFFER 514 /* 256 nome destinatario + 1 spazio + 256 messaggio + 1 '\n' */
#define SERVER_DISCONNECT "Il server si e' disconnesso\n"
#define ERROR_SEND_MSG "Errore nell invio del messaggio"
#define ERROR_RECEIVE_MSG "Client Errore nella ricezione del messaggio"
#define ERR_TYPE -3 /* valore che indica che la stringa inserita su stdin è errata */

/** ========== Variabili globali ========== */
pthread_t handler; /* variabile globale per far terminare l handler in caso di %EXIT */
pthread_t sender; /* variabile globale per far terminare il sender in caso di SIGINT o SIGTERM */
pthread_t receiver; /* variabile globale per far terminare il receiver in caso di SIGINT o SIGTERM */

void * Handler (void * not_used) {

	int signum;
	sigset_t set;
	
	/******************************************************************/
	/** ========== Setting e attesa dei segnali da gestire ========== */
	/******************************************************************/
	
	if (sigemptyset ( &set ) == -1 ) {
		perror ("Errore durante il mascheramento dei segnali");
		exit (EXIT_FAILURE);
	}
	sigaddset (&set, SIGINT);
	sigaddset (&set, SIGTERM);
	sigaddset (&set, SIGUSR1); /* quando deve killare il sender */
	sigaddset (&set, SIGUSR2); /* quando non deve killare nessuno */
	if ( pthread_sigmask (SIG_SETMASK, &set, NULL) == -1 ) {
		fprintf (stderr, "Errore durante il mascheramento dei segnali");
		exit (EXIT_FAILURE);
	}
	/* attesa dell'arrivo di uno dei segnali settati */
	if ( sigwait ( &set, &signum ) != 0) {
		fprintf (stderr, "Errore durante l'esecuzione di sigwait");
		exit (EXIT_FAILURE);
	}
	
	
	/********************************************************/
	/** ========== Gestione dei segnali ricevuti ========== */
	/********************************************************/
	
	if (signum == SIGINT || signum == SIGTERM) {
		/* cancellazione dei thread Sender e receiver */
		pthread_cancel (sender);
		pthread_cancel (receiver);
		return NULL;
	}
	
	if (signum == SIGUSR2) {
		/* non deve killare nessun thread */
		return NULL;
	}
	
	/* signum == SIGUSR1 */
	/* è terminata la connessione con il server */
	pthread_cancel (sender);
	return NULL;
}


void * Sender (void * fd_socket)
{
	int i, n;
	int skt = * ((int *) fd_socket);
	int old;
	char buf [NBUFFER];
	message_t msg;
	char * eof; /* se è stato letto un EOF il suo valore è NULL */
	
	while (1) {
		n = ERR_TYPE;
		
		eof = fgets (buf, NBUFFER + 1, stdin);
		
		pthread_setcancelstate ( PTHREAD_CANCEL_DISABLE, &old );
		
		/***************************************************************************/
		/** ========== Lettura EOF o di un messaggio per disconnettersi ========== */
		/***************************************************************************/
		
		if (eof == NULL || strncmp (buf, "%EXIT", 5) == 0) {
			msg.type = MSG_EXIT;
			msg.length = 0;
			msg.buffer = NULL;

			pthread_kill (handler, SIGUSR2); /* notifico l'handler che non dovrà cancellare il sender perché terminerà da solo */
			
			Send_skt (skt, &msg);
			return NULL;
		}
		
		
		/***********************************************/
		/** ========== Messaggio di listing ========== */
		/***********************************************/
		
		if (strncmp (buf, "%LIST", 5) == 0) {
			msg.type = MSG_LIST;
			msg.length = 0;
			n = Send_skt (skt, &msg);
		}
		
		
		/************************************************************/
		/** ========== Messaggio da inviare ad un client ========== */
		/************************************************************/
		
		if (strncmp (buf, "%ONE ", 5) == 0) {
			buf [strlen (buf) - 1] = '\0'; /* (strlen (buf) - 1) posizione in cui si trova '\n' */
			Left_shift (buf, 5); /* tolgo dal buffer la stringa "%ONE " */
			
			if (To_one_good_str (buf) == 0) { /* stringa digitata non è corretta */
					fprintf (stderr, "\n\n***** WARNING *****\nAttenzione, errato inserimento della stringa.\nPer inviare una richiesta al server digitare:\n\t%%EXIT - per disconnettersi\n\t%%LIST - per ricevere la lista degli utenti connessi al server\nPer inviare un messaggio ad un particolare utente digitare:\n\t%%ONE \"nomeutente\" \"messaggio\"\nPer inviare un messaggio a tutti gli utenti collegati al server digitare semplicemente il messaggio.\nSi ricorda che il messaggio inviato deve contenere solamente carattere stampabili escluso il carattere %%\n\n");	
			} else { /* la stringa seguita da "%ONE " rispetta la nostra sintassi */
				
				msg.type = MSG_TO_ONE;
				msg.length = strlen (buf) + 1;

				msg.buffer = malloc (sizeof (char) * (msg.length));
				
				sprintf (msg.buffer, "%s", buf);
				for (i = 0; (msg.buffer) [i] != ' '; i++);
				msg.buffer [i] = '\0'; /* inserisco il terminatore dopo il nome del destinatario */
				n = Send_skt (skt, &msg);
				
				free (msg.buffer);
			}
		} else { /* a questo punto può essere solamente un messaggio di broadcast */
			
			
		/*************************************************/
		/** ========== Messaggio di broadcast ========== */
		/*************************************************/
		
			if (buf [0] != '%') {
			
				buf [strlen (buf) - 1] = '\0';
			
				if (Is_good_str (buf) == 0) { /* stringa digitata non è corretta */
						fprintf (stderr, "\n\n***** WARNING *****\nAttenzione, errato inserimento della stringa.\nPer inviare una richiesta al server digitare:\n\t%%EXIT - per disconnettersi\n\t%%LIST - per ricevere la lista degli utenti connessi al server\nPer inviare un messaggio ad un particolare utente digitare:\n\t%%ONE \"nomeutente\" \"messaggio\"\nPer inviare un messaggio a tutti gli utenti collegati al server digitare semplicemente il messaggio.\nSi ricorda che il messaggio inviato deve contenere solamente carattere stampabili escluso il carattere %%\n\n");	
				} else {
					msg.type = MSG_BCAST;
					msg.buffer = buf;
					msg.length = strlen (buf) + 1;
					n = Send_skt (skt, &msg);		
				}
			}
		}
		
		if (n == SEOF) { /* il server ha chiuso la connessione */
			pthread_kill (handler, SIGUSR2); /* notifico l'handler che non dovrà cancellare il sender perché terminerà da solo */
			return NULL;
		}
		
		if (n == ERR_TYPE) { /* non è nessuno dei tipi dei messaggi precedenti */
			fprintf (stderr, "\n\n***** WARNING *****\nAttenzione, errato inserimento della stringa.\nPer inviare una richiesta al server digitare:\n\t%%EXIT - per disconnettersi\n\t%%LIST - per ricevere la lista degli utenti connessi al server\nPer inviare un messaggio ad un particolare utente digitare:\n\t%%ONE \"nomeutente\" \"messaggio\"\nPer inviare un messaggio a tutti gli utenti collegati al server digitare semplicemente il messaggio.\nSi ricorda che il messaggio inviato deve contenere solamente carattere stampabili escluso il carattere %%\n\n");	
		}
		
		pthread_setcancelstate ( PTHREAD_CANCEL_ENABLE, &old );
	
	}
	
	return NULL;

}


void * Receiver (void * fd_socket)
{
	int n = 0;
	int skt = * ((int *) fd_socket);
	int old; /* necessaria per abilitare/disabilitare la cancel */
	message_t msg;

	
	while (1) {
		
		n = receiveMessage (skt, &msg);
			
		pthread_setcancelstate ( PTHREAD_CANCEL_DISABLE, &old ); /* in questo modo può visualizzare i messaggi ricevuti 
																	* se nel mentre riceve una pthread_cancel
																	  */
				
				
			/***********************************************/
			/** ========== Connessione conclusa ========== */
			/***********************************************/
				
			if (n == SEOF) {
				/* il server ha chiuso la connessione */
				pthread_kill ( handler, SIGUSR1 ); /* avviso l'handler che, se non ha ricevuto anche SIGUSR2, dovrà killare il sender */
				return NULL;
			}
				
				
			/*********************************************/
			/** ========== Messaggio d'errore ========== */
			/*********************************************/
				
			if (msg.type == MSG_ERROR) {
				printf ("[ERROR] %s\n", msg.buffer);
				fflush (stdout);
			}
					
					
			/***********************************************/
			/** ========== Messaggio di listing ========== */
			/***********************************************/
				
			if (msg.type == MSG_LIST) {
				printf ("[LIST] %s\n", msg.buffer);
				fflush (stdout);
			}
				
				
			/**********************************************************/
			/** ========== Messaggio da parte di un client ========== */
			/**********************************************************/
				
			if (msg.type == MSG_TO_ONE) {
				printf ("%s\n", msg.buffer);
				fflush (stdout);
			}
				
				
			/************************************************************************/
			/** ========== Messaggio dri broadcast da parte di un client ========== */
			/************************************************************************/
				
			if (msg.type == MSG_BCAST) {
				printf ("[BCAST]%s\n", msg.buffer);
				fflush (stdout);
			}
		
			free (msg.buffer);
				
		pthread_setcancelstate ( PTHREAD_CANCEL_ENABLE, &old ); 

	}
	
	return NULL;
	
}


int main (int argc, char * argv [])
{
	int skt, n;
	message_t msg;
	sigset_t set;
	struct sigaction sa;

	if (argc != 2) {
		perror ("L'applicazione msgcli deve essere eseguita come: \"$ msgcli username\"\n");
		exit (EXIT_FAILURE);
	}
	
	if ( (skt = openConnection(SOCKNAME)) < 0 ) {
		perror ("Impossibile comunicare con il server");
		exit (EXIT_FAILURE);
	}
	
	
	/*************************************************************/
	/** ========== Setting della maschera dei segnali ========== */
	/*************************************************************/
	
	if (sigfillset ( &set ) == -1 ) {
		perror ("Errore durante il mascheramento dei segnali");
		exit (EXIT_FAILURE);
	}
	if ( pthread_sigmask (SIG_SETMASK, &set, NULL) == -1 ) {
		fprintf (stderr, "Errore durante il mascheramento dei segnali");
		exit (EXIT_FAILURE);
	}
	/* ignoro sigpipe */ 
	bzero (&sa, sizeof (sa));
	sa.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sa, NULL);

	
	/***************************************************/
	/** ========== Richiesta di connessione ========== */
	/***************************************************/
	
	msg.type = MSG_CONNECT;
	msg.buffer = argv [1];
	msg.length = strlen (argv [1]) + 1;
	
	n = sendMessage (skt, &msg);

	if (n == SEOF) {
		fprintf (stderr, SERVER_DISCONNECT);
		Close_skt (skt);
		exit (EXIT_FAILURE);
	}
	if (n == -1) {
		perror (ERROR_SEND_MSG);
		Close_skt (skt);
		exit (EXIT_FAILURE);
	}

	n = receiveMessage (skt, &msg);
	
	/* Connessione fallita o rifiutata */
	if (n == SEOF) {
		fprintf (stderr, SERVER_DISCONNECT);
		Close_skt (skt);
		exit (EXIT_FAILURE);
	}
	if (n == -1) {
		perror (ERROR_RECEIVE_MSG);
		Close_skt (skt);
		exit (EXIT_FAILURE);
	}
	
	if (msg.type == MSG_ERROR) {
		fprintf (stderr, "%s", msg.buffer);
		free (msg.buffer);
		Close_skt (skt);
		exit (EXIT_FAILURE);
	}
	

	/* Abilitato alla connessione sul server */
	/**********************************************************/
	/** ========== Creazione dei thread del client ========== */
	/**********************************************************/
	
	if ( pthread_create (&sender, NULL, Sender, &skt) != 0 ) {
		fprintf (stderr, "Errore durante la creazione del thread Sender");
		Close_skt (skt);
	}
	
	if ( pthread_create (&receiver, NULL, Receiver, &skt) != 0 ) {
		fprintf (stderr, "Errore durante la creazione del thread Receiver");
		Close_skt (skt);
	}
	
	if ( pthread_create (&handler, NULL, Handler, NULL) != 0 ) {
		fprintf (stderr, "Errore durante la creazione del thread Receiver");
		Close_skt (skt);
	}
	
	
	/*******************************************************************************/
	/** ========== Attesa della corretta terminazione di tutti i thread ========== */
	/*******************************************************************************/
	
	pthread_join ( handler, NULL ); /* caso in cui si riceve una SIGINT o SIGTERM */
	pthread_join ( sender, NULL ); /* caso in cui mentre si sta scrivendo sulla socket il server la chiude */
	pthread_join ( receiver, NULL );
	Close_skt (skt);

	return 0;
}
