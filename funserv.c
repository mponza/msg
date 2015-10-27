/**
   \file funserv.c
   \author Marco Ponza
   \brief  libreria di funzioni per msgserver
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

/** ========== Macro ========== */
#define NHASH 10 /* dimensione della tabella hash */
#define NWRITE 1024 /* dimensione iniziale del buffer, raddoppiata se il buffer viene riempito piu' la metà */
#define ALREADY_CONNECT "Un utente con il tuo username e' gia' connesso\n"
#define NO_CONNECT "Non sei abilitato alla connessione su questo server\n"
#define ERROR_RECEIVE_MSG "Server Errore nella ricezione del messaggio\n"
#define ERROR_SEND_MSG "Server Errore nell'invio del messaggio\n"
#define CLIENT_DISCONNECT "Server Il client ha chiuso la connessione\n"
#define DEST_DISCONNECT "utente non connesso"

/** ========== Strutture globali ========== */
extern hashTable_t * hash_table; /* tabella hash, condivisa tra tutti i thread del server */
extern list_t * thread_list; /* lista che conterrà gli id dei thread */
extern char * to_write; /* stringa da scrivere sul file di log */
extern unsigned int dim_wr; /* dimensione effettiva della variabile "to_write" */
extern char * users_list; /* array che conterrà la lista degli utenti connessi */
extern int n_worker; /* variabile che indica il numero di worker attivi */

/** ========== Variabili mutex globali ========== */
extern pthread_mutex_t mtx_thread;
extern pthread_mutex_t mtx_hash; /* mutex per accedere alla tabella hash da parte di un thread */
extern pthread_mutex_t mtx_write; /* mutex per accedere alla variabile "to_write" e a "dim_wr" */
extern pthread_mutex_t mtx_users; /* mutex per accedere alla variabile users_list */
extern pthread_mutex_t mtx_n; /* mutex per accedere alla variabile n_worker */

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
void * copy_int(void *a) {
  int * _a;

  if ( ( _a = malloc(sizeof(int) ) ) == NULL ) return NULL;

  *_a = * (int * ) a;

  return (void *) _a;
}

/** Funzione che confronta due interi
 *  
 *  \param a, primo intero da confrontare
 *  \param b, secondo intero da confrontare
 *
 *  \retval 0, se sono uguali
 *  \retvale != 0, se sono diversi
 */
int compare_int(void *a, void *b) {
    int *_a, *_b;
    _a = (int *) a;
    _b = (int *) b;
    return ((*_a) - (*_b));
}

/** Funzione che confronta due stringhe
 *  
 *  \param a, primo stringa da confrontare
 *  \param b, secondo stringa da confrontare
 *
 *  \retval 0, se sono uguali
 *  \retvale != 0, se sono diversi
 */
int compare_string(void *a, void *b) {
    char *_a, *_b;
    _a = (char *) a;
    _b = (char *) b;
    return strcmp(_a,_b);
}

/** Funzione che restituisce un puntatore alla copia di una stringa
 *  
 *  \param a, stringa da copiare
 *  \retval _a, puntatore alla copia 
 */
void * copy_string(void * a) {
  char * _a;

  if ( ( _a = strdup(( char * ) a ) ) == NULL ) return NULL;

  return (void *) _a;
}

/** Funzione che restituisce un puntatore alla copia di un pthread_t
 *  
 *  \param a, pthread_t da copiare
 *  \retval _a, puntatore alla copia 
 */
void * copy_pthread_t (void *a) {
  pthread_t * _a;

  if ( ( _a = malloc(sizeof(pthread_t) ) ) == NULL ) return NULL;

  *_a = * (pthread_t * ) a;

  return (void *) _a;
}

/** Funzione che confronta due pthread_t
 *  
 *  \param a, primo pthread_t da confrontare
 *  \param b, secondo pthread_t da confrontare
 *
 *  \retval 0, se sono uguali
 *  \retvale != 0, se sono diversi
 */
int compare_pthread_t (void *a, void *b) {
    pthread_t *_a;
    pthread_t *_b;
    _a = (pthread_t *) a;
    _b = (pthread_t *) b;
    if ( (*_a) == (*_b)) {
		return 0;
	}
    return 1;
}

/** Funzione che restituisce un puntatore alla copia di un field_t
 *  
 *  \param a, field_t da copiare
 *  \retval _a, puntatore alla copia 
 */
void * copy_field (void * a) {
	field_t *_a;
	
	if ( ( _a = malloc (sizeof (field_t)) ) == NULL ) return NULL;
	* _a = * (field_t *) a;
	
	return (void *) _a; 
}

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

/** Procedura che inserisce una stringa nella variabile users_list
 * 
 * 	\param str, stringa da inserire
 */
void Add_user (char * str) {
	if (strlen (users_list) == 0) { /* users_list è vuota */
		sprintf (users_list, "%s", str);
	} else { /* users_list non è vuota */
		strcat (users_list, " ");
		strcat (users_list, str);
	}
}

/** Procedura che elimina una stringa nella variabile users_list
 * 	
 * 	\param str, stringa da rimuovere
 */
void Remove_user (char * str) {
	
		int dim;
		int i;
		
		dim = strlen (users_list) + 1;
		
		if (strstr (users_list, str) != users_list) { /* se l'utente da rimuovere non è il primo nella lista */

			/* sovrascrivo users_list dallo spazio prima di str (strstr (users_list, str) - 1) con la stringa che
			*  inizia dopo la fine di str (strstr (users_list, str) + strlen (str))
			*/
			sprintf (strstr (users_list, str) - 1,  "%s", strstr (users_list, str) + strlen (str));
		
			for (i = 0; users_list [i] != '\0'; i++); /* calcolo da dove partire per ripulire la stringa users_list*/
		
			for (; i < dim; i++) { /* ripulisco la stringa */
				users_list [i] = '\0';
			}
		} else { /* l'utente da rimuovere è il primo nella lista */
		
			if (users_list [strlen (str)] == '\0') { /* è connesso solo l'utente str */
				for (i = 0; i <= strlen (str); i++) { /* ripulisco la stringa */
					users_list [i] = '\0';
				}
				return;
			}
			
			/* ci sono altri utenti connessi presenti in users_list */
			sprintf (users_list,  "%s", users_list + strlen (str) + 1); /* sovrascrivo users_list con users_list meno il primo username */
		
			for (i = 0; users_list [i] != '\0'; i++); /* calcolo da dove partire per ripulire la stringa users_list*/
		
			for (; i < dim; i++) { /* ripulisco la stringa */
				users_list [i] = '\0';
			}
		}
}

/** Procedura che ritorna un puntatore alla copia delle variabile users_list
 * 
 * 	\retval users_list, lista degli utenti connessi
 */
char * Listing () {

	return copy_string (users_list);
}

/** [MTX] Procedura che prende in ingresso 3 stringhe e le concatena
 *  nel formato "mittente:destinatario:messaggio\n" alla variabile to_write
 * 	
 * 	\param mit, mittente
 * 	\param dest, destinatario
 *  \param mess, messaggio
 */
void Add_string (char * mit, char * dest, char * mess) {

	int n;
	char * str;
	
	n = (strlen (mit) + strlen (dest) + strlen (mess) - strlen (mit) - 3 + 4); /* - strlen (mit) per cioe che sta tra [mit]
																				* - 3 per '[', ']', ' '
																				* + 2 per ':', + 1 per '\n', + 1 per '\0'  */
	
	str = malloc (sizeof (char) * n);
	if (str == NULL) {
		perror ("Errore durante l'allocazione della stringa per la concatenazione");
		exit (EXIT_FAILURE);
	}
	sprintf (str, "%s:%s:%s\n", mit, dest, mess + (strlen (mit)) + 3);
	
	Lock (&mtx_write);

		while ( (strlen (to_write) + strlen (str) + 1) >= ((dim_wr) / 2) ) { /* controllo sulla dimensione di to_write */
			/* se la variabile to_write non è abbastanza grande gli si raddoppi la dimensione */
			dim_wr *= 2;
			to_write = realloc (to_write, dim_wr * sizeof (char));
			
			if (to_write == NULL) {
				perror ("Errore durante l'espansione del buffer per la scrittura su file");
				exit (EXIT_FAILURE);
			}
		}	
			
		strcat (to_write, str); /* aggiornamento della variabile to_write */
	
	Unlock (&mtx_write);
	
	free (str);	

}

/** Procedura che ridimensiona al valore NWRITE e resetta la variabile to_write
 */
void Reset_string () {

	free (to_write);
	to_write = calloc (NWRITE, sizeof (char));
	dim_wr = NWRITE;
	
}

/** [MTX] Aggiunge un thread alla lista dei thread attivi

    \param thread_id identificatore del thread
    \param type tipolologia del thread (Dispatcher, Worker, Writer)
 */
void Add_thread_list (pthread_t thread_id, char * type) {
	int err;
	
	Lock (&mtx_thread);
		err = add_ListElement (thread_list, &thread_id, type);
	
		if (err == -1) {
			perror ("Errore durante l'aggiunta del thread alla lista dei thread attivi");
			exit (EXIT_FAILURE);
		}
	Unlock (&mtx_thread);
}

/** [MTX] Rimuove un thread alla lista dei thread attivi (usato esclusivamente dai thread worker)

    \param thread_id identificatore del thread
 */
void Remove_thread_list (pthread_t thread_id) {
	int err;
	
	Lock (&mtx_thread);
		err = remove_ListElement (thread_list, &thread_id);
	
		if (err == -1) {
			perror ("Errore durante la rimozione del thread nella lista dei thread attivi");
			exit (EXIT_FAILURE);
		}
		
		/* decremento il numero di worker attivi */
		Lock (&mtx_n);
			n_worker--;
		Unlock (&mtx_n);
		
	Unlock (&mtx_thread);
}

/**  Chiude la socket gestendo l'errore
 *   \param skt, socket da chiudere
 */
void Close_skt ( int skt) {
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

/** Funzione restituisce un puntatore al payload di un elemento
 *  della tabella hash con key == username
 * 	
 *  \param username, nome utente da cercare
 *  \retval payload, puntatore al payload
 *  \retval NULL, se non è presente nessun elemento nella tabella hash con chiave username
 */
field_t * Field_hash_element (char * key) {
	
	unsigned int i;
	list_t ** l;
	elem_t * elem;
	field_t * payload;

	i = hash_table->hash (key, hash_table->size); /* indice dell'elemento nella tabella hash in cui è possibile ci sia un elem_t con chiave di valore key */
	l = (list_t **) (hash_table->table + i); /* ritorna l'indirizzo della i-esima list_t in cui può essere presente la chiave key */
	if (*l == NULL)
	{
		return NULL;	
	}

	elem = find_ListElement(*l, key);


	if (elem == NULL)
	{
		return NULL;	
	}
	
	
	payload = (field_t *)(elem->payload);
	
	return payload;
}

/** [MTX] Procedura che disconnette un thread da un client che desidera
 *  disconnettersi o che si è gia disconnesso, aggiornando la tabella hash
 *  e la lista dei thread attivi.
 * 
 *  \param thread_id, id del thread che chiama la procedura
 *  \param client, username del client da disconnettere
 * 
 * */
void Disconnect (pthread_t thread_id, char * client, int skt) {
	field_t payload;
	pthread_mutex_t * mtx;

	/** ========== Aggiornamento della tabella hash ========== */
	Lock (&mtx_hash);
	
		/* distruzione della variabile mutex */
		mtx = &((Field_hash_element(client))->mtx);
		if ( pthread_mutex_destroy(mtx) != 0 ) {
			fprintf (stderr, "Errore durante la distruzione della variabile mutex");
			exit (EXIT_FAILURE);
		}
		/* rimuovo e reinserisco l'elemento con payload = -1 */
		if (remove_hashElement (hash_table, client)  == -1) {
			perror ("Errore durante l'aggiornamento della tabella hash");
			exit (EXIT_FAILURE);
		}
		payload.skt = -1;	
		if (add_hashElement (hash_table, client, &payload) == -1) {
			perror ("Errore durante l'aggiornamento della tabella hash");
			exit (EXIT_FAILURE);
		}
	
		/** ========== Rimozione del client dall'array dei client connessi ==========*/
		Lock (&mtx_users);
			Remove_user (client);
		Unlock (&mtx_users);
		
		Close_skt (skt); /* chiusura della socket */
		
	Unlock (&mtx_hash);
	
	
	/** ========= Aggiornamento della lista dei thread attivi ========== */
	Remove_thread_list (thread_id);
}

/** Procedura che distrugge la tabella hash rimuovendo evenutali variabili
 *  di tipo pthread_mutex_t presenti nei payload degli elementi della tabella hash
 * 
 */
void Destroy_hash () {
	
	list_t ** l;
	elem_t * tmp;
	elem_t * p;
	unsigned int i;
	
	
		if (hash_table == NULL) /* *pt non punta a nessuna tabella hash */
		return;

		for(i = 0; i < ((hash_table)->size); i++)
		{
			l = (list_t **) ((hash_table)->table + i); /* l continene l'indirizzo dell'i-esimo elemento (i-esima lista) della tabella hash */
			
			if (*l != NULL) {
				
				p = (*l)->head;
				
				while (p != NULL) {	/* scorro la lista e libero la memoria di ogni elemento */
					tmp = p->next;
					
					if ( ( ((field_t *)(p->payload))->skt ) > -1) { /* se il client è connesso */
						
						/* chiudo la socket */
						Close_skt ( ( ((field_t *)(p->payload))->skt ) );
					
						if ( pthread_mutex_destroy ( &( (field_t *) (p->payload))->mtx  ) != 0 ) { /* distruggo la mutex */
							fprintf (stderr, "Errore durante la distruzione della variabile mutex");
							exit (EXIT_FAILURE);
						}
					}
					
					
					
					free (p->payload);
					free (p->key);
					free (p);
					
					p = tmp;
				}
			}
			free (*l);
			*l = NULL;
		}
		free ((hash_table)->table);
		free (hash_table);
		hash_table = NULL;
		
	
	
}

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
char * Divide_to_one ( message_t * msg, char * mit ) {
	int i, j;
	char * dest; /* username del destinatario */
	char * tmp_buffer; /* buffer temporaneo che conterrà il messaggio effettivo (senza il destinatario) */
	
	dest = malloc ( sizeof (char) * (  strlen (msg->buffer) + 1 )  );
			
	sprintf (dest, "%s", msg->buffer);
						
	tmp_buffer = malloc ( sizeof (char) *  (msg->length - ( strlen (dest) + 1)  )  );
			
	j = 0;
	for (i = (strlen (dest) + 1); i < (msg->length) ; i++) {
		/* inserzione del messaggio effettivo (stringa successiva a "destinatario\0") in tmp_buffer */
		tmp_buffer [j] = (msg->buffer) [i];
		j++;
	}
			
	free (msg->buffer); /* libero il vecchio buffer */
			
	msg->buffer = malloc ( sizeof (char) * (strlen (mit) + strlen (tmp_buffer) + 4) ); /* + 4 per: '[' ']' ' ' '\0' */
		
	sprintf (msg->buffer, "[%s] %s", mit, tmp_buffer);	
	msg->length = strlen (msg->buffer) + 1;	
	
	free (tmp_buffer);
	
	return dest;
}

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
void Divide_bcast ( message_t * msg, char * mit ) {
	int n;
	char * tmp_buffer;
	
	n = (msg->length) + ( strlen (mit) + 3); /* + 3 per '[' ']' ' ' */
	tmp_buffer = malloc ( sizeof (char) *  n  );
	
	sprintf (tmp_buffer, "[%s] %s", mit, (msg->buffer) );
	free (msg->buffer);
	
	msg->length = n;
	msg->buffer = tmp_buffer;
}

/** [MTX] Procedura che invia un messaggio ad un utente destinatario se questo è connesso al server,
 *  o invia al mittente un messaggio d'errore se il destinatario non è conneesso.
 * 
 * 	\param mit, mittente del messaggio
 * 	\param dest, destinatario del messaggio
 *  \param msg, messaggio da inviare
 * 	\param this_cli_mtx, variabile per la mutua esclusione con il mittente
 * 						(in questo modo la complessità dell'invio al mittente è O(1) )
 * 	\retval 1, se è andato tutto a buon fine
 *  \retval 0, se è stato inviato un messaggio d'errore ed buffer "vecchio" (contenuto in msg) è gia stato deallocato
 */
int Send_to_one (char * mit, char * dest, message_t * msg, int mit_skt, pthread_mutex_t * mit_mtx) {
	int dest_skt;
	int k;
	field_t * payload;
	pthread_mutex_t * dest_mtx_skt;
	
	if (strcmp (dest, mit) == 0) { /* se il mittente è lo stesso del destinatario */
				Lock (mit_mtx);
					k = Send_skt (mit_skt, msg);
				Unlock (mit_mtx);
				
				if (k != SEOF) { /* se il destinatario non si è disconnesso nel frattempo */
					Add_string (mit, dest, msg->buffer);
				}
				return 1;
				
	}
	/* il nome del destinatario è diverso da quello del mittente */
	Lock (&mtx_hash);
		/* controllo se il destinatario è presente nella tabella hash */
		payload = Field_hash_element (dest);
				
		if (payload != NULL) { /* è presente nella tabella hash */
		
			dest_skt = payload->skt;
			dest_mtx_skt = &(payload->mtx);
					
			if (dest_skt == -1) { /* il destinatario del messaggio non è connesso */
	Unlock (&mtx_hash);
				
				free (msg->buffer);
						
				msg->type = MSG_ERROR;
				msg->buffer = malloc (sizeof (char) * (strlen (dest) + strlen (DEST_DISCONNECT) + 3) );
				sprintf (msg->buffer, "%s: %s", dest, DEST_DISCONNECT);
				msg->length = strlen (msg->buffer) + 1;
					
				Lock (mit_mtx);
					Send_skt (mit_skt, msg);
				Unlock (mit_mtx);
				
				free (msg->buffer);
				return 0;
			}
					
			/* il destinatario è connesso */
					
			Lock (dest_mtx_skt);
				k = Send_skt (dest_skt, msg);
			Unlock (dest_mtx_skt);
			
			if (k != SEOF) { /* se il destinatario non si è disconnesso nel frattempo */
				Add_string (mit, dest, msg->buffer);
			}
	Unlock (&mtx_hash);
				
		} else { /* l'username del destinatario non è presente nella tabella hash */
					
	Unlock (&mtx_hash);
			
			free (msg->buffer);
			
			msg->type = MSG_ERROR;
			
			msg->buffer = malloc (sizeof (char) * (strlen (dest) + strlen (DEST_DISCONNECT) + 3) );
			
			sprintf (msg->buffer, "%s: %s", dest, DEST_DISCONNECT);
			msg->length = strlen (msg->buffer) + 1;
					
			Lock (mit_mtx);
				Send_skt (mit_skt, msg);
			Unlock (mit_mtx);
			
			free (msg->buffer);

			return 0;
		}
		
	return 1;
}

/** Funzione che restituisce una copia della n-esima stringa contenuta in
 * 	users_list (separata l una dalle altra da uno spazio).
 * 	Per essere usata correttamente n <= al numero di stringhe separate da
 * 	spazio presenti in users_list.
 * 
 * 	\param n, n-esima stringa da restituire come copia
 * 	\retval str, puntatore alla stringa da restituire (allocata all interno della funzione)
 */
char * User (int n) {
	int i;
	char * p;
	char * str;
	
	
	if (n == 0) { /* si deve restituire l'username del primo utente */
		
		for (i = 0; users_list [i] != '\0' && users_list [i] != ' '; i++); /* calcolo la dimensione dell username */
		
		/* i contiene la dimensione dell'username */
		str = calloc ( i + 1, sizeof (char) ); /* i + 1 per il carattere terminatore */
		
		strncat ( str, users_list, i); /* copio i primi i caratteri in str */
		
		return str;
	}
	
	/* non è il primo utente */
	p = users_list;
	for (i = 0; i < n; i++) { /* scorro la stringa fino a reperire il puntatore del primo carattere dell'n-esimo username */
		p = strstr (p, " ");
		p++; 
	}
	
	for (i = 0; p [i] != '\0' && p [i] != ' '; i++); /* calcolo dimensione dell'username */
	
	str = calloc ( i + 1, sizeof (char) ); /* i + 1 per il carattere terminatore */
	
	/* i contiene la dimensione dell'username */
	strncat ( str, p, i); /* copio i primi i caratteri in str */
		
	return str;
	
}

/** Procedura che invia msg a tutti gli utenti connessi
 * 	\param msg, messaggio da inviare
 * 	\param mit, mittente del messaggio da utilizzare per l'aggiornamento
 * 				della variabile to_write
 */
void Bcast (message_t * msg, char * mit) {
	
	int i, n; /* n conterrà il numero di utenti connessi */
	int k;
	int skt; /* socket del destinatario */
	char * str;
	field_t * payload;
	pthread_mutex_t * mtx; /* mutex sulla socket del destinatario */
	
	Lock (&mtx_hash);
		Lock (&mtx_users);
		
		n = 0;
		for (i = 0; i <= strlen (users_list); i++) { /* reperisco il numero di utenti connessi */
			if (users_list [i] == ' ' || users_list [i] == '\0') {
				n++;
			}

		}
		
		for (i = 0; i < n; i++) { /* invio il messaggio ad ogni utente connesso */
			
			str = User (i); /* str conterrà l'username dell'utente i-esimo a cui inviare il messaggio */

			payload = Field_hash_element ( str ); /* reperisco il payload */
			
			skt = payload->skt;
			mtx = &(payload->mtx);
			
			Lock (mtx);
				k = Send_skt (skt, msg);
			Unlock (mtx);
			
			if (k != SEOF) { /* se il client non si è disconnesso nel mentre aggiungo il messaggio inviato al buffer
							  *	che dovrà essere scritto dal Writer
							  */
					Add_string (mit, str, msg->buffer);
			}
			
			free (str);
		}
		
		Unlock (&mtx_users);
	Unlock (&mtx_hash);
}

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
pthread_mutex_t * Enable_connect (int skt, char * username)
{
	int n;
	message_t msg;
	field_t * cpy_p;
	field_t payload;
	pthread_mutex_t * mtx;
	
	n = Receive_skt (skt, &msg);
	if ( n == SEOF ) {
		fprintf (stderr, CLIENT_DISCONNECT);
		Close_skt (skt);
		return NULL;
	}
	
	Lock (&mtx_hash);
	
		sprintf (username, "%s", msg.buffer);
		free (msg.buffer); /* deallocazione del buffer */
	
		cpy_p = find_hashElement (hash_table, username);
		
		if ((cpy_p) == NULL) {
			/* l'username del client non è presente nella tabella hash */
			Unlock (&mtx_hash);
			
			msg.type = MSG_ERROR;
			msg.buffer = NO_CONNECT;
			msg.length = strlen (NO_CONNECT) + 1;
			Close_skt (skt);
			
			return NULL;
		}

		if ((cpy_p->skt) > -1) {
			/* un client con quell username è gia connesso */
			free (cpy_p);
			Unlock (&mtx_hash);
			msg.type = MSG_ERROR;
			msg.buffer = ALREADY_CONNECT;
			msg.length = strlen (ALREADY_CONNECT) + 1;
			Send_skt (skt, &msg);
			Close_skt (skt);
			
			return NULL;
		}

		/* il client può connettersi */
		free (cpy_p);
		
		/** ========== Aggiornamento della tabella hash ========== */
		if (remove_hashElement (hash_table, username)  == -1) {
			perror ("Errore durante l'aggiornamento della tabella hash");
			exit (EXIT_FAILURE);
		}
		
		payload.skt = skt;
		
		if (pthread_mutex_init ( &(payload.mtx), NULL) == -1) {
			perror ("Errore nell inizializzazione della variabile mutex");
			exit (EXIT_FAILURE);
		}

		if (add_hashElement (hash_table, username, &payload) == -1) {
			perror ("Errore durante l'aggiornamento della tabella hash");
			exit (EXIT_FAILURE);
		}
		
		/** ========== Invio del messaggio di conferma abilitazione ========== */
		msg.type = MSG_OK;
		msg.buffer = NULL;
		msg.length = 0;
		
		n = Send_skt (skt, &msg);

		if (n == SEOF) {
			perror (CLIENT_DISCONNECT);
			
			/** ========== Aggiornamento della tabella hash ========== */
			/* distruzione della variabile mutex */
			mtx = &((Field_hash_element (username))->mtx);
			if ( pthread_mutex_destroy(mtx) != 0 ) {
				fprintf (stderr, "Errore durante la distruzione della variabile mutex");
				exit (EXIT_FAILURE);
			}
	
			if (remove_hashElement (hash_table, username)  == -1) {
				perror ("Errore durante l'aggiornamento della tabella hash");
				exit (EXIT_FAILURE);
			}
			payload.skt = -1;
			if (add_hashElement (hash_table, username, &payload) == -1) {
				perror ("Errore durante l'aggiornamento della tabella hash");
				exit (EXIT_FAILURE);
			}
			
			Close_skt (skt);
			Unlock (&mtx_hash);
			return NULL;
		}
		
		mtx = &((Field_hash_element (username))->mtx);
		
		/** ========== Inserzione dell'username del client nell'array dei client connessi ==========*/
		Lock (&mtx_users);
			Add_user (username);
		Unlock (&mtx_users);
		
	Unlock (&mtx_hash);
	
	return mtx;
}
