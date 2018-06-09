/*
 *	@autor: Gabino Luis Lazo (i1028058)
 *	@autor: Francisco Pinto Santos (i0918455)
 *	
 *	Primera prActica SSOO II - PARKING
 *      http://avellano.usal.es/~ssooii/PARKING/parking.htm
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/sem.h>
#include <string.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include "parking.h"

//----------------- PARAMETROS --------------------------------------------------------
#define TRUE    1
#define FALSE   0

#define MAX_LONG_ROAD       80
#define MAX_DATOS_COCHE     4

#define FIFO            1
#define P_APARCAR       2
#define P_DESAPARCAR    3

#define TIMEOUT_SEC     30

#define PRIORITARIO     1
#define NO_PRIORITARIO  255

#define MAX_MONTICULO   100

//---------------- ERRORES Y LOG ------------------------------------------------------
#define LOG(str, ...)                               \
    do{                                             \
        if(global.debug){                           \
            fprintf(stderr, str, ##__VA_ARGS__);    \
        }                                           \
    }while(0)

#define EXIT(str, ...)                                  \
    do{                                                 \
        fprintf(stderr, str, ##__VA_ARGS__);            \
        freeResources(-1);                              \
    }while(0)

#define PRINT_ERROR(returnValue)                                                    \
    do{                                                                             \
        if((returnValue) == -1){                                                    \
            char errorTag[80];                                                      \
            sprintf(errorTag, "\n[%s:%d:%s] ", __FILE__, __LINE__, __FUNCTION__);   \
            perror(errorTag);                                                       \
        }                                                                           \
    }while(0)

#define EXIT_ON_FAILURE(returnValue)                                \
    do{                                                             \
        if((returnValue) == -1 && errno != ENOMSG){                 \
            PRINT_ERROR(-1);                                        \
            freeResources(-1);                                      \
        }                                                           \
    }while(0)

#define EXIT_IF_NULL(returnValue)                                   \
    do{                                                             \
        if((returnValue) == NULL && errno != ENOMSG){               \
            PRINT_ERROR(-1);                                        \
            freeResources(-1);                                      \
        }                                                           \
    }while(0)

//--------------- GESTION IPCS --------------------------------------------------------
#define MSG_SIZE(msg)   (sizeof(msg) - sizeof(long))
                           
#define TIPO_COMANDO                1
#define TIPO_REQUEST                2
#define __TIPO_ORDEN(algoritmo)     (20 + algoritmo)
#define __TIPO_RESERVAS(algoritmo)  (30 + algoritmo)    
#define __TIPO_OCUPADOS(algoritmo)  (40 + algoritmo)
//estructura tipos mensajes: (coche*100 (n cifras)) + (tipo msg-algoritmo ( 2 cifras))
#define TIPO_ORDEN(coche,algoritmo)     ((100*(coche)) + __TIPO_ORDEN(algoritmo))
#define TIPO_RESERVA(coche,algoritmo)   ((100*(coche)) + __TIPO_RESERVAS(algoritmo))
#define TIPO_OCUPACION(coche,algoritmo) ((100*(coche)) + __TIPO_OCUPADOS(algoritmo))

#define WAIT -1
#define SIGNAL 1
#define WAIT0 0

#define SEM_START               (PARKING_getNSemAforos())
#define SEM_WRITE(algoritmo)    (PARKING_getNSemAforos() + 1 +     (algoritmo))
#define SEM_READ(algoritmo)     (PARKING_getNSemAforos() + 1 + 4 + (algoritmo))

//--------------- PROCESOS Y SEÑALES --------------------------------------------------
#define CREATE_PROCESS(value)    EXIT_ON_FAILURE((value) = fork())

#define IF_CHILD(value) if((value) == 0)

#define KILL_PROCESS(pid,signal)                     \
    do{                                              \
        if(pid != 0){                                \
            PRINT_ERROR(kill(pid,signal));           \
            PRINT_ERROR(waitpid(pid,NULL,0));        \
        }                                            \
    }while(0)

#define READY(isSIGALARMsensitive)                                      \
    do{                                                                 \
        sigset_t initialSet;                                            \
        semops(SEM_START,WAIT,1);                                       \
        semops(SEM_START,WAIT0,0);                                      \
        EXIT_ON_FAILURE(sigfillset(&initialSet));                       \
        EXIT_ON_FAILURE(sigdelset(&initialSet,SIGINT));                 \
        if(isSIGALARMsensitive){                                        \
            sigdelset(&initialSet,SIGALRM);                             \
        }                                                               \
        EXIT_ON_FAILURE(sigprocmask(SIG_SETMASK, &initialSet, NULL));   \
    }while(0)

#define REDEFINE_SIGNAL(signal,funcion)                         \
    do{                                                         \
        struct sigaction sigactionSs;                           \
        EXIT_ON_FAILURE(sigfillset(&sigactionSs.sa_mask));      \
        sigactionSs.sa_handler=funcion;                         \
        sigactionSs.sa_flags=0;                                 \
        EXIT_ON_FAILURE(sigaction(signal,&sigactionSs,NULL));   \
    }while(0)

//--------------- SECCIONES CRITICAS --------------------------------------------------
#define START_WRITE_CRITICAL_SECTION(coche)                                                                     \
    do{                                                                                                         \
        permisoEntradaEscrituraAtomico(SEM_WRITE(PARKING_getAlgoritmo(coche)));                                 \
        semops(SEM_READ(PARKING_getAlgoritmo(coche)), WAIT0, 0);                                                \
        LOG("\n%d###%d: He entrado en SemWrite.\n", PARKING_getAlgoritmo(coche), PARKING_getNUmero(coche));     \
    }while(0)

#define END_WRITE_CRITICAL_SECTION(coche)                                                                      \
    do{                                                                                                        \
        semops(SEM_WRITE(PARKING_getAlgoritmo(coche)),WAIT,1);                                                 \
        LOG("\n%d###%d: He salido en SemWrite.\n", PARKING_getAlgoritmo(coche), PARKING_getNUmero(coche));     \
    }while(0)

#define START_READ_CRITICAL_SECTION(coche)                                                                              \
    do{                                                                                                                 \
        permisoEntradaLecturaAtomico(SEM_WRITE(PARKING_getAlgoritmo(coche)), SEM_READ(PARKING_getAlgoritmo(coche)));    \
        LOG("\n%d###%d: He entrado en SemRead.\n", PARKING_getAlgoritmo(coche), PARKING_getNUmero(coche));              \
    }while(0)

#define END_READ_CRITICAL_SECTION(coche)                                                                    \
    do{                                                                                                     \
        semops(SEM_READ(PARKING_getAlgoritmo(coche)),WAIT,1);                                               \
        LOG("\n%d###%d: He salido en SemRead.\n", PARKING_getAlgoritmo(coche), PARKING_getNUmero(coche));   \
    }while(0)

//--------------- MOVIMIENTOS ---------------------------------------------------------
#define ESTA_DESAPARCANDO_AVANCE(coche)     (PARKING_getY(coche)  == 1 && PARKING_getY2(coche) == 2)
#define ESTA_DESAPARCANDO_COMMIT(coche)     (PARKING_getY2(coche) == 1 && PARKING_getY(coche)  == 2)
#define ESTA_APARCANDO_AVANCE(coche)        (PARKING_getY2(coche) == 1 && PARKING_getY(coche)  == 2)
#define ESTA_APARCANDO_COMMIT(coche)        (PARKING_getY(coche)  == 1 && PARKING_getY2(coche) == 2)
#define ESTA_EN_CARRETERA(coche)            (PARKING_getY2(coche) == 2 && PARKING_getY(coche)  == 2)

//--------------- MENSAJES ------------------------------------------------------------
#define USAGE_ERROR_MSG     "./parking <velocidad> <numChoferes> [D] [PA | PD]"
#define NOT_ENOUGH_CHOF_MSG "NUmero de chOferes insuficiente"

#define DUMP_PATH_PRIMER    "_dump_primer"
#define DUMP_PATH_SIGUIENTE "_dump_siguiente"
#define DUMP_PATH_MEJOR     "_dump_mejor"
#define DUMP_PATH_PEOR      "_dump_peor"

// Tipos usados internamente por el monticulo binario
typedef int tipoClave;
typedef struct PARKING_mensajeBiblioteca tipoInfo;

typedef struct
{ tipoClave clave;
  tipoInfo  informacion;
} tipoElemento;

typedef struct
{   tipoElemento elemento[MAX_MONTICULO];
    int tamanno;
} Monticulo;

// Tipos usados en el programa
typedef unsigned char bool;

/* Estructura de datos de la carretera:
 *
 *      estado: situacion de dicha casilla de la carretera. Puede ser:
 *          - LIBRE: no hay ningun coche ocupando ni reservando la casilla.
 *          - OCUPADO: hay un coche ocupando esa casilla. El identificador
 *                     de dicho coche esta reflejado en el atributo <ocupante>.
 *          - RESERVADO: hay un coche reservando esa casilla, lo que significa
 *                       que sera el siguiente coche en ocuparla una vez este
 *                       libre. El identificador de dicho coche esta reflejado
 *                       en el atributo <reservante>.
 *          - OC_RES: la casilla esta ocupada y reservada.
 * 
 *      ocupante: Identificador del coche que ocupa la casilla.
 *      reservante: Identificador del coche que reserva la casilla.
 */
typedef enum {LIBRE, OCUPADO, RESERVADO, OC_RES} estado;
typedef struct{
    estado e;
    int ocupante, reservante;
}Carretera;

//  Estructura de los mensajes de buzon de peticion de permiso para ocupar o reservar.
typedef struct{
    long tipo;
    long idReceptor;
    long idRemitente;
    int posXRequerida;
}msgCarretera;

//  Mensaje que envia un coche justo despues de salir para que salga el siguiente.
//  Aseguramos el orden correcto de salida.
struct mensajeOrden{
    long tipo;
};

//  Union para inicializar los semaforos de SYSTEM V
union semun{
    int val;
    struct semid_ds*buf;
    unsigned short *array;
};


//  Funcion para tratamiento de argumentos
void registrarArgumentos(int argc, char *argv[], int*vel, int*nChof, int *prioridad);
//  Funciones de reserva de los IPCs e inicializacion de los mismos
void reservarIpcs(int nChof);
void initSemaforos(int nChof);
void initBuzones();
void initMemoria();

//  Funcion para crear los procesos necesarios en la ejecucion.
void procrear(int nChof, int prioridad);

/*  Funciones del proceso gestor de mensajes de la biblioteca:
 *
 *  Dicho proceso se encarga de recoger los mensajes que crea la
 *  biblioteca y los guardara en una estructura interna de
 *  acuerdo con el parametro <prioridad>:
 * 
 *      FIFO: los mensajes se pasan directamente desde el buzon.
 *      P_APARCAR: se guardan en una cola de prioridad para las
 *                 ordenes de aparcamiento.
 *      P_DESAPARCAR: se guardan en una cola de prioridad para las
 *                    ordenes de desaparcamiento.
 * 
 *  Cuando un chofer quiere un nuevo trabajo pide uno al gestor
 *  y este se lo da atendiendo a la prioridad antes descrita.
 */
void mailManagerFunction(int prioridad);
void mailManagerFIFO();
void mailManagerPA();
void mailManagerPD();

//  Funcion de los procesos choferes. Piden un trabajo al gestor y
//  llaman a las funciones aparcar() o desaparcar() de la
//  biblioteca.
void choferFunction(void);

//  Funcion del proceso timer, se encargara de terminar la ejecucion
//  una vez pasado el tiempo determinado por la macro TIMEOUT_SEC.
void timerFunction(void);

//  Funciones de callback para interfaz con la biblioteca
void commit(HCoche c);
void permisoAvance(HCoche c);
void permisoAvanceCommit(HCoche c);

/*  FUNCIONES DE ESCRITURA EN MEMORIA COMPARTIDA
 *  
 *  Funciones de escritura en la memoria compartida que representa la carretera.
 *  Dichas funciones son seguras ante concurrencia gracias a una zona de exclusion
 *  mutua. Solo habra un proceso escribiendo en la memoria cada vez. 
 */
void actualizarCarretera(HCoche c, Carretera* carr);
void reservarCarretera(HCoche c, Carretera* carr, int posicion);

/*  FUNCIONES DE LECTURA EN MEMORIA COMPARTIDA
 * 
 *  Funcion para pedir permiso de ocupacion antes de avanzar, esta protegido de
 *  concurrencia gracias a una zona de exclusion mutua. Pueden leer la 
 *  memoria un numero indeterminado de procesos siempre y cuando no
 *  exista otro dentro de la seccion critica de una funcion de
 *  escritura.
 */
void pedirPermisoOcupacion(HCoche c, Carretera* carr, int posInicial, int posFinal);

/*  Helpers para gestionar las peticiones de otros coches:
 *
 *      - guardarPeticionOcupacion():   Guarda las peticiones de permiso de ocupacion en una 
 *                                      estructura interna para satisfacerlas en un futuro.
 * 
 *      - recepcionPeticionReserva():   Recibe un mensaje de permiso de reserva de otro
 *                                      coche para satisfacerlo.
 * 
 *      - recepcionPeticionOcupacion(): Idem con un mensaje de permiso de ocupacion. Si
 *                                      no se puede satisfacer se guardan usando la funcion
 *                                      guardarPeticionOcupacion().
 */
void guardarPeticionOcupacion(HCoche c, msgCarretera msg);
void recepcionPeticionReserva(HCoche c);
void recepcionPeticionOcupacion(HCoche c);

/*  Diferentes algoritmos de posicion de aparcamiento en la acera.
 *  Llamados por la biblioteca cada vez que quiere aparcar un
 *  nuevo coche.
 */ 
int primerAjuste(HCoche c);
int mejorAjuste(HCoche c);
int peorAjuste(HCoche c);
int siguienteAjuste(HCoche c);

//  Handlers de los procesos. Usadas cuando se termina la ejecucion 
//  del programa para liberar recursos.
void freeResources(int ss);
void timeIsUp(int ss);
void childHandler(int ss);

/*  Funciones de abstraccion para las operaciones con semaforos de SYSTEM V.
 *  No llamadas directamente dentro del programa; son usadas dentro de las 
 *  macros de inicio y final de seccion critica:
 *      - START_WRITE_CRITICAL_SECTION
 *      - END_WRITE_CRITICAL_SECTION
 *      - START_READ_CRITICAL_SECTION
 *      - END_READ_CRITICAL_SECTION
 */
void semops(int pos, int typeOp, int quantity);
void permisoEntradaEscrituraAtomico(int semWrite);
void permisoEntradaLecturaAtomico(int semWrite, int semRead);

//  Funciones usadas para depuracion.
void writeMemorySnapshot(int algoritmo);
char* getEstadoString(estado e);
char* getDumpPath(int algoritmo);

//  Funciones de la estructura de datos monticulo binario
void iniciaMonticulo(Monticulo *m);
int vacioMonticulo(Monticulo m);
int insertar(tipoElemento x, Monticulo *m);
int eliminarMinimo(Monticulo *m, tipoElemento *minimo);
void filtradoDescendente(Monticulo *m, int i);
void filtradoAscendente(Monticulo *m, int i);
void crearMonticulo(Monticulo *m);

//  Estructura principal del programa.
struct _global{
    //  Punteros a las aceras/carreteras de cada algoritmo.
    //  Iniciadas en initMemoria()
    bool* memAceras[4];
    Carretera* memCarreteras[4];

    //  Ficheros de depuracion donde se escriben snapshots de memoria
    //  cada vez que un coche actualiza la memoria compartida de
    //  la carretera.
    FILE* dump_file[4];

    //  Representan manejadores opacos de los recursos IPC.
    int sem, mem, buzon;

    //  Puntero de toda la memoria compartida, usado para iniciar
    //  memAceras[] y memCarreteras[] y para liberar toda la
    //  memoria en freeResources()
    void* memp;

    //  pids de todos procesos creados por el padre.
    pid_t mailManager, timer, *chofers;

    //Valor que indica al programa si estamos en ejecucion con depuracion o no.
    int debug;

    //Valor para saber si es un hijo o el padre
    bool isChild;
} global;


int main(int argc, char *argv[]){
    int nChof, vel;
    int prioridad = FIFO;
    TIPO_FUNCION_LLEGADA func[4] ={primerAjuste, siguienteAjuste, mejorAjuste, peorAjuste};
    sigset_t set; 

    global.isChild = FALSE;
    global.sem   = -1;
    global.buzon = -1;
    global.mem   = -1;
    global.mailManager = 0;
    global.timer = 0;
    global.chofers = NULL;

    EXIT_ON_FAILURE(sigfillset(&set));
    EXIT_ON_FAILURE(sigprocmask(SIG_SETMASK, &set, NULL));

    registrarArgumentos(argc,argv,&vel,&nChof,&prioridad);

    if(global.debug){
        EXIT_IF_NULL((global.dump_file[PRIMER_AJUSTE] = fopen(DUMP_PATH_PRIMER, "w")));
        EXIT_IF_NULL((global.dump_file[SIGUIENTE_AJUSTE] = fopen(DUMP_PATH_SIGUIENTE, "w")));
        EXIT_IF_NULL((global.dump_file[MEJOR_AJUSTE] = fopen(DUMP_PATH_MEJOR, "w")));
        EXIT_IF_NULL((global.dump_file[PEOR_AJUSTE] = fopen(DUMP_PATH_PEOR, "w")));
    }

    reservarIpcs(nChof);

    PARKING_inicio(vel, func, global.sem, global.buzon, global.mem, global.debug);
        
    procrear(nChof, prioridad);

    REDEFINE_SIGNAL(SIGALRM,freeResources);
    REDEFINE_SIGNAL(SIGINT,freeResources);

    READY(TRUE);

    PARKING_simulaciOn();

    freeResources(SIGALRM);
}

void registrarArgumentos(int argc, char *argv[], int*vel, int*nChof, int *prioridad){

    if(argc < 3 || argc > 5){
        EXIT("%s\n",USAGE_ERROR_MSG);
    }
    *vel = atoi(argv[1]);
    *nChof = atoi(argv[2]);
    if(*nChof == 0){
        EXIT("%s\n%s\n",USAGE_ERROR_MSG, NOT_ENOUGH_CHOF_MSG);
    }

    if(argc == 4){
        if(strcmp(argv[3], "D") == 0)
            global.debug = TRUE;
        
        else if (strcmp(argv[3], "PA") == 0)
            *prioridad = P_APARCAR;

        else if (strcmp(argv[3], "PD") == 0)
            *prioridad = P_DESAPARCAR;

        else {
            EXIT("%s\n",USAGE_ERROR_MSG);
        }
    }
    else if(argc == 5){
        if((strcmp(argv[3], "D") == 0) || (strcmp(argv[4], "D") == 0)){
            global.debug = TRUE;
        
            if ((strcmp(argv[3], "PA") == 0) || (strcmp(argv[4], "PA") == 0)){
                *prioridad = P_APARCAR;
            }

            else if ((strcmp(argv[3], "PD") == 0) || (strcmp(argv[4], "PD") == 0)){
                *prioridad = P_DESAPARCAR;
            }

            else{
                EXIT("%s\n", USAGE_ERROR_MSG);
            }
        }
        else {
            EXIT("%s\n",USAGE_ERROR_MSG);
        }
    }
}
//----------- FUNCIONES DE RESERVA E INICIALIZACION DE IPCS ---------------------------
void reservarIpcs(int nChof){
    
    initSemaforos(nChof);

    initBuzones();

    initMemoria();
}

void initSemaforos(int nChof){
    int i;
    union semun semunion;

    //Reserva de semaforos
    EXIT_ON_FAILURE(global.sem = semget(IPC_PRIVATE,PARKING_getNSemAforos() + 4 + 4 + 1, IPC_CREAT | 0600));

    //Valores iniciales  a los semaforos
    semunion.val = nChof+3;
    EXIT_ON_FAILURE(semctl(global.sem, SEM_START,SETVAL, semunion));
    for(i = SEM_WRITE(PRIMER_AJUSTE); i <= SEM_READ(PEOR_AJUSTE); i++){
        semunion.val = 0;
        EXIT_ON_FAILURE(semctl(global.sem, i, SETVAL,semunion));
    }
}

void initBuzones(){
    int i;
    struct mensajeOrden initialMsg = {1};

    //Reserva de los buzones
    EXIT_ON_FAILURE(global.buzon = msgget(IPC_PRIVATE, IPC_CREAT | 0600));

    //Enviamos un primer mensaje al buzon de orden para que el primer chofer pueda salir
    for(i = PRIMER_AJUSTE; i <= PEOR_AJUSTE; i++){
        initialMsg.tipo = TIPO_ORDEN(1,i);
        EXIT_ON_FAILURE(msgsnd(global.buzon, &initialMsg, MSG_SIZE(initialMsg), 0));
    }
}

void initMemoria(){
    int i;
    bool* memAcerasTemp;
    Carretera* memCarrTemp;

    int tamannoMemAcera = sizeof(bool) * 4 * MAX_LONG_ROAD;
    int tamannoMemCarretera = sizeof(Carretera) * 4 * MAX_LONG_ROAD;
    int tamannoMemTotal = PARKING_getTamaNoMemoriaCompartida() + tamannoMemAcera + tamannoMemCarretera;

    //Reserva la memoria compartida y recoge un puntero a la misma
    EXIT_ON_FAILURE(global.mem = shmget(IPC_PRIVATE, tamannoMemTotal, IPC_CREAT | 0600));
    EXIT_IF_NULL(global.memp = (void*)shmat(global.mem, NULL, 0));

    memset(global.memp, 0, tamannoMemTotal);

    //Iniciamos los arrays de memoria memAceras[] y memCarreteras[] para un acceso facil a la misma dentro del programa 
    memAcerasTemp = (bool*)(global.memp + PARKING_getTamaNoMemoriaCompartida());
    memCarrTemp = (Carretera*)(global.memp + PARKING_getTamaNoMemoriaCompartida() + tamannoMemAcera);
        
    for(i=PRIMER_AJUSTE; i<=PEOR_AJUSTE; i++){
        global.memAceras[i] = memAcerasTemp + (MAX_LONG_ROAD*i);
        global.memCarreteras[i] =  memCarrTemp + (MAX_LONG_ROAD*i);
    }
}

//----------- FUNCION DE CREACION DE PROCESOS -----------------------------------------
void procrear(int nChof, int prioridad){
    int i;

    EXIT_IF_NULL(global.chofers = calloc(nChof+1,sizeof(pid_t)));

    CREATE_PROCESS(global.mailManager);
    IF_CHILD(global.mailManager){
        global.isChild = TRUE;
        mailManagerFunction(prioridad);
    }

    CREATE_PROCESS(global.timer);
    IF_CHILD(global.timer){
        global.isChild = TRUE;
        timerFunction();
    }

    for(i=0; i<nChof; i++){
        CREATE_PROCESS(global.chofers[i]);
        IF_CHILD(global.chofers[i]){
            global.isChild = TRUE;
            choferFunction();
        }
    }
}

//----------- FUNCIONES DEL GESTOR DE MENSAJES ----------------------------------------
void mailManagerFunction(int prioridad){

    REDEFINE_SIGNAL(SIGINT,childHandler);

    READY(FALSE);
        
    switch(prioridad){
        case FIFO:
            mailManagerFIFO();
            break;
        case P_APARCAR:
            mailManagerPA();
            break;
        case P_DESAPARCAR:
            mailManagerPD();
    }
}

void mailManagerFIFO(){
    struct PARKING_mensajeBiblioteca nuevoMsj;
    struct PARKING_mensajeBiblioteca siguienteMsj;

    while(TRUE){
        //Recoge el mensaje del buzon
        EXIT_ON_FAILURE(msgrcv(global.buzon, &siguienteMsj, MSG_SIZE(siguienteMsj), PARKING_MSG, 0));
        siguienteMsj.tipo = TIPO_COMANDO;

        //Espera por la peticion de mensaje de un chofer para enviar el mensaje recogido
        EXIT_ON_FAILURE(msgrcv(global.buzon, &nuevoMsj, MSG_SIZE(nuevoMsj), TIPO_REQUEST, 0));
        EXIT_ON_FAILURE(msgsnd(global.buzon, &siguienteMsj, MSG_SIZE(siguienteMsj), 0));
    }
}

void mailManagerPA(){
    Monticulo montMsj;
    tipoElemento mensjFormatted;
    struct PARKING_mensajeBiblioteca nuevoMsj;
    struct PARKING_mensajeBiblioteca siguienteMsj;
    int orden = 1; //Para asgurarnos el orden correcto de salida del monticulo binario en aparcamientos

    iniciaMonticulo(&montMsj);

    while(TRUE){
        
        //Recoge todos los mensajes de la biblioteca que pueda y los guarda en monticulo binario
        while(montMsj.tamanno < MAX_MONTICULO-1){
            nuevoMsj.subtipo = 0;
            EXIT_ON_FAILURE(msgrcv(global.buzon, &nuevoMsj, MSG_SIZE(nuevoMsj), PARKING_MSG, IPC_NOWAIT));
            if(nuevoMsj.subtipo == 0) break;

            nuevoMsj.tipo = TIPO_COMANDO;
            mensjFormatted.clave = (nuevoMsj.subtipo == PARKING_MSGSUB_APARCAR) ? PRIORITARIO + orden : NO_PRIORITARIO + orden;
            mensjFormatted.informacion = nuevoMsj;
            insertar(mensjFormatted, &montMsj);
            orden++;
        }

        // Cogemos el primer mensaje del monticulo y lo preparamos para enviar
        // o
        // Si el monticulo esta vacio, esperamos por el siguiente mensaje de la biblioteca
        if(!vacioMonticulo(montMsj)){
            eliminarMinimo(&montMsj, &mensjFormatted);
            siguienteMsj = mensjFormatted.informacion;
        }
        else{
            EXIT_ON_FAILURE(msgrcv(global.buzon, &siguienteMsj, MSG_SIZE(siguienteMsj), PARKING_MSG, 0));
            siguienteMsj.tipo = TIPO_COMANDO;
            orden++;
        }

        //Espera por la peticion de mensaje de un chofer para enviar el mensaje recogido
        EXIT_ON_FAILURE(msgrcv(global.buzon, &nuevoMsj, MSG_SIZE(nuevoMsj), TIPO_REQUEST, 0));
        EXIT_ON_FAILURE(msgsnd(global.buzon, &siguienteMsj, MSG_SIZE(siguienteMsj), 0));
    }
}

void mailManagerPD(){
    Monticulo montMsj;
    tipoElemento mensjFormatted;
    struct PARKING_mensajeBiblioteca nuevoMsj;
    struct PARKING_mensajeBiblioteca siguienteMsj;
    int orden = 1; //Para asgurarnos el orden correcto de salida del monticulo binario en aparcamientos

    iniciaMonticulo(&montMsj);

    while(TRUE){
        
        //Recoge todos los mensajes de la biblioteca que pueda y los guarda en monticulo binario
        while(montMsj.tamanno < MAX_MONTICULO-1){
            nuevoMsj.subtipo = 0;
            EXIT_ON_FAILURE(msgrcv(global.buzon, &nuevoMsj, MSG_SIZE(nuevoMsj), PARKING_MSG, IPC_NOWAIT));
            if(nuevoMsj.subtipo == 0) break;

            nuevoMsj.tipo = TIPO_COMANDO;
            mensjFormatted.clave = (nuevoMsj.subtipo == PARKING_MSGSUB_DESAPARCAR) ? PRIORITARIO + orden : NO_PRIORITARIO + orden;
            mensjFormatted.informacion = nuevoMsj;
            insertar(mensjFormatted, &montMsj);
            orden++;
        }

        // Cogemos el primer mensaje del monticulo y lo preparamos para enviar
        // o
        // Si el monticulo esta vacio, esperamos por el siguiente mensaje de la biblioteca
        if(!vacioMonticulo(montMsj)){
            eliminarMinimo(&montMsj, &mensjFormatted);
            siguienteMsj = mensjFormatted.informacion;
        }
        else{
            EXIT_ON_FAILURE(msgrcv(global.buzon, &siguienteMsj, MSG_SIZE(siguienteMsj), PARKING_MSG, 0));
            siguienteMsj.tipo = TIPO_COMANDO;
            orden++;
        }

        //Espera por la peticion de mensaje de un chofer para enviar el mensaje recogido
        EXIT_ON_FAILURE(msgrcv(global.buzon, &nuevoMsj, MSG_SIZE(nuevoMsj), TIPO_REQUEST, 0));
        EXIT_ON_FAILURE(msgsnd(global.buzon, &siguienteMsj, MSG_SIZE(siguienteMsj), 0));
    }
}

//----------- FUNCION DEL PROCESO TIMER -----------------------------------------------
void timerFunction(void){
    struct itimerval it;

    REDEFINE_SIGNAL(SIGALRM, timeIsUp);
    REDEFINE_SIGNAL(SIGINT, childHandler);

    READY(TRUE);

    it.it_value.tv_sec = it.it_interval.tv_sec = TIMEOUT_SEC;
    it.it_value.tv_usec = it.it_interval.tv_usec = 0;
    EXIT_ON_FAILURE(setitimer(ITIMER_REAL, &it, NULL));

    // La mascara del proceso solo permite responder a señales SIGALRM y SIGINT.
    // En caso de fallo, si recibe SIGINT antes que SIGALRM pasara a la handler
    // childHandler() y morirá sin esperar por el segundo pause(). 
	
    pause();    // pause() para esperar a SIGALRM
    pause();	// pause() para esperar a SIGINT
}

//----------- FUNCION DE LOS PROCESOS CHOFERES ----------------------------------------
void choferFunction(void){
    struct PARKING_mensajeBiblioteca mensj = {0,0,0};
    struct mensajeOrden orden;

    msgCarretera datosPeticiones[MAX_DATOS_COCHE] = {0};

    REDEFINE_SIGNAL(SIGINT,childHandler);

    READY(FALSE);

    while(TRUE){

        //Envia un mensaje al mailManager para pedir un nuevo comando
        mensj.tipo = TIPO_REQUEST;
        EXIT_ON_FAILURE(msgsnd(global.buzon, &mensj, MSG_SIZE(mensj), 0));
        EXIT_ON_FAILURE(msgrcv(global.buzon, &mensj, MSG_SIZE(mensj), TIPO_COMANDO, 0));

        //Si tiene que aparcar espera por la confirmacion del inmediatamente anterior para salir
        if(mensj.subtipo == PARKING_MSGSUB_APARCAR){
            EXIT_ON_FAILURE(msgrcv(global.buzon, &orden, MSG_SIZE(orden), TIPO_ORDEN(PARKING_getNUmero(mensj.hCoche), PARKING_getAlgoritmo(mensj.hCoche)), 0));
            PARKING_aparcar(mensj.hCoche, &datosPeticiones, commit, permisoAvance, permisoAvanceCommit);    
        }
        else{
            PARKING_desaparcar(mensj.hCoche, &datosPeticiones, permisoAvance, permisoAvanceCommit);
        }
    }
}

//----------- FUNCIONES DE CALLBACK DE INTERFAZ CON LA BIBLIOTECA ---------------------
void commit(HCoche c){
    struct mensajeOrden mensj;
    mensj.tipo = TIPO_ORDEN(PARKING_getNUmero(c) + 1, PARKING_getAlgoritmo(c));

    EXIT_ON_FAILURE(msgsnd(global.buzon, &mensj, MSG_SIZE(mensj), 0));
}

void permisoAvance(HCoche c){
    Carretera* carr = global.memCarreteras[PARKING_getAlgoritmo(c)];
    int posReserva, posOcupacionInicio, posOcupacionFin;

    // Si esta en carretera y se esta ocultando o si va a aparcar
    // siempre se da al coche permiso de avance.
    if(ESTA_EN_CARRETERA(c) && PARKING_getX(c) > 0){
        posReserva = PARKING_getX2(c);
        posOcupacionInicio = posOcupacionFin = PARKING_getX2(c);    // La zona critica de ocupacion es solo la casilla de delante.
    }
    else if(ESTA_DESAPARCANDO_AVANCE(c)){
        posReserva = PARKING_getX2(c) + PARKING_getLongitud(c) - 1; // Se reserva la ultima posicion del coche para evitar que
        posOcupacionInicio = PARKING_getX2(c);                      // otro coche avance dentro de su zona de desaparcamiento.
        posOcupacionFin = posReserva;                               // La zona critica de ocupacion es toda la longitud del coche
    }                                                               // en carretera.
    else{
        return;
    }

    reservarCarretera(c, carr, posReserva);
    pedirPermisoOcupacion(c, carr, posOcupacionInicio, posOcupacionFin);
}

void permisoAvanceCommit(HCoche c){
    Carretera*carr = global.memCarreteras[PARKING_getAlgoritmo(c)];
        
    //Desreservamos la acera, es zona de exclusion mutua a ojos de la biblioteca gracias a un semaforo interno
    if(ESTA_DESAPARCANDO_COMMIT(c)){
        bool* acera = global.memAceras[PARKING_getAlgoritmo(c)];
        memset(acera + PARKING_getX(c), FALSE, sizeof(bool)*PARKING_getLongitud(c));
    }

    actualizarCarretera(c, carr);

    recepcionPeticionReserva(c);
    recepcionPeticionOcupacion(c);

}

//----------- FUNCIONES DE ESCRITURA EN MEMORIA COMPARTIDA ----------------------------
void actualizarCarretera(HCoche c, Carretera* carr){
    int i;

    if(ESTA_DESAPARCANDO_COMMIT(c)){

        START_WRITE_CRITICAL_SECTION(c);

        for(i=PARKING_getX(c); i < PARKING_getX(c) + PARKING_getLongitud(c); i++){
            carr[i].e += OCUPADO;
            carr[i].ocupante = PARKING_getNUmero(c);
            if(i == PARKING_getX(c)+PARKING_getLongitud(c)-1){
                carr[i].e -= RESERVADO;
                carr[i].reservante = 0;
            }       
        }

        writeMemorySnapshot(PARKING_getAlgoritmo(c));

        END_WRITE_CRITICAL_SECTION(c);
    }
    else if(ESTA_APARCANDO_COMMIT(c)){

        START_WRITE_CRITICAL_SECTION(c);

        for(i = PARKING_getX(c); i < PARKING_getX(c) + PARKING_getLongitud(c); i++){
            carr[i].e -= OCUPADO;
            carr[i].ocupante = 0;
        }

        writeMemorySnapshot(PARKING_getAlgoritmo(c));

        END_WRITE_CRITICAL_SECTION(c);
    }
    else if(ESTA_EN_CARRETERA(c)){

        START_WRITE_CRITICAL_SECTION(c);
        //Si el coche no ha empezado a ocultarse
        if(PARKING_getX(c) >= 0){
            LOG("\n%d##%d: Voy a escribir OCUPADO en %d.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), PARKING_getX(c));
            carr[PARKING_getX(c)].e += (OCUPADO - RESERVADO);
            carr[PARKING_getX(c)].ocupante = PARKING_getNUmero(c);
            carr[PARKING_getX(c)].reservante = 0;
            LOG("\n%d##%d: He escrito OCUPADO en %d.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), PARKING_getX(c));
        }

        //Si el coche ya ha salido por la izquierda del todo
        if(PARKING_getX(c)+PARKING_getLongitud(c) < MAX_LONG_ROAD){
            
            LOG("\n%d##%d: Voy a liberar OCUPADO en %d.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), PARKING_getX(c)+PARKING_getLongitud(c));
            carr[PARKING_getX(c)+PARKING_getLongitud(c)].e -= OCUPADO;
            carr[PARKING_getX(c)+PARKING_getLongitud(c)].ocupante = 0;
            LOG("\n%d##%d: He liberado OCUPADO en %d.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), PARKING_getX(c)+PARKING_getLongitud(c));           
        }

        writeMemorySnapshot(PARKING_getAlgoritmo(c));

        END_WRITE_CRITICAL_SECTION(c);
    }

}

void reservarCarretera(HCoche c, Carretera* carr, int posicion){
    msgCarretera msg;

    START_WRITE_CRITICAL_SECTION(c);

    //Se comprueba si la posicion requerida ya esta reservada, en cuyo caso se envia mensaje a dicho coche para pedir permiso de avance
    while((carr[posicion].e == RESERVADO || carr[posicion].e == RESERVADO + OCUPADO) && carr[posicion].reservante != PARKING_getNUmero(c)){
        msg.tipo = TIPO_RESERVA(carr[posicion].reservante,PARKING_getAlgoritmo(c));
        msg.idReceptor = carr[posicion].reservante;
        msg.idRemitente = PARKING_getNUmero(c);

        LOG("\n(Carretera - %d#%d): Envio a %ld peticion para RESERVAR.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idReceptor);
        EXIT_ON_FAILURE(msgsnd(global.buzon, &msg, MSG_SIZE(msg), 0));   

        END_WRITE_CRITICAL_SECTION(c);

        EXIT_ON_FAILURE(msgrcv(global.buzon, &msg, MSG_SIZE(msg), TIPO_RESERVA(PARKING_getNUmero(c),PARKING_getAlgoritmo(c)), 0));
        LOG("\n(Carretera - %d#%d): He recibido de %ld confirmacion para RESERVAR.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idRemitente);

        START_WRITE_CRITICAL_SECTION(c);
        //Se vuelve a comprobar que la zona esta sin reservar ya que se ha estado durante la espera del permiso fuera de seccion critica
    }
        
    carr[posicion].e += RESERVADO;
    carr[posicion].reservante = PARKING_getNUmero(c);

    writeMemorySnapshot(PARKING_getAlgoritmo(c));

    END_WRITE_CRITICAL_SECTION(c);  
}

//----------- FUNCIONES DE LECTURA EN MEMORIA COMPARTIDA ------------------------------
void pedirPermisoOcupacion(HCoche c, Carretera* carr, int posInicial, int posFinal){
    msgCarretera msg;
    long idPeticionAvance;
    int i;

    START_READ_CRITICAL_SECTION(c);

    //  Comprobamos si dentro de las posiciones criticas a ocupar hay algun coche.
    //  De derecha a izquierda porque el trafico va en esa direccion.
    //  Si encontramos alguno le enviamos la peticion de ocupacion y esperamos
    //  a que nos de el permiso para ocupar.
    for(i = posFinal; i >= posInicial; i--){
        if(carr[i].e == OCUPADO || carr[i].e == RESERVADO + OCUPADO)
            break;
    }

    if(i >= posInicial){
        msg.idReceptor = carr[i].ocupante;
        msg.tipo = TIPO_OCUPACION(msg.idReceptor,PARKING_getAlgoritmo(c));
        idPeticionAvance = msg.idReceptor;
        msg.idRemitente = PARKING_getNUmero(c);
        msg.posXRequerida = posInicial;

        LOG("\n(Carretera - %d#%d): Envio a %ld peticion para OCUPAR.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idReceptor);
        EXIT_ON_FAILURE(msgsnd(global.buzon, &msg, MSG_SIZE(msg), 0));

        END_READ_CRITICAL_SECTION(c);

        // Es posible que mientras esperemos por ocupacion otro coche nos pida permiso. En ese caso guardamos la peticion
        // en la entructura interna para satisfacerla cuando podamos y volvemos a esperar al mensaje de permiso deseado.
        while(TRUE){
            LOG("\n(Carretera - %d#%d): Espero a %ld para OCUPAR.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idReceptor);

            EXIT_ON_FAILURE(msgrcv(global.buzon, &msg, MSG_SIZE(msg), TIPO_OCUPACION(PARKING_getNUmero(c),PARKING_getAlgoritmo(c)), 0));

            if(msg.idRemitente == idPeticionAvance){
                LOG("\n(Carretera - %d#%d): He recibido de %ld confirmacion para OCUPAR.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idRemitente);
                break;
            }
            else{
                guardarPeticionOcupacion(c, msg);
            }
        }
    }
    else{
        END_READ_CRITICAL_SECTION(c);
    }   
}

//----------- FUNCIONES DE GESTION DE PETICIONES AJENAS -------------------------------
void recepcionPeticionReserva(HCoche c){

    if(ESTA_APARCANDO_COMMIT(c))
        return;

    msgCarretera msg = {0, 0, 0, 0};

    EXIT_ON_FAILURE(msgrcv(global.buzon, &msg, MSG_SIZE(msg), TIPO_RESERVA(PARKING_getNUmero(c),PARKING_getAlgoritmo(c)), IPC_NOWAIT));
    if(msg.idReceptor != 0){
        LOG("\n(%d#%d): Envio OK a %ld buzon RESERVAS.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idRemitente);
        msg.tipo = TIPO_RESERVA(msg.idRemitente, PARKING_getAlgoritmo(c));
        msg.idReceptor= msg.idRemitente;
        msg.idRemitente = PARKING_getNUmero(c);
        EXIT_ON_FAILURE(msgsnd(global.buzon, &msg, MSG_SIZE(msg), 0));
    }
}

void recepcionPeticionOcupacion(HCoche c){

    if(ESTA_DESAPARCANDO_COMMIT(c))
        return;

    msgCarretera msg = {0, 0, 0, 0};
    msgCarretera* datosPeticiones = PARKING_getDatos(c);
    int posicionDesocupada = PARKING_getX(c) + PARKING_getLongitud(c);
    int i;

    //  Primero miramos si tenemos peticiones guardadas y si podemos atenderlas inmediatamente.
    for(i = 0; i < MAX_DATOS_COCHE; i++){
        if(datosPeticiones[i].idRemitente != 0 && (ESTA_APARCANDO_COMMIT(c) || datosPeticiones[i].posXRequerida == posicionDesocupada)){
            msg = datosPeticiones[i];

            msg.tipo = TIPO_OCUPACION(msg.idRemitente, PARKING_getAlgoritmo(c));
            msg.idReceptor = msg.idRemitente;
            msg.idRemitente = PARKING_getNUmero(c);

            LOG("\n(Carretera - %d#%d): Atiendo peticion de %ld guardada en datosCoche para OCUPAR\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idReceptor);

            EXIT_ON_FAILURE(msgsnd(global.buzon, &msg, MSG_SIZE(msg), 0));
            memset(&datosPeticiones[i], 0, sizeof(datosPeticiones[i]));
            memset(&msg, 0, sizeof(msg));
        }
    }

    //  Recoge todos los mensajes del buzon de peticiones de ocupacion dirigidas a el mismo.
    //  Las satisface inmediatamente si puede o las guarda en su estructura de datos interna.
    EXIT_ON_FAILURE(msgrcv(global.buzon, &msg, MSG_SIZE(msg), TIPO_OCUPACION(PARKING_getNUmero(c),PARKING_getAlgoritmo(c)), IPC_NOWAIT));
    while(msg.idReceptor != 0){
        if(ESTA_APARCANDO_COMMIT(c) || (msg.posXRequerida == posicionDesocupada)){  
            LOG("\n(Carretera - %d#%d): Envio OK a %ld buzon OCUPACION.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idRemitente);
            msg.tipo = TIPO_OCUPACION(msg.idRemitente,PARKING_getAlgoritmo(c));
            msg.idReceptor = msg.idRemitente;
            msg.idRemitente = PARKING_getNUmero(c);

            LOG("\n%d#%d): msg: idReceptor=%ld, idRemitente=%ld, posXRequerida=%d\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idReceptor, msg.idRemitente, msg.posXRequerida);
            EXIT_ON_FAILURE(msgsnd(global.buzon, &msg, MSG_SIZE(msg), 0));
        }
        else{
            LOG("\n%d#%d: Guardo Peticion Ocupacion en recepcion() com msg: idReceptor: %ld, idRemitente: %ld, posXRequerida: %d\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idReceptor, msg.idRemitente, msg.posXRequerida);
            guardarPeticionOcupacion(c, msg);
        }
        memset(&msg, 0, sizeof(msg));
        EXIT_ON_FAILURE(msgrcv(global.buzon, &msg, MSG_SIZE(msg), TIPO_OCUPACION(PARKING_getNUmero(c),PARKING_getAlgoritmo(c)), IPC_NOWAIT));
    }
}

void guardarPeticionOcupacion(HCoche c, msgCarretera msg){
    msgCarretera* datosPeticiones = PARKING_getDatos(c);

    int i = 0;
    while(datosPeticiones[i].idReceptor != 0 && i < MAX_DATOS_COCHE){
        i++;
    }
    if(i == MAX_DATOS_COCHE){
        EXIT("\n(Carretera - %d#%d): He recibido demasiadas peticiones de OCUPAR. Abortando...\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c));
    }
    datosPeticiones[i] = msg;
    LOG("\n(Carretera - %d#%d): He recibido de %ld de peticion de OCUPAR y guardo en datosCoche[%d].\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c), msg.idRemitente, i);
}

//----------- FUNCIONES DE ALGORITMOS DE POSICION DE APARCAMIENTO ---------------------
int primerAjuste(HCoche c){

    int longitud;
    int posInicial, longLibre, i=-1;
    bool* acera;

    acera = global.memAceras[PARKING_getAlgoritmo(c)];
        
    longitud = PARKING_getLongitud(c);
    while(i < MAX_LONG_ROAD){
        i++;
        longLibre = 0;
        while(acera[i] == FALSE && i < MAX_LONG_ROAD){
            i++;
            longLibre++;
            if(longLibre == longitud){
                posInicial = i-longitud;
                memset(acera + posInicial, TRUE, sizeof(bool)*longitud);
                return posInicial;
            }           
        }
    }
        
    return -1;
}

int mejorAjuste(HCoche c){

    int longitud, i, p, f, pa, fa;
    bool* acera;

    acera = global.memAceras[PARKING_getAlgoritmo(c)];
    longitud = PARKING_getLongitud(c);

    i = 0;
    p = f = pa = fa = -1;

    while(i<MAX_LONG_ROAD){
        if(acera[i] == FALSE){
            p = i;
            while(acera[i]==FALSE && i<MAX_LONG_ROAD) {i++;}
            f = i-1;

            if(pa==-1 && (f-p+1)>=longitud){
                pa = p;
                fa = f;
            }else if((f-p+1)>=longitud && (f-p)<(fa-pa)){
                pa = p;
                fa = f;
            }
        }
            i++;
    }


    if(pa != -1)
        memset(acera + pa, TRUE, sizeof(bool)*longitud);

    return pa;
}

int peorAjuste(HCoche c){

    int longitud, i, p, f, pa, fa;
    bool* acera;

    acera = global.memAceras[PARKING_getAlgoritmo(c)];
    longitud = PARKING_getLongitud(c);

    i = 0;
    p = f = pa = fa = -1;

    while(i<MAX_LONG_ROAD){
        if(acera[i] == FALSE){
            p = i;
            while(acera[i]==FALSE && i<MAX_LONG_ROAD) {i++;}
            f = i-1;

            if(pa==-1 && (f-p+1)>=longitud){
                pa = p;
                fa = f;
            }else if((f-p+1)>=longitud && (f-p)>(fa-pa)){
                pa = p;
                fa = f;
            }
        }
            i++;
    }


    if(pa != -1)
        memset(acera + pa, TRUE, sizeof(bool)*longitud);
        
    return pa;
}

int siguienteAjuste(HCoche c){

    static int start = -1;

    int posInicial, longLibre;
    bool* acera = global.memAceras[PARKING_getAlgoritmo(c)];
    int i = start;
    int contador = -1;
    int longitud = PARKING_getLongitud(c);

    while(acera[i+1]==FALSE && i>=0) i--;

    while(contador <= MAX_LONG_ROAD){
        i = (i+1 < MAX_LONG_ROAD) ? i+1 : 0;
        contador++;
        longLibre = 0;
        while(acera[i] == FALSE && contador <= MAX_LONG_ROAD && i < MAX_LONG_ROAD){
            i++;
            contador++;
            longLibre++;
            if(longLibre == longitud){
                posInicial = i-longitud;
                memset(acera + posInicial, TRUE, sizeof(bool)*longitud);
                start = posInicial-1;

                return posInicial;
            }           
        }
    }
    return -1;
}

//----------- MANEJADORAS DE LOS PROCESOS ---------------------------------------------
void freeResources(int ss){

    //  Si el hijo entra en esta funcion es a causa de un error.
    //  Envia SINGINT al padre y muere con codigo de error. 
    if(global.isChild == TRUE){
        PRINT_ERROR(kill(SIGALRM,getppid()));
        pause();
    }

    int i;

   if(ss == SIGINT){
        PRINT_ERROR(waitpid(global.timer,NULL,0));
        PRINT_ERROR(waitpid(global.mailManager,NULL,0));
        if(global.chofers != NULL){
            for(i=0; global.chofers[i] != 0; i++){
                PRINT_ERROR(waitpid(global.chofers[i],NULL,0));
            }
        }
    }else{
        KILL_PROCESS(global.timer,SIGINT);
        KILL_PROCESS(global.mailManager,SIGINT);
        if(global.chofers != NULL){
            for(i=0; global.chofers[i] != 0; i++){
                KILL_PROCESS(global.chofers[i],SIGINT);
            }
        }
    }
    if(global.chofers != NULL)
        free(global.chofers);

    if(-1 != global.sem)
        PRINT_ERROR(semctl(global.sem,0,IPC_RMID));
    if(-1 != global.buzon)
        PRINT_ERROR(msgctl(global.buzon,IPC_RMID,NULL));
    if(-1 != global.mem && NULL != global.memp)
        PRINT_ERROR(shmdt(global.memp));
    if(-1 != global.mem)
        PRINT_ERROR(shmctl(global.mem,IPC_RMID,NULL));
        
    if(global.debug){
        for(i = PRIMER_AJUSTE; i <= PEOR_AJUSTE; i++)
            fclose(global.dump_file[i]);
    }

    exit(EXIT_SUCCESS);
}

void timeIsUp(int ss){
    PARKING_fin(1);
}

void childHandler(int ss){
    exit(EXIT_SUCCESS);
}

//----------- FUNCIONES DE USO DE SEMAFOROS SYSTEM V ----------------------------------

/*
 *  @params
 *      pos: semaforo dentro del vector
 *      typeOp: WAIT, WAIT0 o SIGNAL
 *      quantity: cantidad de operaciones
 */
void semops(int pos, int typeOp, int quantity){
    struct sembuf sops;
        
    sops.sem_op = typeOp*quantity;
    sops.sem_num = pos;
    sops.sem_flg = 0;
        
    EXIT_ON_FAILURE(semop(global.sem,&sops,1));
}

void permisoEntradaEscrituraAtomico(int semWrite){
    struct sembuf sops[2];

    sops[0].sem_op = WAIT0;
    sops[0].sem_num = semWrite;
    sops[0].sem_flg = 0;

    sops[1].sem_op = SIGNAL;
    sops[1].sem_num = semWrite;
    sops[1].sem_flg = 0;
        
    EXIT_ON_FAILURE(semop(global.sem,sops,2));
}

void permisoEntradaLecturaAtomico(int semWrite, int semRead){
    struct sembuf sops[2];

    sops[0].sem_op = WAIT0;
    sops[0].sem_num = semWrite;
    sops[0].sem_flg = 0;

    sops[1].sem_op = SIGNAL;
    sops[1].sem_num = semRead;
    sops[1].sem_flg = 0;
        
    EXIT_ON_FAILURE(semop(global.sem,sops,2));
}

//----------- FUNCIONES DE DEPURACION -------------------------------------------------
void writeMemorySnapshot(int algoritmo){

        if(!global.debug)
            return;

        int i;
        Carretera carretera;

        for(i = 0; i < MAX_LONG_ROAD; i++){
            carretera.e = global.memCarreteras[algoritmo][i].e;
            fprintf(global.dump_file[algoritmo], "%2d:%s ", i, getEstadoString(carretera.e));
        }
        fprintf(global.dump_file[algoritmo], "\n");

        for(i = 0; i < MAX_LONG_ROAD; i++){
            carretera.ocupante = global.memCarreteras[algoritmo][i].ocupante;
            fprintf(global.dump_file[algoritmo], "%7zu ", carretera.ocupante);
        }
        fprintf(global.dump_file[algoritmo], "\n");

        for(i = 0; i < MAX_LONG_ROAD; i++){
            carretera.reservante = global.memCarreteras[algoritmo][i].reservante;
            fprintf(global.dump_file[algoritmo], "%7zu ", carretera.reservante);
        }
        fprintf(global.dump_file[algoritmo], "\n\n");

        fflush(global.dump_file[algoritmo]);
}

char* getEstadoString(estado e){
        switch(e){
            case LIBRE:     return "LIBR";
            case OCUPADO:   return "OCUP";
            case RESERVADO: return "RESE";
            case OC_RES:    return "OCRE";
        }
}

char* getDumpPath(int algoritmo){
        switch(algoritmo){
            case PRIMER_AJUSTE:     return DUMP_PATH_PRIMER;
            case SIGUIENTE_AJUSTE:  return DUMP_PATH_SIGUIENTE;
            case MEJOR_AJUSTE:      return DUMP_PATH_MEJOR;
            case PEOR_AJUSTE:       return DUMP_PATH_PEOR;
        }
}

//----------- ESTRUCTURA DE DATOS MONTICULO BINARIO -----------------------------------
void iniciaMonticulo(Monticulo *m)
{
    m->tamanno = 0;
}

int vacioMonticulo(Monticulo m)
{
        if(m.tamanno == 0)
            return 1;

        else
            return 0;
}


int insertar(tipoElemento x, Monticulo *m)
{
        if (m->tamanno == MAX_MONTICULO-1)
            return 1;

        m->tamanno++;
        m->elemento[m->tamanno] = x;
        filtradoAscendente(m, m->tamanno);
        return 0;
}

int eliminarMinimo(Monticulo *m, tipoElemento *minimo)
{
        if(vacioMonticulo(*m))
            return 1;

        minimo->clave = m->elemento[1].clave;
        minimo->informacion = m->elemento[1].informacion;

        m->elemento[1] = m->elemento[m->tamanno];
        m->tamanno--;
        filtradoDescendente(m, 1);
        return 0;
}

void decrementarClave(int pos, tipoClave cantidad, Monticulo *m)
{
        if(pos < 1 || pos > m->tamanno)
            return;

        m->elemento[pos].clave -= cantidad;
        filtradoAscendente(m, pos);
}

void incrementarClave(int pos, int cantidad, Monticulo *m)
{
        if (pos < 1 || pos > m->tamanno)
            return;

        m->elemento[pos].clave += cantidad;
        filtradoDescendente(m, pos);
}

int esMonticulo(Monticulo m)
{
        int i, posicion;

        for(i = (m.tamanno/2)+1; i <= m.tamanno; i++)
        {
            posicion = i;
            while(posicion > 1)
            {
                if(m.elemento[posicion].clave < m.elemento[posicion/2].clave)
                    return 0;

                posicion /= 2;
            }
        }

        return 1;
}

void filtradoDescendente(Monticulo *m, int i)
{
        int posHijo = 2*i;

        if(posHijo > m->tamanno)
            return;

        else
        {
            if((posHijo < m->tamanno) && (m->elemento[posHijo].clave > m->elemento[posHijo+1].clave))
                posHijo++;

            if(m->elemento[i].clave > m->elemento[posHijo].clave)
            {
                tipoElemento temp = m->elemento[i];
                m->elemento[i] = m->elemento[posHijo];
                m->elemento[posHijo] = temp;

                filtradoDescendente(m, posHijo);		
            }
            else
                return;
        }
}

void filtradoAscendente(Monticulo *m, int i)
{
        int posPadre = i/2;

        if((posPadre < 1) || (i > m->tamanno))
            return;

        else
        {
            if(m->elemento[i].clave < m->elemento[posPadre].clave)
            {
                tipoElemento temp = m->elemento[i];
                m->elemento[i] = m->elemento[posPadre];
                m->elemento[posPadre] = temp;

                filtradoAscendente(m, posPadre);
            }
            else
                return;
        }
}

void crearMonticulo(Monticulo *m)
{
        int i;

        for(i = m->tamanno/2; i >= 1; i--)
        {
            filtradoDescendente(m, i);
        }
}
