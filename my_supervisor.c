#include <stdio.h> /* setvbuf FILE _IOLBF BUFSIZ fprintf perror printf */
#include <stdlib.h> /* EXIT_FAILURE exit malloc free strtol */
#include <unistd.h> /* pipe fork close execlp getppid read */
#include <pthread.h> /* pthread_attr_init pthread_attr_setdetachstate pthread_mutex_lock/unlock pthread_exit */
#include <errno.h> /* ERANGE EAGAIN EINTR errno */
#include <sys/wait.h> /* sigemptyset sigaddset sigdelset SIGINT SIGTERM pthread_sigmask kill sigwait sigtimedwait waitpid*/
#include <inttypes.h> /* uint64_t PRIX64 */
#include <limits.h> /* INT_MAX INT_MIN */

#include "utility.h"

#define TRUE 			1
#define FALSE 			0
#define THREAD_SIZE  	0
#define ARG_BUF_LEN 	8

#define handle_error(error, msg) \
		do { errno = error; perror(msg); exit(EXIT_FAILURE); } while (0)

/*STRUTTURE DATI*/

/* messaggi ricevuti dai server */
struct msg{
	uint64_t client_ID;
	unsigned long int stima;
};

struct entry{
	uint64_t client_ID;
	unsigned long int stima;
	unsigned int fonti; /* numero di server da cui si è ricevuta una stima*/
	struct entry * next;
};

/* archivio delle stime: head serve per far si che tutti i thread sappiano dove comincia la lista, tail serve per inserire più comodamente in coda */
struct archivio{
	struct entry *head;
	struct entry *tail;
};

/* PROTOTIPI */
void aggiorna_archivio(unsigned long int stima, uint64_t ID);
void* addetto_connessioni(void *arg);
void stampa_archivio(FILE *stream);
void chiudi_servers(void);
void attendi_servers(void);
void stermina_archivio(void);

/*VARIABILI GLOBALI*/
int **pipes, *serverpid, num_of_server;
pthread_mutex_t arch_mut = PTHREAD_MUTEX_INITIALIZER;
struct archivio arch;

int main(int argc, char* argv[]){

	int i, error, sig, *thread_arg;
	char serverid[ARG_BUF_LEN], serversupfd[ARG_BUF_LEN];
	sigset_t sig_set, full_set;
	struct timespec onesec;
	pthread_t tpid;
	pthread_attr_t attr;

	if(setvbuf(stdout, NULL, _IOLBF, BUFSIZ) != 0){
		exit(EXIT_FAILURE);
	}
	if(setvbuf(stderr, NULL, _IOLBF, BUFSIZ) != 0){
		exit(EXIT_FAILURE);
	}

	if (argc != 2) {
		fprintf(stderr, "supervisor: wrong number of arguments\n");
		exit(EXIT_FAILURE);
	}

	/* inizializzo la struttura che conterrà i dati dei server */
	arch.head = NULL;
	arch.tail = NULL;

	errno = 0; /* se strtol restituisce un valore fuori range setta errno ad ERANGE */
	num_of_server = strtol(argv[1], NULL, 10); /* converto il numero di server da lanciare da stringa ad intero */
	if ((errno == ERANGE && (num_of_server == INT_MAX || num_of_server == INT_MIN)) || (errno != 0 && num_of_server == 0)){
		fprintf(stderr, "supervisor: argv[1] out of range.\n");
		exit(EXIT_FAILURE);
	}

	fprintf(stdout, "SUPERVISOR STARTING %d\n", num_of_server);

	/* alloco lo spazio per i pid dei server e per le pipe anonime di comunicazione fra server e supervisor dopodichè apro le pipe */
	if ((serverpid = (int *)malloc(num_of_server * sizeof(int))) == NULL) {
		fprintf(stderr, "supervisor: serverpid's malloc failed\n");
		exit(EXIT_FAILURE);
	}
	if ((pipes = (int **)malloc(num_of_server * sizeof(int*))) == NULL) {
		fprintf(stderr, "supervisor: pipes' malloc failed\n");
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < num_of_server; i++) {
		if (! (pipes[i] = (int *)malloc(2 * sizeof(int)))) {
			fprintf(stderr, "supervisor: i-th pipes' malloc failed\n");
			exit(EXIT_FAILURE);
		}
		if (pipe(pipes[i]) == -1) {
			perror("supervisor: couldnt open pipes");
			exit(EXIT_FAILURE);
		}
	}
	
	/* Inizio a lanciare i server dopo aver bloccato i loro SIGINT e SIGTERM (erediteranno la mask dal padre/supervisor) */
	sigemptyset(&sig_set);
	sigaddset(&sig_set, SIGINT);
	sigaddset(&sig_set, SIGTERM);

	if ((error = pthread_sigmask(SIG_BLOCK, &sig_set, NULL))!= 0)
		handle_error(error,"supervisor: sigmask(sigint) failed");

	/* da qui in poi i server saranno creati quindi preparo gli handler per il cleanup in caso di exit */
	if (atexit(&attendi_servers)) {
		fprintf(stderr,"supervisor: atexit(attendi) failed");
	}
	if (atexit(&chiudi_servers)) {
		fprintf(stderr,"supervisor: atexit(chiudi) failed");
	}

	for (i = 0; i < num_of_server; i++) {
		/* converto da interi a stringhe il server-id ed il filedescriptor per la comunicazione tramite pipe anonima
		   tra server e supervisor, questi saranno passati come paramentri alla chiamata di ./my_server tramite il buffer che ho preparato*/
		error = snprintf(serverid, ARG_BUF_LEN, "%d", i+1);
		if (error <= 0 || error >= ARG_BUF_LEN ) {
			fprintf(stderr, "snprintf(serverid) failed\n");
			exit(EXIT_FAILURE);
		}
		error = snprintf(serversupfd, ARG_BUF_LEN, "%d", pipes[i][1]);
		if (error <= 0 || error >= ARG_BUF_LEN) {
			fprintf(stderr, "snprintf(serverid) failed\n");
			exit(EXIT_FAILURE);
		}

		/* i server chiudono l'estremità in lettura delle loro pipe, il supervisor quelle in scrittura */
		switch (serverpid[i] = fork()) {
			case 0: /* server */
				if (close(pipes[i][0]) == -1) {
					perror("failed to close reading fd of server");
				}
				execlp("./my_server", "my_server", serverid, serversupfd, (char*)NULL);
				perror("supervisor: failed execlp");
				exit(EXIT_FAILURE);

			case -1: /* supervisor error */
				perror("supervisor: failed fork");
				exit(EXIT_FAILURE);

			default: /* supervisor success*/
				if (close(pipes[i][1]) == -1) {
					perror("failed to close writing fd of supervisor");
				}
				break;
		}
	}

	
	/* 	I thread moriranno quando dirò ai loro server di chiudersi, non avendo intenzione di fare join su di loro
		li creo DETACHED  */
	if( (error = pthread_attr_init(&attr)) != 0)
		handle_error(error,"server: pthread_attr_init failed");
	
	if((error = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) != 0)
		handle_error(error,"server: pthread_attr_setdetachstate failed");
		
	/*	Diminuisco lo stack (da 2MB, su Linux, a 64kB) di default per thread poichè numero_thread_supervisor = numero_server+1 */
	/*pthread_attr_setstacksize(&attr, THREADSTACK);*/
	if((error = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + THREAD_SIZE))!=0)
			handle_error(error,"server: pthread_attr_setstacksize failed");

	/* lancio i thead che si occuperanno di raccogliere i dati dai server */
	for (i = 0; i < num_of_server; i++) {
		/*alloco spazio per l'argomento da passare al thread*/
		if ((thread_arg = malloc(sizeof(int))) == NULL) {
			fprintf(stderr, "supervisor: listener_arg's malloc failed");
			exit(EXIT_FAILURE);
		}
		/* l'argomento è il numero della pipe anonima di cui occuparsi */
		*thread_arg = i;

		/* creo il thread con attributo detached */
		if (( error = pthread_create(&tpid, &attr, &addetto_connessioni, (void*)thread_arg)) != 0)
			handle_error(error,"supervisor: pthread_create(addetto_connessioni) failed");
	}

	/* Gestione segnali */
	/* Inizializzo la struttura necessaria per l'attesa di un secondo */
	onesec.tv_sec = 1;
	onesec.tv_nsec = 0;

	/* mi interessa gestire SIGINT e SIGCHLD */
	sigdelset(&sig_set, SIGTERM);
	sigaddset(&sig_set, SIGCHLD);

	/* creo un set a parte per bloccare tutto e non essere interrotto mentre attendo il secondo SIGINT */
	sigfillset(&full_set);
	
	while(1){
		/* mi metto in attesa di ricevere un SIGINT o SIGCHLD */
		if ( (error = sigwait(&sig_set, &sig)) != 0 )
			handle_error(error,"supervisor: sigwait failed");
		
		switch (sig) {
			case SIGINT:	stampa_archivio(stderr);
							/* blocco tutti i segnali per evitare di essere interrotto mentre attendo per un secondo il secondo SIGINT*/
							if (pthread_sigmask(SIG_BLOCK, &full_set, NULL))
								handle_error(error,"supervisor: sigmask(sigint) failed");
								
							if (sigtimedwait(&sig_set, NULL, &onesec) == -1) {
								switch(errno){
									case EAGAIN:	/* nessun secondo SIGINT ricevuto, riattivo i segnali ( eccetto SIGINT e SIGCHLD ) e proseguo */
													if (pthread_sigmask(SIG_BLOCK, &sig_set, NULL))
														handle_error(error,"supervisor: sigmask(sigint) failed");
													break;
									case EINVAL:	perror("supervisor: sigtimedwait");
													exit(EXIT_FAILURE);
								}
							} else {
								/* se arrivo qui vuol dire che ho ricevuto un secondo SIGINT dopo meno di un secondo */
								/* innanzitutto riattivo i segnali che avevo bloccato prima (ora sono bloccati solo SIGINT e SIGCHLD)*/
								if (pthread_sigmask(SIG_BLOCK, &sig_set, NULL)) {
									perror("supervisor: sigmask(sigint) failed");
									exit(EXIT_FAILURE);
								}

								stampa_archivio(stdout);
								stermina_archivio(); /* libero la memoria dell'archivio prima di uscire */
								fprintf(stdout, "SUPERVISOR EXITING\n");
								exit(EXIT_SUCCESS);
							}
							break;
			
			case SIGCHLD:	/* i server terminano solo quando invio loro il SIGTERM, se ricevo adesso un SIGCHLD qualcosa è andato storto */
							fprintf(stderr, "Supervisor: server failure\n");
							exit(EXIT_FAILURE);
							break;
	
		}
	}

	return 0; /*Mai raggiunta */
}

void aggiorna_archivio(unsigned long int stima, uint64_t ID){

	int found = FALSE;
	struct entry *tmp, *nuovo;

	/* faccio la malloc fuori perchè non voglio avere errori mentre ho il lock */
	if((nuovo = (struct entry *)malloc(sizeof(struct entry))) == NULL ){
			fprintf(stderr, "malloc failed\n");
			exit(EXIT_FAILURE);
		}
	
	pthread_mutex_lock(&arch_mut);

	tmp = arch.head; /* inizializzo il mio puntatore temporaneo al primo elemento dell'archivio */

	/*scorro la lista finchè non trovo una entry con lo stesso ID o la lista finisce*/
	while(!found && tmp != NULL ){
		if(tmp->client_ID == ID)
			found = TRUE;
		else
			tmp = tmp->next;
		
	}
	/*se non ho trovato una entry con lo stesso ID inserisco i miei dati in "nuovo" e lo inserisco nella lista*/
	if(!found){
		nuovo->client_ID = ID;
		nuovo->stima = stima;
		nuovo->fonti = 1;
		nuovo->next = NULL;
		if(arch.head == NULL) arch.head = nuovo;
		if(arch.tail != NULL) (arch.tail)->next = nuovo;
		arch.tail = nuovo;
	}
	else { /* altrimenti elimino nuovo, aumento il numero delle fonti per quella stima ed eventualmente aggiorno la stima */
		free(nuovo);
		(tmp->fonti)++;
		if(tmp->stima > stima) tmp->stima = stima;
	}

	pthread_mutex_unlock(&arch_mut);
}

void* addetto_connessioni(void *arg){
	int server, readres, server_vivo = TRUE;
	struct msg messaggio;
	
	server = *(int*)arg;
	free(arg);

	/* looppo fintanto che il serrver a cui sono dedicato è attivo */
	while (server_vivo) {
		switch (readres = readall(pipes[server][0], &messaggio, sizeof(struct msg))) {
			case -1:	exit(EXIT_FAILURE); /* la mia read è fallita, nulla ha più senso ormai */
						break;

			case 0: 	server_vivo = FALSE; /* termino il loop */
						close(pipes[server][0]); /* chiudo la pipe con il server */
						pthread_exit(NULL);

			default:	fprintf(stdout, "SUPERVISOR ESTIMATE %lu FOR %"PRIX64" FROM %d\n", messaggio.stima, messaggio.client_ID, server+1); /*Server+1 perchè io li conto da zero, ma sarebbero a partire da 1*/
						aggiorna_archivio(messaggio.stima, messaggio.client_ID);
						break;
		}
	}
	
	pthread_exit(NULL);
}

void stampa_archivio(FILE *stream){
	struct entry *tmp;

	pthread_mutex_lock(&arch_mut);

	tmp = arch.head;

	while(tmp != NULL){
		fprintf(stream, "SUPERVISOR ESTIMATE %lu FOR %"PRIX64" BASED ON %u\n", tmp->stima, tmp->client_ID, tmp->fonti);
		tmp = tmp->next;
	}

	pthread_mutex_unlock(&arch_mut);
}

void chiudi_servers(void){
	int i;
	for (i = 0; i < num_of_server; i++) {
		kill(serverpid[i], SIGTERM); /* male che vada finiamo di uscire */
	}
}

void attendi_servers(void){
	int i;
	for (i = 0; i < num_of_server; i++) {
		waitpid(serverpid[i], NULL, 0);
	}
}

void stermina_archivio(){
	struct entry *tmp = arch.head;
	struct entry *coldbloodedkiller;
	
	while(tmp != NULL){
		coldbloodedkiller = tmp;
		tmp = tmp -> next;
		free(coldbloodedkiller);
	}
}