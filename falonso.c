#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include "falonso.h"

#define TAM_MEMORIA 600

#define LIBRE 1
#define OCUPADO 0
#define OCUPADO_SV 2
#define OCUPADO_SH 3
#define INF 999999

union semun{
	int val;
        struct semid_ds *buf;
        //ushort_t *array;
};

//Estructura para las variables globales
typedef struct globalVars{
	int sharedMemory;
	int semaphores;
	char *pMemory;
	int *contador;
	int hayCoches;
	int numCoches;
} globalVars;

globalVars gv;

//Funciones propias
int tengoCocheDelante(int posicion, char *pMemory, int carril); //Consulta la posición inmediatamente posterior y evalúa si está ocupada
int mirarSemaforo(int posicion, char *pMemory, int carril); //Consulta el color de los semáforos del circuito
int mirarAdelantar(int posicion, int carril, char *pMemory, int color); //Consulta la posición de cambio de carril y evalúa si está ocupada
void seccion_critica(int sem_num, int option, struct sembuf *sops); //Engloba las operaciones a realizar sobre un semáforo(IPC)

void handlerVacia(int signal){
	//Cambia la acción por defecto de SIGALRM
}

//Manejadora. Se salta a este código en el momento en que se pulsa Ctrl+C
void handler(int signal){
	int status;
	int numeroCoches = gv.numCoches;
	
	//Si hay coches, esperamos por todos ellos
	while(numeroCoches>0){
		if(gv.hayCoches) {
			if(waitpid(-1, &status, 0)==-1 && errno!=EINTR) perror("Fatal error in waitpid");
			numeroCoches--;
			if(numeroCoches==0) gv.hayCoches=0;
		}
		else break;
	}
	
	if(fin_falonso(gv.contador) == -1){fprintf(stderr, "fatal error in fin_falonso\n"); exit(1);} //Se llama a fin_falonso con el contador de vueltas
	
	if(gv.semaphores!=-1){ //Libera el array de semáforos
		if(semctl(gv.semaphores, 0, IPC_RMID) == -1){ perror("fatal error in semctl"); exit(1); }
	}
		
	if(gv.sharedMemory != -1){
		if(shmdt((char *)gv.pMemory) == -1){ perror("fatal error in shmdt"); exit(1); } //Desasociarse de memoria compartida
		if(shmctl(gv.sharedMemory, IPC_RMID, NULL) == -1){  //Marcar para borrar la memoria compartida
			perror("fatal error in shmctl");
			exit(1);
		}
	}
	
	exit(0);
}

//Función para imprimir el error en los parámetros iniciales de entrada
void printError(char *msgError){
	sprintf(msgError, "\n\nERROR: ./falonso.c {numero de coches(max.20)} {velocidad(0/1)}\n\n");
	write(1, msgError, strlen(msgError));
}

int main(int argc, char *argv[]){
	/*********************************************************/
	/****************** CAPTURA SIGINT ***********************/
	/*********************************************************/
	struct sigaction accion, accionNula, accionInicio, accionVieja;
	
	sigset_t mask;
	if(sigfillset(&mask)==-1){ perror("fatal error in sigfillset"); return 1;} //Creamos la máscara y la llenamos
	if(sigdelset(&mask, SIGINT)==-1){ perror("fatal error in sigdelset"); return 1;} //Sacamos SIGINT de la máscara
	if(sigdelset(&mask, SIGALRM)==-1){ perror("fatal error in sigdelset"); return 1;} //Sacamos SIGALRM de la máscara
	
	accion.sa_handler=handler;
	accion.sa_mask=mask;
	accion.sa_flags=0;
	
	accionNula.sa_handler=handlerVacia;
	accionNula.sa_mask=mask;
	accionNula.sa_flags=0;
	
	/*Todas las señales estan bloqueadas menos SIGINT y SIGALRM*/
	if(sigprocmask(SIG_SETMASK, &mask, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
	
	sigset_t maskInicio;
	if(sigemptyset(&maskInicio)==-1){ perror("fatal error in sigfillset"); return 1;} //Mascara vacia
	if(sigaddset(&maskInicio, SIGINT)==-1){ perror("fatal error in sigdelset"); return 1;} //Metemos SIGINT en la mascara
	
	accionInicio.sa_handler=handler;
	accionInicio.sa_mask=maskInicio;
	accionInicio.sa_flags=0;
	
	if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;} //Bloqueamos SIGINT
	
	// Cambio de accion a SIGINT (Ahora llama a la manejadora)
	// Conservamos su accion original en accionVieja
	if(sigaction(SIGINT, &accion, &accionVieja) == -1){ 
		perror("Fatal error in sigaction");
		return 1;
	}
	/*********************************************************/
	/*********************************************************/
	/*********************************************************/
	
	//--------------- Variables --------------------//
	gv.sharedMemory = -1;
	gv.semaphores = -1;
	gv.pMemory = NULL;
	gv.contador = NULL;
	
	union semun su; //Para que funcione semctl en encina
	
	int i, j, carrilAux, posAux, carril, posicion, color, velocimetro;
	char msgError[100];
	int modo, procesoAvanza;
	
	gv.hayCoches=0;
	
	int *miPos = NULL;
	int *miCarril = NULL;
	int *posConsulta = NULL;
	int *carrilConsulta = NULL;
	//-------------------------------------------//
	
	//--------------- Precondiciones --------------------//
	if(argc != 3){
		printError(msgError);
		return 1;
	}
	else{
		gv.numCoches = atoi(argv[1]);
		
		if(gv.numCoches<1 || gv.numCoches>20){
			printError(msgError);
			return 1;
		}
		
		modo = atoi(argv[2]);
		
		if(strcmp(argv[2], "0")!=0 && strcmp(argv[2], "1")!=0){
			printError(msgError);
			return 1;
		}
	}
	//------------------------------------------------//
	
	//--------------- Memoria Compartida --------------------//
	if((gv.sharedMemory = shmget(IPC_PRIVATE, TAM_MEMORIA, 0664 | IPC_CREAT)) == -1) {
        	perror("Fatal error in shmget");
    	}
	
	if((gv.pMemory = shmat(gv.sharedMemory, (void *)0, 0)) == NULL){ //Asociacion
		perror("Fatal error in shmat");
	}
	
	gv.contador = (int *)(gv.pMemory+304);
	*(gv.contador) = 0;
	//--------------------------------------------------------//
	 
	//--------------- Semaforos --------------------//
	int numSemaforos = 5 + 1 + gv.numCoches;
	struct sembuf sops[1];
	gv.semaphores = semget(IPC_PRIVATE, numSemaforos, IPC_CREAT | 0600);
	
	//su.array=NULL;
	su.buf=NULL;
	
	su.val=0; if(semctl(gv.semaphores, 1, SETVAL, su) == -1) perror("Fatal error in semctl");
	su.val=1; if(semctl(gv.semaphores, 2, SETVAL, su) == -1) perror("Fatal error in semctl");
	su.val=1; if(semctl(gv.semaphores, 3, SETVAL, su) == -1) perror("Fatal error in semctl");
	su.val=1; if(semctl(gv.semaphores, 4, SETVAL, su) == -1) perror("Fatal error in semctl"); 
	su.val=1; if(semctl(gv.semaphores, 5, SETVAL, su) == -1) perror("Fatal error in semctl"); 
	
	for(i=1; i<=gv.numCoches; i++){
		su.val=0; if(semctl(gv.semaphores, 5+gv.numCoches, SETVAL, su) == -1) perror("Fatal error in semctl");
	}
	//-----------------------------------------------//
	
	//Pintamos el circuito
	if(inicio_falonso(modo, gv.semaphores, gv.pMemory) == -1){ fprintf(stderr, "fatal error in inicio_falonso\n"); return 1;}
	if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
	
	//Creación de procesos
	for(i=1; i<=gv.numCoches; i++){
		switch(fork()){
			case -1: perror("Fatal error in fork...\n"); return 1;
		
			case 0: //codigo del hijo
				if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
				
				//SIGINT recupera su accion original
				if(sigaction(SIGINT, &accionVieja, NULL) == -1){ 
					perror("Fatal error in sigaction");
					return 1;
				}
				
				srand(getpid());
				
				carril = (rand()) % 2; //Carril aleatorio entre 0 y 1
				posicion = i; //Posición inicial distinta para cada proceso
				velocimetro = (rand()) % 99 + 1; //Velocidad inicial aleatoria entre 1 y 99
				
				do{
					color = (rand()) % 8 + 16; //Color inicial aleatorio(+16 para verlo más vivo)	
				}while(color==AZUL);
				
				//Guardo mi situacion en la memoria compartida
				miPos = (int *)(gv.pMemory+320+8*(i-1));
				*miPos = INF;
				miCarril = (int *)(gv.pMemory+320+8*(i-1)+4);
				*miCarril = INF;
				
				//Pintamos el coche
				if(inicio_coche(&carril, &posicion, color) == -1){fprintf(stderr, "fatal error in inicio_coche\n"); return 1;}
				
				/****** LOS COCHES ESPERAN A QUE TODOS HAYAN LLEGADO A LA SALIDA **********/
				if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
				seccion_critica(1, 1, sops);
	     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_1"); return 1;}
	     			
	     			seccion_critica(2, 0, sops);
	     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_2"); return 1;}
	     			if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
	     			/***************************************************************************/
				
				while(1){
					if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
					
					//El coche coge la sección crítica
					seccion_critica(3, -1, sops);
	     				if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_3"); return 1;}
					
					//Si el semaforo vertical está rojo, me paro...
					if(mirarSemaforo(posicion, gv.pMemory, carril) == OCUPADO_SV){
		     				seccion_critica(3, 1, sops);
		     				if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_4"); return 1;}
		     				
		     				//Suelto la sección crítica y me quedo esperando a VERDE
		     				if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
		     				seccion_critica(4, 0, sops); 
		     				if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_5"); return 1;}
		     				if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
					}
					
					//Si el semaforo horizontal está rojo, me paro...
					else if(mirarSemaforo(posicion, gv.pMemory, carril) == OCUPADO_SH){
						seccion_critica(3, 1, sops);
		     				if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_6"); return 1;}
		     				
		     				//Suelto la sección crítica y me quedo esperando a VERDE
		     				if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
		     				seccion_critica(5, 0, sops);
		     				if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_7"); return 1;}
		     				if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
					}
					
					//Si el semaforo esta verde (o no estoy ante el) y no tengo nadie delante, avanzo
					else if(tengoCocheDelante(posicion, gv.pMemory, carril)==LIBRE){
						if(avance_coche(&carril, &posicion, color) == -1){fprintf(stderr, "fatal error in avance_coche\n"); return 1;}
						*miPos = INF;
						*miCarril = INF; //Elimino mi situación de la memoria compartida
						
						//Actualización atómica de la variable compartida: contador
						if( (carril==CARRIL_DERECHO && posicion==133) || (carril==CARRIL_IZQUIERDO && posicion==131)){
							(*(gv.contador))++;
						}
						
						//Avisamos al coche que estaba esperando detrás de nosotros que puede avanzar
						/********************************************/
						for(j=320; j<gv.numCoches*8+320; j=j+8){
							posConsulta = (int*)(gv.pMemory+j);
							carrilConsulta = (int*)(gv.pMemory+j+4);
							
							if(posicion==0){
								if(*posConsulta == 135 && *carrilConsulta == carril){
									seccion_critica((j-320)/8 + 6, 1, sops);
				     					if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_8"); return 1;}
								}
							}
							
							else if(posicion==1){
								if(*posConsulta == 136 && *carrilConsulta == carril){
									seccion_critica((j-320)/8 + 6, 1, sops);
				     					if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_9"); return 1;}
								}
							}
							
							else if(*posConsulta == posicion-2 && *carrilConsulta == carril){
								seccion_critica((j-320)/8 + 6, 1, sops);
			     					if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_10"); return 1;}
							}
						}
			     			/********************************************/
			     			
			     			//Avisamos al coche que estaba esperando justo fuera del cruce de que puede avanzar
						/********************************************/
					     	if(posicion==23 && carril==CARRIL_IZQUIERDO){ posAux=106-1; carrilAux=CARRIL_DERECHO;}
					     	if(posicion==23 && carril==CARRIL_DERECHO){ posAux=101-1; carrilAux=CARRIL_IZQUIERDO;}
					     	if(posicion==21 && carril==CARRIL_DERECHO){ posAux=108-1; carrilAux=CARRIL_DERECHO;}
					     	if(posicion==25 && carril==CARRIL_IZQUIERDO){ posAux=99-1; carrilAux=CARRIL_IZQUIERDO;}
					     	if(posicion==106 && carril==CARRIL_DERECHO){ posAux=23-1; carrilAux=CARRIL_IZQUIERDO;}
					     	if(posicion==108 && carril==CARRIL_DERECHO){ posAux=21-1; carrilAux=CARRIL_DERECHO;}
					     	if(posicion==99 && carril==CARRIL_IZQUIERDO){ posAux=25-1; carrilAux=CARRIL_IZQUIERDO;}
					     	if(posicion==101 && carril==CARRIL_IZQUIERDO){ posAux=23-1; carrilAux=CARRIL_DERECHO;}
					     			
					     	for(j=320; j<gv.numCoches*8+320; j=j+8){
							posConsulta = (int*)(gv.pMemory+j);
							carrilConsulta = (int*)(gv.pMemory+j+4);
									
							if(*posConsulta == posAux && *carrilConsulta == carrilAux){
								seccion_critica((j-320)/8 + 6, 1, sops);
					     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_11"); return 1;}
							}
						}
					     	/********************************************/
						
						seccion_critica(3, 1, sops);
			     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_12"); return 1;}
						
						if(velocidad(velocimetro, carril, posicion) == -1){fprintf(stderr, "fatal error in velocidad\n"); return 1;}
					}

					else if((procesoAvanza = tengoCocheDelante(posicion, gv.pMemory, carril)) > LIBRE){
						switch(procesoAvanza){
							case 105:
							case 107:
							case 98:
							case 100:
							case 221:
							case 20:
							case 24:
							case 220:
								//Escribo donde estoy en la memoria compartida
								*miPos = posicion;
				     				*miCarril = carril;
								
								seccion_critica(3, 1, sops);
					     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_13"); return 1;}
					     			
					     			//Esperar hasta que el coche que tengo delante me avise de que puedo avanzar
					     			if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
					     			seccion_critica(5+i, -1, sops);
					     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_14"); return 1;}
					     			if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
						}
					}
					
					//Si estoy obstaculizado por delante...
					else if(tengoCocheDelante(posicion, gv.pMemory, carril) == OCUPADO){
					
						//Si puedo adelantar, me cambio de carril...
						if(mirarAdelantar(posicion, carril, gv.pMemory, color)){
							posAux = posicion-1;
							carrilAux = carril;
							if(cambio_carril(&carril, &posicion, color) == -1){fprintf(stderr, "fatal error in cambio_carril\n"); return 1;}
							*miPos = INF;
							*miCarril = INF; //Borro mi situación de la memoria compartida
							
							//Avisamos al coche que estaba esperando detrás de nosotros que puede avanzar
							/********************************************/
							for(j=320; j<gv.numCoches*8+320; j=j+8){
								posConsulta = (int*)(gv.pMemory+j);
								carrilConsulta = (int*)(gv.pMemory+j+4);
								if(*posConsulta == posAux && *carrilConsulta == carrilAux){
									seccion_critica((j-320)/8 + 6, 1, sops);
					     				if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_15"); return 1;}
								}
							}
					     		/********************************************/
							
							seccion_critica(3, 1, sops);
		     					if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_16"); return 1;}
								
							if(velocidad(velocimetro, carril, posicion) == -1){fprintf(stderr, "fatal error in velocidad\n"); return 1;}
						}
					
						//Si no puedo (obstaculizado totalmente), me paro...
						else{
							//Escribo donde estoy en la memoria compartida
							*miPos = posicion;
				     			*miCarril = carril;
							
							seccion_critica(3, 1, sops);
				     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_17"); return 1;}
				     			
				     			//Esperar hasta que el coche que tengo delante me avise de que puedo avanzar
				     			if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
				     			seccion_critica(5+i, -1, sops);
				     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_18"); return 1;}
				     			if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
	     					}
					}
					if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
				}
			return 0;
			
			default: //Codigo del padre
				//Espero a que todos los hijos hayan llegado para dar el banderazo (un wait por hijo)
				if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
				seccion_critica(1, -1, sops);
	     			if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_19"); return 1;}
	     			if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
		}
	}
	
	gv.hayCoches=1;
	
	//Captura del CTRL+C
	if(sigaction(SIGINT, &accion, NULL) == -1){ 
		perror("Fatal error in sigaction");
		return 1;
	}
	
	//Cambio de acción a SIGALRM
	if(sigaction(SIGALRM, &accionNula, NULL) == -1){ 
		perror("Fatal error in sigaction");
		return 1;
	}
	
	/********** ¡BANDERAZO! **************/
	seccion_critica(2, -1, sops);
	if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_20"); return 1;}
	/***********************************/
	
	while(1){ //Bucle para ir cambiando el semaforo
		if(luz_semAforo(HORIZONTAL, VERDE) == -1){fprintf(stderr, "fatal error in luz_semaforo\n"); return 1;} //horizontal - verde
		if(luz_semAforo(VERTICAL, ROJO) == -1){fprintf(stderr, "fatal error in luz_semaforo\n"); return 1;} //vertical - rojo
		
		seccion_critica(5, -1, sops);
		if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_21"); return 1;} //1V / 0H -> Pasa H
		
		alarm(2);
		if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
		pause();
		if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
		
		if(luz_semAforo(HORIZONTAL, AMARILLO) == -1){fprintf(stderr, "fatal error in luz_semaforo\n"); return 1;}; //horizontal - amarillo
		
		seccion_critica(5, 1, sops);
		if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_22"); return 1;} //1V / 1H -> No pasan
		
		alarm(1);
		if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}		
		pause();
		if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}		
		
		if(luz_semAforo(HORIZONTAL, ROJO) == -1){fprintf(stderr, "fatal error in luz_semaforo\n"); return 1;} //horizontal - rojo
		if(luz_semAforo(VERTICAL, VERDE) == -1){fprintf(stderr, "fatal error in luz_semaforo\n"); return 1;} //vertical - verde
		
		seccion_critica(4, -1, sops);
		if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_23"); return 1;} //1V / 0H -> Pasa V
		
		alarm(2);
		if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}		
		pause();
		if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
		
		if(luz_semAforo(VERTICAL, AMARILLO) == -1){fprintf(stderr, "fatal error in luz_semaforo\n"); return 1;}; //vertical - amarillo
		
		seccion_critica(4, 1, sops);
		if( (semop(gv.semaphores, sops, 1)) == -1){ perror("Fatal error in semop_24"); return 1;} //1V / 1H -> No pasan
		
		alarm(1);
		if(sigprocmask(SIG_UNBLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}		
		pause();
		if(sigprocmask(SIG_BLOCK, &maskInicio, NULL)==-1){ perror("fatal error in sigprocmask"); return 1;}
	}
		
	return 0;
}

int tengoCocheDelante(int posicion, char *pMemory, int carril){
	int posSig;
	
	if(posicion==136) posicion=-1; //Circuito circular
	//Las posiciones del carril izquierdo están a partir de la 137 de la memoria compartida
	if(carril==CARRIL_IZQUIERDO) posicion+=137;
	
	//Casos especiales del cruce (véase cuadro anexo)
	if(posicion==(106-1) && pMemory[23+137]!=' ') return 105; //El coche de la 105 de esperarse
	if(posicion==(108-1) && pMemory[21]!=' ') return 107;
	if(posicion==(99+137-1) && pMemory[25+137]!=' ') return 98;
	if(posicion==(101+137-1) && pMemory[23]!=' ') return 100;
	
	if(posicion==(23+137-1) && pMemory[106]!=' ') return 221;
	if(posicion==(21-1) && pMemory[108]!=' ') return 20;
	if(posicion==(25+137-1) && pMemory[99+137]!=' ') return 24;
	if(posicion==(23-1) && pMemory[101+137]!=' ') return 220;
	
	//Casos habituales
	posSig = posicion+1;
	
	if(pMemory[posSig] == ' ') return LIBRE;
	else return OCUPADO;
}

int mirarSemaforo(int posicion, char *pMemory, int carril){
	//Las posiciones 20, 22, 105 y 98 son las posiciones inmediatamente anteriores a los ptos de comprobación de semáforo
	
	if(carril==CARRIL_DERECHO && posicion==20){
		if(pMemory[275] == VERDE) return LIBRE;
		else return OCUPADO_SV; //Semaforo vertical en rojo o amarillo
	}
	
	if(carril==CARRIL_IZQUIERDO && posicion==22){
		if(pMemory[275] == VERDE) return LIBRE;
		else return OCUPADO_SV;
	}
	
	if(carril==CARRIL_DERECHO && posicion==105){
		if(pMemory[274] == VERDE) return LIBRE;
		else return OCUPADO_SH; //Semaforo horizontal en rojo o amarillo
	}
	
	if(carril==CARRIL_IZQUIERDO && posicion==98){
		if(pMemory[274] == VERDE) return LIBRE;
		else return OCUPADO_SH;
	}
	
	return LIBRE; //Cuando consultamos el semáforo y estamos lejos de él, siempre recibimos LIBRE
}

int mirarAdelantar(int posicion, int carril, char *pMemory, int color){
	//Si vamos a adelantar y justo vamos a dar a posición de comprobación de semáforo, cuando el semáforo está rojo, LO IMPEDIMOS
	//Las sumas y restas siguientes se justifican en la tabla de adelantamientos de la práctica (consúltese avellano)
	/**********************************/
	if(pMemory[274] == ROJO && carril == CARRIL_DERECHO && posicion-5 == 99) return 0;
	if(pMemory[274] == ROJO && carril == CARRIL_IZQUIERDO && posicion+5 == 106) return 0;
	if(pMemory[275] == ROJO && carril == CARRIL_DERECHO && posicion+1 == 23) return 0;
	if(pMemory[275] == ROJO && carril == CARRIL_IZQUIERDO && posicion-1 == 21) return 0;
	/**********************************/
	
	//Formalización de la tabla de equivalencias de adelantamientos
	//Una consulta de la forma [posicion+137] corresponde a una posición del carril izquierdo (véase la distribución de la memoria compartida)
	if(carril==CARRIL_DERECHO){
		if(posicion>=0 && posicion<=13){ if(pMemory[posicion+137+0]==' ') return 1;}
		
		else if(posicion>=14 && posicion<=28){
			//Si hay dos procesos en el cruce, no podemos adelantar si alguien de la horizontal ya está donde nosotros queremos ir
			//Ver cuadro anexo sobre posiciones equivalentes del cruce para entender las equivalencias 
			/****************************/
			if(posicion==22 && (pMemory[23+137]!=' ' || pMemory[106]!=' ')) return 0;
			if(posicion==24 && (pMemory[25+137]!=' ' || pMemory[99+137]!=' ')) return 0;
			/****************************/
			
			if(pMemory[posicion+137+1]==' ') return 1;
		}
		
		else if(posicion>=29 && posicion<=60){ if(pMemory[posicion+137+0]==' ')  return 1;}
		else if(posicion>=61 && posicion<=62){ if(pMemory[posicion+137-1]==' ')  return 1;}
		else if(posicion>=63 && posicion<=65){ if(pMemory[posicion+137-2]==' ')  return 1;}
		else if(posicion>=66 && posicion<=67){ if(pMemory[posicion+137-3]==' ')  return 1;}
		else if(posicion==68){ if(pMemory[posicion+137-4]==' ')  return 1;}
		
		else if(posicion>=69 && posicion<=129){
			/****************************/
			if(posicion==106 && (pMemory[101+137]!=' ' || pMemory[23]!=' ')) return 0;
			if(posicion==104 && (pMemory[99+137]!=' ' || pMemory[25+137]!=' ')) return 0;
			/****************************/
			
			if(pMemory[posicion+137-5]==' ')  return 1;
		}
		
		else if(posicion==130){ if(pMemory[posicion+137-3]==' ')  return 1;}
		else if(posicion>=131 && posicion<=134){ if(pMemory[posicion+137-2]==' ')  return 1;}
		else if(posicion>=135 && posicion<=136){ if(pMemory[posicion+137-1]==' ')  return 1;}
		
		return 0;
	}
	
	else if(carril==CARRIL_IZQUIERDO){
		if(posicion>=0 && posicion<=15){ if(pMemory[posicion+0]==' ')  return 1;}
		
		else if(posicion>=16 && posicion<=28){
			/****************************/
			if(posicion==22 && (pMemory[21]!=' ' || pMemory[108]!=' ')) return 0;
			if(posicion==24 && (pMemory[23]!=' ' || pMemory[101+137]!=' ')) return 0;
			/****************************/
			
			if(pMemory[posicion-1]==' ')  return 1;
		}
		
		else if(posicion>=29 && posicion<=58){ if(pMemory[posicion+0]==' ')  return 1;}
		else if(posicion>=59 && posicion<=60){ if(pMemory[posicion+1]==' ')  return 1;}
		else if(posicion>=61 && posicion<=62){ if(pMemory[posicion+2]==' ')  return 1;}
		else if(posicion>=63 && posicion<=64){ if(pMemory[posicion+4]==' ')  return 1;}
		
		else if(posicion>=65 && posicion<=125){
			/****************************/
			if(posicion==101 && (pMemory[106]!=' ' || pMemory[23+137]!=' ')) return 0;
			if(posicion==103 && (pMemory[108]!=' ' || pMemory[21]!=' ')) return 0;
			/****************************/
			
			if(pMemory[posicion+5]==' ')  return 1;
		}
		
		else if(posicion==126){ if(pMemory[posicion+4]==' ')  return 1;}
		else if(posicion>=127 && posicion<=128){ if(pMemory[posicion+3]==' ')  return 1;}
		else if(posicion>=129 && posicion<=133){ if(pMemory[posicion+2]==' ')  return 1;}
		else if(posicion>=134 && posicion<=136){ if(pMemory[136]==' ')  return 1;}
		
		return 0;
	}
}

//Operaciones recurrentes sobre semáforos
void seccion_critica(int sem_num, int option, struct sembuf *sops){
	sops[0].sem_num = sem_num;
	sops[0].sem_op = option;
	sops[0].sem_flg = 0;
}
