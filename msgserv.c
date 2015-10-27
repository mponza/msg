/**
   \file msgserv.c
   \author Marco Ponza
   \brief  server
   Si dichiara che ogni singolo bit presente in questo file è solo ed esclusivamente "farina del sacco" del rispettivo autore :D
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/un.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <signal.h>

#include "genHash.h"
#include "genList.h"
#include "comsock.h"
#include "funserv.h"

/** ========== Macro ========== */
#define DIRSOCK "./tmp"
#define SOCKNAME "./tmp/msgsock"
#define NHASH 10 /* dimensione della tabella hash */
#define NUSR 256 /* lunghezza massima degli username */
#define NWRITE 1024 /* dimensione iniziale del buffer, raddoppiata se il buffer viene riempito piu' la metà */
#define ALREADY_CONNECT "Un utente con il tuo username e' gia' connesso\n"
#define NO_CONNECT "Non sei abilitato alla connessione su questo server\n"
#define ERROR_RECEIVE_MSG "Server Errore nella ricezione del messaggio\n"
#define ERROR_SEND_MSG "Server Errore nell'invio del messaggio\n"
#define CLIENT_DISCONNECT "Server Il client ha chiuso la connessione\n"
#define DEST_DISCONNECT "utente non connesso"
#define WRITE_SLEEP 2

/** ========== Strutture globali ========== */
hashTable_t * hash_table; /* tabella hash, condivisa tra tutti i thread del server */
list_t * thread_list; /* lista che conterrà gli id dei thread */
char * to_write; /* stringa da scrivere sul file di log */
unsigned int dim_wr = NWRITE; /* dimensione effettiva della variabile "to_write" */
char * users_list; /* array che conterrà la lista degli utenti connessi */
int n_worker = 0; /* variabile che indica il numero di worker attivi */

/** ========== Variabili mutex globali ========== */
pthread_mutex_t mtx_thread = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_hash = PTHREAD_MUTEX_INITIALIZER; /* mutex per accedere alla tabella hash da parte di un thread */
pthread_mutex_t mtx_write = PTHREAD_MUTEX_INITIALIZER; /* mutex per accedere alla variabile "to_write" e a "dim_wr" */
pthread_mutex_t mtx_users = PTHREAD_MUTEX_INITIALIZER; /* mutex per accedere alla variabile users_list */
pthread_mutex_t mtx_n = PTHREAD_MUTEX_INITIALIZER; /* mutex per accedere alla variabile n_worker */

void Cleanup_writer (void * log) {
	int n;
	FILE * fd_log = (FILE *) log;
	
	Lock (&mtx_write);
		
		if (to_write [0] != '\0') {
			n = fprintf (fd_log, "%s", to_write);
			fflush (fd_log);
				
			if (n < strlen (to_write)) {
				fprintf (stderr, "Errore durante la scrittura sul file di log");
				exit (EXIT_FAILURE);
			}
				
			Reset_string ();
		}
					
	Unlock (&mtx_write);
	
	fflush (fd_log);
	fclose (fd_log);
	
	return;
}

void Cleanup_worker ( void * arg ) {
	Lock (&mtx_n);
		n_worker--;
	Unlock (&mtx_n);
}

void * Writer (void * name)
{	
	int n;
	int old;
	char * name_log = ((char *) name); /* nome del file di log */
	FILE * fd_log;
	
	Add_thread_list ( pthread_self (), "Writer");
	
	fd_log = fopen (name_log, "w");
	
	pthread_cleanup_push ( Cleanup_writer, fd_log );
	
		while (1) {

			sleep (WRITE_SLEEP);
		
			pthread_setcancelstate ( PTHREAD_CANCEL_DISABLE, &old );
				Lock (&mtx_write);
		
					if (to_write [0] != '\0') {
						n = fprintf (fd_log, "%s", to_write);
						fflush (fd_log);
				
						if (n < strlen (to_write)) {
							fprintf (stderr, "Errore durante la scrittura sul file di log");
							exit (EXIT_FAILURE);
						}
				
						Reset_string ();
					}
					
				Unlock (&mtx_write);
			pthread_setcancelstate ( PTHREAD_CANCEL_ENABLE, &old );
		}
		
	pthread_cleanup_pop (0);

	return NULL;
}

void * Worker (void * fd_socket)
{
	int n; /* variabile per conoscere il numero di caratteri ricevuti  */
	int skt;
	int old; /* necessaria per abilitare/disabilitare la cancel */
	char username [NUSR]; /* username dell'utente connesso tramite questo worker */
	char * dest_username;
	message_t msg;
	pthread_mutex_t * this_cli_mtx; /* puntatore alla variabile mutex dell'elemento nella tabella hash che "conversa" con questo worker*/
	
	skt = * ((int *) fd_socket);
	free (fd_socket);
	Add_thread_list ( pthread_self(), "Worker" );
	
	/* incremento il numero di worker attivi */
	Lock (&mtx_n);
		n_worker++;
	Unlock (&mtx_n);
	
	pthread_cleanup_push ( Cleanup_worker, NULL ); /* procedura di cleanup che decrementerà n_worker all'arrivo di un segnale
												   * di cancellazione
												   */
	
		if ( pthread_detach (pthread_self()) != 0) {
			fprintf (stderr, "Errore durante l'esecuzione di pthread_detach");
			exit (EXIT_FAILURE);	
		} 
	
		pthread_setcancelstate ( PTHREAD_CANCEL_DISABLE, &old ); /* in questo modo garantisco che il thread non lasci
															  * lo stato delle strutture globali in uno stato inconsistente
															  */
	
		/* verifico che il client sia abilitato alla connessione */
			this_cli_mtx = Enable_connect (skt, username);
	
			if (this_cli_mtx == NULL) {
				/* client non puo connettersi a questo server */
				Remove_thread_list (pthread_self ());
				return NULL;
			}
		pthread_setcancelstate ( PTHREAD_CANCEL_ENABLE, &old );
	
		/* client è stato abilitato alla connessione */

		while (1) {
			n = Receive_skt (skt, &msg);
			pthread_setcancelstate ( PTHREAD_CANCEL_DISABLE, &old ); /* disabilito la cancel in modo da esaudire l'eventuale richiesta pendente ricevuta */
		
				/*****************************************************************/
				/** ==================== Fine comunicazione ==================== */
				/*****************************************************************/

				if (n == SEOF || msg.type == MSG_EXIT) {
					Disconnect (pthread_self (), username, skt);	
					return NULL;
				}
		
		
				/*******************************************************************/
				/** ==================== Messaggio di listing ==================== */
				/*******************************************************************/
		
				if (msg.type == MSG_LIST) {
					Lock (&mtx_hash); /* locking della tabella hash, in quanto un altro thread nel frattempo potrebbe aggiornarla */
						Lock (&mtx_users);
							msg.buffer = Listing ();
						Unlock (&mtx_users);
					Unlock (&mtx_hash);
			
					msg.length = strlen ((msg.buffer)) + 1;
			
					Lock (this_cli_mtx);
						Send_skt (skt, &msg);
					Unlock (this_cli_mtx);
	
					free ( (msg.buffer) );
				}
	
	
				/********************************************************************************/
				/** ==================== Messaggio ad uno specifico client ==================== */
				/********************************************************************************/
		
				if (msg.type == MSG_TO_ONE) {
			
					dest_username = Divide_to_one (&msg, username);
			

					if ( Send_to_one (username, dest_username, &msg, skt, this_cli_mtx) == 1) {
					/* è necessario deallocare il buffer */
						free (msg.buffer);
					}
			
					free (dest_username);
				}


				/*********************************************************************/
				/** ==================== Messaggio di broadcast ==================== */
				/*********************************************************************/
		
				if (msg.type == MSG_BCAST) {
			
					Divide_bcast (&msg, username);

					Bcast (&msg, username);
			
					free (msg.buffer);		
				}	
			
			pthread_setcancelstate ( PTHREAD_CANCEL_ENABLE, &old );
		}
		
	pthread_cleanup_pop (0);
	
	return NULL;
}

void * Dispatcher (void * fd_socket)
{
	int skt = * ((int *) fd_socket);
	int fd_cli;
	int * param;
	pthread_t worker;
	
	Add_thread_list ( pthread_self(), "Dispatcher" );
	if ( pthread_detach (pthread_self()) != 0) {
		fprintf (stderr, "Errore durante l'esecuzione di pthread_detach");
		exit (EXIT_FAILURE);	
	} 
	
	while (1) {
		
		fd_cli = acceptConnection (skt);
		
		if (fd_cli == -1) {
			perror ("Errore durante l'esecuzione di \"acceptConnection\"");
			exit (EXIT_FAILURE);
		}
		
		param = malloc (sizeof (int));
		*param = fd_cli;
		
		if (pthread_create (&worker, NULL, Worker, param) != 0) {
			perror ("Errore durante la creazione del thread Worker");
			exit (EXIT_FAILURE);
		}
	}
	
	return NULL;
}

void * Handler (void * asd) {
	int signum;
	sigset_t set;
	elem_t * p;
	elem_t * tmp;
	pthread_t writer; /* id del thread writer (deve esser l'ultimo thread a venir cancellato) */
	
	/******************************************************************/
	/** ========== Setting e attesa dei segnali da gestire ========== */
	/******************************************************************/
	
	if (sigemptyset ( &set ) == -1 ) {
		perror ("Errore durante il mascheramento dei segnali");
		exit (EXIT_FAILURE);
	}
	sigaddset (&set, SIGINT);
	sigaddset (&set, SIGTERM);
	if ( pthread_sigmask (SIG_SETMASK, &set, NULL) == -1 ) {
		fprintf (stderr, "Errore durante il mascheramento dei segnali");
		exit (EXIT_FAILURE);
	}
	/* attesa dell'arrivo di uno dei segnali settati */
	if ( sigwait ( &set, &signum ) != 0) {
		fprintf (stderr, "Errore durante l'esecuzione di sigwait");
		exit (EXIT_FAILURE);
	}
	
	/**************************************************/
	/** ========== Terminazione del server ========== */
	/**************************************************/
	
	Lock (&mtx_thread);
		p = (thread_list->head);
		thread_list->head = NULL;
		
		while (p != NULL) { /* scansione della lista dei thread attivi */
			tmp = p->next;
			
			if ( strcmp ( (p->payload), "Writer") == 0 ) { /* il writer dovrà terminare solo alla fine */
				writer = *( (pthread_t *) (p->key)); /* salvo l'id del writer */
				free (p->key);
				free (p->payload);
				free (p);
			} else {
				pthread_cancel ( *( (pthread_t *) (p->key)));
				free (p->key);
				free (p->payload);
				free (p);
			}
			p = tmp;
		}
	Unlock (&mtx_thread);
	
	/* faccio una seconda scansione in quanto, mentre veniva fatta la prima, qualche thread poteva essersi
	 * bloccato sulla Lock (&mtx_thread) che non è un punto di cancellazione e quindi abbia aggiunto il suo
	 * id alla lista dei thread attivi
	 */
	Lock (&mtx_thread);
		p = (thread_list->head);
		thread_list->head = NULL;
		
		while (p != NULL) {
			tmp = p->next;
			/* non ci possono essere writer */
			pthread_cancel ( *( (pthread_t *) (p->key)));
			free (p->key);
			free (p->payload);
			free (p);
			
			p = tmp;
		}
	Unlock (&mtx_thread);
	
	/* attendo finché non ho piu worker attivi */
	Lock (&mtx_n);
	while (n_worker != 0) {
		Unlock (&mtx_n);
		sleep (1);
		Lock (&mtx_n);
	}

	/* tutti i worker sono sicuramente terminati (n_worker == 0)
	 * invio segnale di terminazione al thread writer
	 */
	pthread_cancel ( writer );
		
	return NULL;
	
}

int main (int argc, char * argv [])
{
	int i, skt, n = 0; /* n è la grandezza massima che potrà assumera users_list */
	char buf [NUSR]; /* buffer contenente l'ultimo username letto */
	field_t payload;
	FILE * fp;
	DIR * dp;
	pthread_t disp, writer, handler;
	sigset_t set;
	struct sigaction sa;
	
	if (argc != 3) {
		fprintf(stderr,"L'applicazione msgserv deve essere eseguita come: \"$ msgserv file_utenti_autorizzati file_log\"\n");
		exit (EXIT_FAILURE);
	}

	if ( (fp = fopen (argv [1], "r")) == NULL) {
		fprintf (stderr, "Errore nell'apertura del file degli utenti autorizzati");
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
	
	
	/************************************************************************************/
	/** ==================== Caricamento utenti nella tabella hash ==================== */
	/************************************************************************************/
	
	hash_table = new_hashTable(NHASH, compare_string, copy_string, copy_field, hash_string);
	if (hash_table == NULL) {
		fprintf (stderr, "Errore durante la creazione della tabella hash");
		exit (EXIT_FAILURE);
	}
	
	while ( fgets(buf, NUSR + 1, fp) != NULL ) { /* Viene salvato nel buffer anche '\n' se viene incontrato.
											   * Dopo l'ultimo carattere letto, viene inserito nel buffer il carattere '\0' (se ci sta).
											   */
			
		for (i = 0; buf [i] != '\n' && buf [i] != '\0' && buf [i] != EOF && (i < NUSR); i++) {	
			if ( isalnum(buf [i]) == 0) {
				free_hashTable (&hash_table);
				fclose (fp);
				fprintf (stderr, "Il file degli utenti autorizzati deve contenere solamente stringhe di caratteri alfanumerici di lunghezza inferiore a 256");
				exit (EXIT_FAILURE);
			}
		}
		buf [i] = '\0'; /* in questo modo si può applicare tranquillamente la funzione hash su stringhe */
		n += strlen (buf) + 1; /* + 1 per l'eventuale spazio o carattere terminatore */
		payload.skt = -1;
		
		/* non è necessario inizializzare payload.mtx in quanto sono tutti client disconnessi */
		
		/* si inserisce la stringa dell'username fino a '\0' (compreso per via di strdup) */
		if ( add_hashElement(hash_table, &buf, &payload) == -1 ) { /* all'avvio del server tutti gli utenti hanno
														  * come valore del payload (socket) -1 perché 
														  * ancora non connessi
														  */
			perror ("Errore durante il caricamento degli utenti nella tabella hash");
			free_hashTable (&hash_table);
			exit (EXIT_FAILURE);
		}
	}
	fclose (fp);
	
	
	/**************************************************************************************************/
	/** ==================== Creazione della socket e della rispettiva directory ==================== */
	/**************************************************************************************************/
	
	dp = opendir (DIRSOCK);
	if ( dp == NULL && errno != ENOENT) { /* apertura della directory ha dato un errore diverso dall'errore di non esistenza */
		perror ("Errore nell'apertura della directory ./tmp");
		free_hashTable (&hash_table);
		exit (EXIT_FAILURE);
	}
	
	if (dp == NULL && errno == ENOENT) { /* se la directory non esiste viene creata */
		if ( mkdir (DIRSOCK, 0777) == -1 ) {
			perror ("Errore durante la creazione della directory");
			free_hashTable (&hash_table);
			exit (EXIT_FAILURE);
		}
	}
	closedir (dp);
	errno = 0;
	
	/* in questo punto la directory esiste sicuramente */
	
	skt = createServerChannel(SOCKNAME);
	if (skt < 0) {
		perror ("Errore durante la creazione della socket");
		free_hashTable (&hash_table);
		rmdir (DIRSOCK);
		exit (EXIT_FAILURE);
	}
	
	/*****************************************************************************/
	/** ==================== Creazione del buffer "to_write" ==================== */
	/*****************************************************************************/
	
	to_write = calloc (NWRITE, sizeof (char));
	if (to_write == NULL) {
		perror ("Errore durante la creazione del buffer per la scrittura su file");
		free_hashTable (&hash_table);
		Close_skt (skt); /* aggiunto di recente */
		rmdir (DIRSOCK);
		exit (EXIT_FAILURE);
	}
	
	
	/******************************************************************************/
	/** ==================== Creazione dell'array users_list ==================== */
	/******************************************************************************/
	
	users_list = calloc ( n , sizeof (char) );
	if (users_list == NULL) {
		perror ("Errore durante la creazione del buffer per tener traccia degli utenti connessi");
		free_hashTable (&hash_table);
		Close_skt (skt); /* aggiunto di recente */
		free (to_write);
		rmdir (DIRSOCK);
		exit (EXIT_FAILURE);
	}
	
	
	/******************************************************************************/
	/** ==================== Creazione dei thread del server ==================== */
	/******************************************************************************/
	
	thread_list = new_List ( compare_pthread_t, copy_pthread_t, copy_string );
	if (thread_list == NULL) {
		perror ("Errore durante la creazione della lista dei thread");
		free_hashTable (&hash_table);
		Close_skt (skt); /* aggiunto di recente */
		rmdir (DIRSOCK);
		exit (EXIT_FAILURE);
	}
	
	if ( pthread_create (&disp, NULL, Dispatcher, &skt) != 0) {
		perror ("Errore durante la creazione del thread dispatcher");
		free_hashTable (&hash_table);
		Close_skt (skt); /* aggiunto di recente */
		free_List (&thread_list);
		rmdir (DIRSOCK);
		exit (EXIT_FAILURE);
	}
	
	if (pthread_create (&writer, NULL, Writer, argv [2]) != 0) {
		perror ("Errore durante la creazione del thread writer");
		free_hashTable (&hash_table);
		Close_skt (skt);
		rmdir (DIRSOCK);
		exit (EXIT_FAILURE);
	}
	
	if (pthread_create (&handler, NULL, Handler, NULL) != 0) {
		perror ("Errore durante la creazione del thread writer");
		free_hashTable (&hash_table);
		Close_skt (skt);
		rmdir (DIRSOCK);
		exit (EXIT_FAILURE);
	}
	
	
	pthread_join (handler, NULL);
	pthread_join (writer, NULL);
	
	/* mutua esclusione non necessaria, una volta arrivato qui oltre
	 * al thread main non ci sono altri thread attivi che possono accedere alle
	 * strutture dati globali 
	 */
	Destroy_hash (); 
	free_List (&thread_list);
	
	free ( users_list );
	free (to_write);
	Close_skt (skt);
	
	unlink ( SOCKNAME );
	rmdir (DIRSOCK);

	return 0;
}
