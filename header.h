#ifndef __HEADER_H
#define __HEADER_H

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

typedef short bool;

typedef enum {LIBRE,OCUPADO,RESERVADO,OC_RES} estado;

typedef struct{
    estado e;
    int ocupado, reservado;
}Carretera;

typedef struct{
    long tipo;
    long idReceptor;
    long idRemitente;
    int posXRequerida;
}msgCarretera;

struct mensajeOrden{
    long tipo;
    long orden;
};
union semun{
    int val;
    struct semid_ds*buf;
    unsigned short *array;
};

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


//---------------GESTION IPCS------------------------------------------------
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

#define SEM_START           (PARKING_getNSemAforos())
#define SEM_WRITE(algoritmo)    (PARKING_getNSemAforos() + 1 +     (algoritmo))
#define SEM_READ(algoritmo)     (PARKING_getNSemAforos() + 1 + 4 + (algoritmo))

//-----------------------------------------------------------------------------------------

//---------------PROCESOS Y SEÑALES------------------------------------------------
#define CREATE_PROCESS(value)    EXIT_ON_FAILURE(value = fork())

#define IF_CHILD(value) if(value == 0)

#define KILL_PROCESS(pid,signal)                \
    do{                                         \
        PRINT_ERROR(kill(pid,signal));          \
        PRINT_ERROR(waitpid(pid,NULL,0));       \
    }while(0)

#define READY(isSIGALARMsensitive)                                      \
    do{                                                                 \
        sigset_t initialSet;                                            \
        semops(SEM_START,WAIT,1);                                       \
        semops(SEM_START,WAIT0,0);                                      \
        EXIT_ON_FAILURE(sigfillset(&initialSet));                       \
        EXIT_ON_FAILURE(sigdelset(&initialSet,SIGINT));                 \
    if(isSIGALARMsensitive){                                            \
        sigdelset(&initialSet,SIGALRM);                                 \
    }                                                                   \
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
//-----------------------------------------------------------------------------------------

//---------------SECCIONES CRITICAS------------------------------------------------
#define START_WRITE_CRITICAL_SECTION(coche)                                                             \
    do{                                                                                                 \
        permisoEntradaEscrituraAtomico(SEM_WRITE(PARKING_getAlgoritmo(coche)));                         \
        semops(SEM_READ(PARKING_getAlgoritmo(coche)), WAIT0, 0);                                        \
        LOG("\n%d###%d: He entrado en SemWrite.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c));     \
    }while(0)

#define END_WRITE_CRITICAL_SECTION(coche)                                                               \
    do{                                                                                                 \
        semops(SEM_WRITE(PARKING_getAlgoritmo(coche)),WAIT,1);                                          \
        LOG("\n%d###%d: He salido en SemWrite.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c));      \
    }while(0)

#define START_READ_CRITICAL_SECTION(coche)                                                                              \
    do{                                                                                                                 \
        permisoEntradaLecturaAtomico(SEM_WRITE(PARKING_getAlgoritmo(coche)), SEM_READ(PARKING_getAlgoritmo(coche)));    \
        LOG("\n%d###%d: He entrado en SemRead.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c));                      \
    }while(0)

#define END_READ_CRITICAL_SECTION(coche)                                                            \
    do{                                                                                             \
        semops(SEM_READ(PARKING_getAlgoritmo(coche)),WAIT,1);                                       \
        LOG("\n%d###%d: He salido en SemRead.\n", PARKING_getAlgoritmo(c), PARKING_getNUmero(c));   \
    }while(0)
//-----------------------------------------------------------------------------------------

//---------------MOVIMIENTOS------------------------------------------------
#define ESTA_DESAPARCANDO_AVANCE(coche)     (PARKING_getY(coche)  == 1 && PARKING_getY2(coche) == 2)
#define ESTA_DESAPARCANDO_COMMIT(coche)     (PARKING_getY2(coche) == 1 && PARKING_getY(coche)  == 2)
#define ESTA_APARCANDO_AVANCE(coche)        (PARKING_getY2(coche) == 1 && PARKING_getY(coche)  == 2)
#define ESTA_APARCANDO_COMMIT(coche)        (PARKING_getY(coche)  == 1 && PARKING_getY2(coche) == 2)
#define ESTA_EN_CARRETERA(coche)            (PARKING_getY2(coche) == 2 && PARKING_getY(coche)  == 2)
//-----------------------------------------------------------------------------------------

//---------------ERRORES Y LOG------------------------------------------------
#define USAGE_ERROR_MSG     "./parking <velocidad> <numChoferes> [D] [PA | PD]"
#define NOT_ENOUGH_CHOF_MSG "NUmero de chOferes insuficiente"
#define DEBUG_WARNING       "WARNING: entrando al programa sin modo debug\n"    
#define IPC_WARNING         "\n[%s:%d:%s] WARNING: IPC type %d couldn't be removed.\n"

#define DUMP_PATH_PRIMER    "_dump_primer"
#define DUMP_PATH_SIGUIENTE "_dump_siguiente"
#define DUMP_PATH_MEJOR     "_dump_mejor"
#define DUMP_PATH_PEOR      "_dump_peor"

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

#define EXIT_ON_FAILURE(returnValue)            \
    do{                                         \
        if((returnValue) == -1){                \
            PRINT_ERROR(-1);                    \
            freeResources(-1);                  \
        }                                       \
    }while(0)

#define EXIT_IF_NULL(returnValue)               \
    do{                                         \
        if((returnValue) == NULL){              \
            PRINT_ERROR(-1);                    \
            freeResources(-1);                  \
        }                                       \
    }while(0)
//-----------------------------------------------------------------------------------------
            
//---------------MONTICULO------------------------------------------------
#define MAX_MONTICULO 100

typedef int tipoClave ;
typedef struct PARKING_mensajeBiblioteca tipoInfo ;

typedef struct
{ tipoClave clave;
  tipoInfo  informacion;
} tipoElemento;

typedef struct
{ tipoElemento elemento[MAX_MONTICULO];
    int tamanno;
} Monticulo;

void iniciaMonticulo(Monticulo *m);
int vacioMonticulo(Monticulo m);
int insertar(tipoElemento x, Monticulo *m);
int eliminarMinimo(Monticulo *m, tipoElemento *minimo);

void filtradoDescendente(Monticulo *m, int i);
void filtradoAscendente(Monticulo *m, int i);

void crearMonticulo(Monticulo *m);
//-----------------------------------------------------------------------------------------

#endif


