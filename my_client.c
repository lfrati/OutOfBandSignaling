#include <stdio.h>  /* fprintf perror sprintf */
#include <stdlib.h> /* EXIT_FAILURE exit srand rand malloc atexit strtol */
#include <unistd.h> /* getpid sleep write close */
#include <time.h> /* time nanosleep */
#include <sys/un.h> /*sockaddr_un*/
#include <inttypes.h> /* PRIX64 */
#include <arpa/inet.h> /* socket connect */
#include <errno.h>  /* ERANGE ENOENT EINTR errno*/
#include <limits.h> /*INT_MAX INT_MIN*/

#include "utility.h"

#define MAXSCRT 3000
#define MEXSIZE 8
#define TRUE 1
#define FALSE 0
#define UNIX_PATH_MAX 108

/*PROTOTIPI*/
int check(int target, int array[], int dim_array);
void chiudi_pipe(void);
uint64_t print_ID(uint64_t ID);
uint64_t rand_ID(void);

/*VARIABILI GLOBALI*/
int *servers, p; /* Dichiaro servers e p globali per accederci comodamente dalla funzione di clean-up */

int main(int argc, char *argv[])
{	int tmp, k, w, i, secret, tentativi, error;
	struct sockaddr_un sa;
	struct timespec pisolino, brusco_risveglio;
	uint64_t ID;

	if (argc != 4) {
		fprintf(stderr, "client: wrong number of arguments.\n");
		exit(EXIT_FAILURE);
	}
	errno = 0; /* se strtol restituisce un valore fuori range setta errno ad ERANGE */
	p = strtol(argv[1], NULL, 10); /* NULL vuol dire che non ci interessa dove termina il numero, 10 sta per "converti in base 10" */
	if ((errno == ERANGE && (p == INT_MAX || p == INT_MIN)) || (errno != 0 && p == 0)){
		fprintf(stderr, "client: strtol(argv[1])\n");
		exit(EXIT_FAILURE);
	}
	k = strtol(argv[2], NULL, 10);
	if ((errno == ERANGE && (k == INT_MAX || k == INT_MIN)) || (errno != 0 && k == 0)){
		fprintf(stderr, "client: strtol(argv[2])\n");
		exit(EXIT_FAILURE);
	}
	w = strtol(argv[3], NULL, 10);
	if ((errno == ERANGE && (k == INT_MAX || k == INT_MIN)) || (errno != 0 && k == 0)){
		fprintf(stderr, "client: strtol(argv[3])\n");
		exit(EXIT_FAILURE);
	}
	
	sa.sun_family = AF_UNIX; /* come da specifica */

	srand(time(NULL)+getpid()); /* uso anche getpid per maggiore sicurezza */

	/* secret compreso tra 1 e 3000 */
	secret = (rand()%MAXSCRT)+1;

	/* il secret è l'attesa in millisecondi tra un invio ed il successivo */
	pisolino.tv_sec = secret / 1000;
	pisolino.tv_nsec = (long) (secret % 1000) * 1000000;

	/*  genero un ID casuale a 64 bit */
	ID = rand_ID();

	/* l'ID lo invio così come è (tanto è casuale), ma la stampa la eseguo come la eseguirà il server*/
	fprintf(stdout,"CLIENT %"PRIX64" SECRET %d\n", print_ID(ID), secret);

	/* servers conterrà inizialmente il numero degli array a cui il socket si connetterà,
	   via via che le connessioni hanno successo i numeri dei server verranno rimpiazzati con i rispettivi fd */
	if( !(servers = (int*)malloc(p*sizeof(int)))){
		fprintf(stderr, "server:msg malloc failed\n");
		exit(EXIT_FAILURE);
	}
	
	/*preparo il cleanup per le pipe*/
	if (atexit(&chiudi_pipe)) {
		fprintf(stderr,"client: atexit(chiudi_pipe) failed");
	}

	/*  seleziono p server fra i k disponibili (non posso scegliere uno stesso server più volte) */
	for(i=0;i<p;i++){
		do{ tmp = 1+rand()%k;
		}while( check(tmp,servers,i) );
		servers[i]=tmp;
	}

	/*  comincio le connessioni ai server */
	for (i = 0; i < p; i++) {
		
		error = snprintf(sa.sun_path, UNIX_PATH_MAX ,"OOB-server-%d", servers[i]);
		if (error <= 0 || error >= UNIX_PATH_MAX ) {
			fprintf(stderr, "server: snprintf(OOB-server) failed\n");
			exit(EXIT_FAILURE);
		}
		/* rimpiazzo i numeri dei server con i loro fd */
		if ((servers[i] = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
			perror("client: socket failed");
			exit(EXIT_FAILURE);
		}
		tentativi = 0;
		while( connect(servers[i], (struct sockaddr*)&sa, sizeof(struct sockaddr_un)) == -1) {
			switch(errno){
				case ENOENT:	if(tentativi < 3){
									tentativi++;
									sleep(1+tentativi);
									break;
								}
								fprintf(stderr, "client: connect exceeded maximum attempts\n");
								exit(EXIT_FAILURE);

				case EINTR:		break;

				default:		perror("client: connect failed");
								exit(EXIT_FAILURE);
			}
		}
	}

	i=0;
	while(i<w){
		/* attendo Secret millisecondi */
		while (nanosleep(&pisolino, &brusco_risveglio) == -1) { 
			switch(errno){/*  in caso l'attesa venga interrotta prematuramente */
				case EINTR:		pisolino = brusco_risveglio;
								break;
				default:		perror("client: nanosleep failed");
								exit(EXIT_FAILURE);
			}
		}
		/* scrivo ad uno dei server selezionato casualmente dai fd disponibili */
		while(writeall(servers[rand() % p], &ID, MEXSIZE) == -1){
			switch(errno){
				case EINTR: 	break; /*  effettuo un nuovo tentativo */

				default:		perror("client: write on server failed");
								exit(EXIT_FAILURE);		
			}
		}
		i++;
	}

	fprintf(stdout,"CLIENT %"PRIX64" DONE\n", print_ID(ID));
	
	return 0;
}

/* funzione di servizio per la scelta dei server a cui connettersi, controlla che il server "target" non sia stato già scelto */
int check(int target, int array[], int dim_array){
	int i = 0, trovato = FALSE;

	while(trovato == FALSE && i < dim_array)
	{
		if(array[i] == target)
			trovato = TRUE;
		else
			i++;
	}

	return trovato;
}

void chiudi_pipe(void){	
	int i;

	/* chiudo le socket */
	for(i=0; i<p; i++){
		close(servers[i]);
	}
}

/* print_ID sarà: ID se la macchina è big-endian, DI se la macchina è little-endian in modo da stampare lo stesso valore dei server/supervisor*/
uint64_t print_ID(uint64_t ID){
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

/* Genero un ID a 64 bit random con due chiamate di rand() sulle sue due metà da 32 bit */
uint64_t rand_ID(){
	uint64_t ID;
	uint8_t *temp = (uint8_t *)&ID;
	
	/* 	rand() genera almeno 15 bit random, servono almeno 5 rand per aver >= 64 bit random.
		uint16_t avrebbe usato solo 4 rand() che sono troppo poche, uint_8 invece ne usa 8 	*/

	temp[0]=rand();
	temp[1]=rand();
	temp[2]=rand();
	temp[3]=rand();
	temp[4]=rand();
	temp[5]=rand();
	temp[6]=rand();
	temp[7]=rand();

	return ID;
}