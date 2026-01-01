#ifndef MASTER_H
#define MASTER_H

#define _READLINE_READLINE_H
#define _READLINE_HISTORY_H
#define _READLINEH

#include "../../utils/src/utils/utils.h"


typedef enum{
    READY,
    EXEC,
    EXIT
} t_estado;


typedef struct {
    int id;
    char path[PATH_MAX];
    int prioridad;
    t_estado estado;
    int socket_qc;           // para reenviar lecturas/fin
    int pc;                  // por si el worker devuelve PC al desalojar (futuro)
} t_query;

typedef struct {
    int id;
    int socket_worker;
    int id_query;            // -1 si libre
    bool libre;
} t_worker;

t_log* log_master;
t_config* config_master;

// Colas
t_queue* cola_ready;
t_queue* cola_exec;
t_queue* cola_exit;

// Workers
t_list* lista_querys;
t_list* lista_workers;
t_queue* cola_workers_libres;

// Mutex
pthread_mutex_t mutex_pid_query;
pthread_mutex_t mutex_cola_ready;
pthread_mutex_t mutex_cola_exec;
pthread_mutex_t mutex_cola_exit;
pthread_mutex_t mutex_lista_workers;
pthread_mutex_t mutex_workers_libres;
pthread_mutex_t mutex_lista_querys;
pthread_mutex_t mutex_aging;

// Variables de condicion
pthread_cond_t cond_aging;

// Semáforos
sem_t sem_query_ready;
sem_t sem_worker_disponible;

// Config
char* ALGORITMO_PLANIFICACION;
int TIEMPO_AGING;
char* PUERTO_ESCUCHA;

// Otros
static void planificador_corto_plazo(void);
static void asignar_query_a_worker(t_query* q, t_worker* w);
static t_worker* pop_worker_libre(void);
static void manejar_conexiones_entrantes(void);
static void comunicacion_con_query_control(int* socket_qc);
static void comunicacion_con_worker(int* socket_w);
#endif