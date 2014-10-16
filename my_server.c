#include <stdio.h> /* setvbuf FILE _IOLBF BUFSIZ fprintf prperror printf */
#include <stdlib.h> /* EXIT_FAILURE EXIT_SUCCESS exit malloc free */
#include <pthread.h> /* pthread_attr_init pthread_attr_setdetachstate pthread_exit */
#include <signal.h> /* sigemptyset sigaddset sigwait */
#include <unistd.h> /* unlink sleep write read close */
#include <arpa/inet.h> /* ntohl */
#include <sys/un.h> /* sockaddr_un */
#include <errno.h> /* ERANGE EINTR EINVAL errno */
#include <inttypes.h> /* PRIX64 */
#include <sys/time.h> /* gettimeofday */
#include <limits.h> /* ULONG_MAX INT_MAX INT_MIN*/

#include "utility.h"

#define MEXSIZE 		8
#define TRUE 			1
#define FALSE 			0
#define UNIX_PATH_MAX 	108
#define THREAD_SIZE  	0

#define handle_error(error, msg) \
               do { errno = error; perror(msg); exit(EXIT_FAILURE); } while (0)

/* NOTA i SIGINT sono bloccati poichè hanno ereditato la maschera dal supervisor */

/*STRUTTORE DATI*/

/* messaggi da inviare al supervisor */
struct msg{
	uint64_t client_ID;
	unsigned long int stima;
};

/*PROTOTIPI*/
void* gestore_segnali(void* arg);
void* addetto_connessioni(void* arg);
void close_supfd(void);
void unlink_socket(void);
uint64_t convert_ID(uint64_t ID);

/*VARIABILI GLOBALI*/
struct sockaddr_un sa; /* socket usata dal server per ricevere dati dai client */
int supfd; /* fd per comunicare con supervisor */
int ID; /* ID del server che sarà ricevuto tra gli argomenti */

int main(int argc, char* argv[]){
	pthread_t sig_thread, conn_thread;
	pthread_attr_t attr;
	int *thread_arg, fd_skt, fd_client, error;

	if(setvbuf(stdout, NULL, _IOLBF, BUFSIZ) != 0){
		exit(EXIT_FAILURE);
	}
	if(setvbuf(stderr, NULL, _IOLBF, BUFSIZ) != 0){
		exit(EXIT_FAILURE);
	}

	if (argc != 3) {
		fprintf(stderr, "server: wrong number of arguments\n");
		exit(EXIT_FAILURE);
	}

	errno = 0; /* se strtol restituisce un valore fuori range setta errno ad ERANGE */
	ID = strtol(argv[1], NULL, 10); /* ID del server */
	if ((errno == ERANGE && (ID == INT_MAX || ID == INT_MIN)) || (errno != 0 && ID == 0)){
		fprintf(stderr, "server: strtol(argv[1]) failed\n");
		exit(EXIT_FAILURE);
	}
	supfd = strtol(argv[2], NULL, 10); /* fd per comunicare col supervisor */
	if ((errno == ERANGE && (supfd == INT_MAX || supfd == INT_MIN)) || (errno != 0 && supfd == 0)){
		fprintf(stderr, "server: strtol(argv[2]) failed\n");
		exit(EXIT_FAILURE);
	}

	fprintf(stdout,"SERVER %3d ACTIVE\n", ID);

	/* preparo le funzioni di cleanup da chiamare prima di exit */
	if (atexit(&close_supfd)) {
		fprintf(stderr, "server: atexit(close_supfd) failed");
	}
	if (atexit(&unlink_socket)) {
		fprintf(stderr, "server: atexit(unlink_socket) failed");
	}
	
	error = snprintf(sa.sun_path, UNIX_PATH_MAX ,"OOB-server-%d", ID);
	if (error <= 0 || error >= UNIX_PATH_MAX ) {
		fprintf(stderr, "server: snprintf(OOB-server) failed\n");
		exit(EXIT_FAILURE);
	}

	(void)unlink(sa.sun_path);

	sa.sun_family=AF_UNIX;

	/* I thread terminerranno alla chiusura del server, niente join per loro */
	if( (error = pthread_attr_init(&attr)) != 0)
		handle_error(error,"server: pthread_attr_init failed");

	if((error = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) != 0)
		handle_error(error,"server: pthread_attr_setdetachstate failed");

	/*	Diminuisco lo stack (da 2MB, su Linux, a 64kB) di default per thread poichè numero_thread_server = numero_client_connessi+2 */
	/*pthread_attr_setstacksize(&attr, THREADSTACK);*/
	if((error = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + THREAD_SIZE))!=0)
		handle_error(error,"server: pthread_attr_setstacksize failed");

	/* creo un thread per la gestione dei segnali */
	if( (error = pthread_create( &sig_thread , &attr ,  gestore_segnali , NULL)) != 0)
		handle_error(error,"server: pthread_create(gestore segnali) failed");

	/*inizio le fasi di creazione socket e accettazione connessione con client */
	if((fd_skt = socket(AF_UNIX, SOCK_STREAM,0))==-1){
			perror("server: socket's second attempt failed");
			exit(errno);				
	}
	if(bind(fd_skt, (struct sockaddr *)&sa, sizeof(sa))==-1){
			perror("server: bind failed");
			exit(errno);
		}
	
	if(listen(fd_skt,SOMAXCONN)==-1){
			perror("server: listen failed");
			exit(errno);
		}

	/*Label per ritentare l'accept*/
	retry:
	/* attesa di connessioni da parte dei client */
	while( (fd_client = accept(fd_skt,NULL,0)) != -1 )
    {
        fprintf(stdout,"SERVER %d CONNECT FROM CLIENT\n", ID);
        
        if((thread_arg = (int*)malloc(sizeof(int))) == NULL){
			fprintf(stderr, "server:msg malloc failed\n");
			exit(EXIT_FAILURE);
		}
		
		*thread_arg = fd_client;

		/* creo un thread per gestire la connessione e gli passo come argomento il fd per comunicare con client */     
        if( (error = pthread_create( &conn_thread , &attr ,  addetto_connessioni , (void*) thread_arg)) != 0)
			handle_error(error,"server: pthread_create(addetto_connessioni) failed");
			
    }
    switch(errno){
    	case ENETDOWN:
    	case EPROTO:
    	case ENOPROTOOPT:
    	case EHOSTDOWN:
    	case ENONET:
    	case EHOSTUNREACH:
    	case EOPNOTSUPP:
    	case ENETUNREACH:
    	case EAGAIN:
    	case EINTR: goto retry;

    	default: perror("server: accept failed");
    			 exit(EXIT_FAILURE);
    }
	
	return 0; /* mai raggiunto */
}

void* gestore_segnali(void* arg){
	sigset_t set;
	int sig;

	sigemptyset(&set);
	sigaddset(&set,SIGTERM);

	/* mi metto in attesa di ricevere un sigterm */
	while(1){
		if(sigwait(&set, &sig)>0){
				perror("servr: invalid set in sigwait");
				exit(EINVAL); /* unico errore che può generare */
		}
		if(sig == SIGTERM){
				exit(EXIT_SUCCESS);
		}else{
			perror("Server: sigwait wrong signal");
			exit(EXIT_FAILURE);
		}
	}
	pthread_exit(NULL);
}

void* addetto_connessioni(void* arg){
	
	int client_fd = *(int *)arg, nread, first = TRUE; 
	struct timeval latest;
	struct msg *server_msg;
	unsigned long int previous, tmp, stima, min = ULONG_MAX;
	uint64_t client_ID, temp;

	free(arg);
	if((server_msg = (struct msg *)malloc(sizeof(struct msg))) == NULL){
		fprintf(stderr, "server:msg malloc failed\n");
		exit(EXIT_FAILURE);
	}

	while( (nread = readall(client_fd, &temp, MEXSIZE)) > 0){
		if(first == TRUE){ /* primo messaggio ricevuto dal client, inizio previous con il tempo di questa ricezione */
			gettimeofday(&latest, NULL);
			previous = (latest.tv_sec)*1000 + (latest.tv_usec)/1000;
			client_ID = convert_ID(temp); /*L'ID ricevuto è in network byte order, lo converto per stamparlo adeguatamente*/
			first = FALSE;
		} else
			{	gettimeofday(&latest, NULL);
				/* converto latest in millisecondi */
				tmp = (latest.tv_sec)*1000 + (latest.tv_usec)/1000;
				/*calcolo il nuovo secret stimato */
				stima = tmp - previous;
				if(stima < min){ min = stima; }; 
				/*mi salvo l'ultimo tempo di arrivo */
				previous = tmp;
			}
			/* uso previous perchè a questo punto è stato aggiornato al tempo dell'ultima ricezione */
			fprintf(stdout,"SERVER %d INCOMINGFROM %"PRIX64" @ %lu\n", ID , client_ID, previous);
	}

	switch(nread){
		case 0:		if( first == FALSE ){ /*se first è TRUE vuol dire che non ho ricevuto nulla e quindi non devo inviare nulla */
						fprintf(stdout,"SERVER %d CLOSING %"PRIX64" ESTIMATE %lu\n", ID, client_ID, min);
						/*inviamo la stima al supervisor*/
						server_msg->client_ID = client_ID;
						server_msg->stima = min;
						if( writeall(supfd, server_msg, sizeof(struct msg)) == -1){
							perror("client: write failed");
							exit(errno);
						}
					}
					break;
		default:	perror("client: read failed");
					exit(errno);
	}

	if(close(client_fd)==-1){
			perror("client: client_fd close failed");
			exit(errno);
		}

	free(server_msg);

	pthread_exit(NULL);
}

void close_supfd(void){
	if (close(supfd) == -1) {
		perror("server: close_supfd failed");
	}
}

void unlink_socket(void){
	if (unlink(sa.sun_path) == -1) {
		perror("server: unlink_socket failed");
	}
}

/* print_ID sarà: ID se la macchina è big-endian, DI se la macchina è little-endian in modo da stampare lo stesso valore dei server/supervisor*/
uint64_t convert_ID(uint64_t ID){
	unsigned short i;
	uint64_t result;
	unsigned char *res_p = (unsigned char *)&result, *ID_p = (unsigned char *)&ID;

	if(is_littleendian())
	{	
		for(i=0; i < 8; i++) res_p[i] = ID_p[7-i]; /* rovescio */
	} 
	else {
		result = ID;
	}

	return result;
}