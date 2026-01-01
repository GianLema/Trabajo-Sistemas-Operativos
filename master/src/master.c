#include "./master.h"

/*──────────────────────────────────────────
 *          PROTOTIPOS INTERNOS
 *──────────────────────────────────────────*/
void planificador_corto_plazo(void);
void asignar_query_a_worker(t_query* q, t_worker* w);
t_query* elegir_query_por_prioridad(t_queue* q);
t_worker* pop_worker_libre(void);
void manejar_conexiones_entrantes(void);
void comunicacion_con_query_control(int* socket_qc);
void comunicacion_con_worker(int* socket_w);
void sacar_de_cola_exec(t_query* q);
static void* hilo_aging_query(void* arg); // ⬅️ CAMBIO: Nuevo prototipo para Aging por Query
static void evaluar_desalojo_si_corresponde(t_query* q_nueva);

/*──────────────────────────────────────────
 *          VARIABLES GLOBALES
 *──────────────────────────────────────────*/
int pid_query = 0;
int fd_master = -1;
bool finalizar_master = false;

/* ────────────────────────────────
   Planificador 
   ──────────────────────────────── */
   static void planificador_corto_plazo(void){
    while (!finalizar_master) {
        sem_wait(&sem_query_ready);
        sem_wait(&sem_worker_disponible);

        pthread_mutex_lock(&mutex_cola_ready);
        t_query* q = NULL;

        if (strcmp(ALGORITMO_PLANIFICACION, "PRIORIDADES") == 0) {
            q = elegir_query_por_prioridad(cola_ready);
        } else {
            q = queue_pop(cola_ready); // FIFO
        }

        pthread_mutex_unlock(&mutex_cola_ready);

        if (q == NULL) continue; // nada para planificar (raro, pero por las dudas)

        t_worker* w = pop_worker_libre();

        if (w != NULL) {
            asignar_query_a_worker(q, w);
        } else {
            // por si en algún caso raro no había worker libre igual
            pthread_mutex_lock(&mutex_cola_ready);
            queue_push(cola_ready, q);
            pthread_mutex_unlock(&mutex_cola_ready);
        }
    }
}

/* ────────────────────────────────
   Asignación READY → EXEC
   ──────────────────────────────── */
   static void asignar_query_a_worker(t_query* q, t_worker* w){
    q->estado = EXEC;
    w->id_query = q->id;
    w->libre = false;

    pthread_mutex_lock(&mutex_cola_exec);
    queue_push(cola_exec,q);
    pthread_mutex_unlock(&mutex_cola_exec);

    log_info(log_master,"## Se envía la Query %d (%d) al Worker %d",q->id,q->prioridad,w->id);

    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer,q->id);
    cargar_string_al_buff(buffer,q->path);
    cargar_int_al_buff(buffer,q->pc);
    t_paquete* paquete = crear_super_paquete(MASTER_INICIA_QUERY_EN_WORKER,buffer);
    enviar_paquete(paquete,w->socket_worker);
    eliminar_paquete(paquete);
}

t_query* elegir_query_por_prioridad(t_queue* ready) {
    if (queue_is_empty(ready)) return NULL;

    int cant = queue_size(ready);
    t_queue* aux = queue_create();

    t_query* mejor = NULL;
    int mejor_prio = INT_MAX;

    // Recorro toda la cola READY
    for (int i = 0; i < cant; i++) {
        t_query* q = queue_pop(ready);

        if (q->estado != READY) {
            // Por las dudas, si hubiera algo raro, lo mantengo igual
            queue_push(aux, q);
            continue;
        }

        if (q->prioridad < mejor_prio) {
            // Guardo la mejor encontrada
            if (mejor != NULL)
                queue_push(aux, mejor);
            mejor = q;
            mejor_prio = q->prioridad;
        } else {
            queue_push(aux, q);
        }
    }

    // Devuelvo todo menos la mejor a ready
    while (!queue_is_empty(aux)) {
        queue_push(ready, queue_pop(aux));
    }
    queue_destroy(aux);

    return mejor;  // puede ser NULL si no había READY válidas
}

/* ────────────────────────────────
   FUNCIONES DE ESTRUCTURAS
   ──────────────────────────────── */
t_worker* crear_estructura_worker(int id_worker, int socket_worker) {
    t_worker* w = malloc(sizeof(t_worker));
    w->id = id_worker;
    w->socket_worker = socket_worker;
    w->id_query = -1;
    w->libre = true;

    pthread_mutex_lock(&mutex_lista_workers);
    list_add(lista_workers,w);
    pthread_mutex_unlock(&mutex_lista_workers);

    pthread_mutex_lock(&mutex_workers_libres);
    queue_push(cola_workers_libres,w);
    pthread_mutex_unlock(&mutex_workers_libres);
    sem_post(&sem_worker_disponible);

    return w;
}

/* ────────────────────────────────
   Queries: crear y encolar en READY
   ──────────────────────────────── */
   t_query* crear_estructura_query(const char* path,int prioridad,int socket_qc){
    t_query* q = malloc(sizeof(t_query));
    pthread_mutex_lock(&mutex_pid_query);
    q->id = pid_query++;
    pthread_mutex_unlock(&mutex_pid_query);
    strncpy(q->path,path,sizeof(q->path)-1);
    q->path[sizeof(q->path)-1]='\0';
    q->prioridad = prioridad;
    q->estado = READY;
    q->socket_qc = socket_qc;
    q->pc = 0;

    pthread_mutex_lock(&mutex_lista_querys);
    list_add(lista_querys,q);
    pthread_mutex_unlock(&mutex_lista_querys);
    return q;
}

void push_ready(t_query* q) {
    q->estado = READY;

    pthread_mutex_lock(&mutex_cola_ready);
    queue_push(cola_ready, q);
    pthread_mutex_unlock(&mutex_cola_ready);

    /* Desalojo por prioridad y lanzamiento de hilo de aging por Query */ // ⬅️ CAMBIO: Reemplazo del bloque completo (293-364)
    if (strcmp(ALGORITMO_PLANIFICACION, "PRIORIDADES") == 0) {
        if (TIEMPO_AGING > 0) {
            pthread_t th_aging;
            pthread_create(&th_aging, NULL, hilo_aging_query, q);
            pthread_detach(th_aging);
        }
        evaluar_desalojo_si_corresponde(q);
    }

    // Despierto al planificador
    sem_post(&sem_query_ready);
    // pthread_cond_signal(&cond_aging); // ⬅️ CAMBIO: Se elimina la señal al hilo global de aging
}

static t_worker* pop_worker_libre(void){
    pthread_mutex_lock(&mutex_workers_libres);
    t_worker* w = queue_is_empty(cola_workers_libres)
                    ? NULL
                    : queue_pop(cola_workers_libres);
    pthread_mutex_unlock(&mutex_workers_libres);
    return w;
}

/*──────────────────────────────────────────
 * AGING
 *──────────────────────────────────────────*/
static void* hilo_aging_query(void* arg) { // ⬅️ CAMBIO: Se renombra de 'aging_ready' y se cambia la firma a 'void*'

    t_query* q = (t_query*) arg;

    while (!finalizar_master && TIEMPO_AGING > 0) {

        /* Dormir TIEMPO_AGING ms (sin espera activa) */
        struct timespec ts;
        ts.tv_sec  = TIEMPO_AGING / 1000;
        ts.tv_nsec = (TIEMPO_AGING % 1000) * 1000000;
        nanosleep(&ts, NULL); // Nota: `nanosleep` requiere <time.h> si no está ya incluido.

        if (finalizar_master) break;

        /* Verificar que la query siga en READY */
        pthread_mutex_lock(&mutex_cola_ready);
        if (q->estado != READY) {
            pthread_mutex_unlock(&mutex_cola_ready);
            break;  // salió de READY → se termina el hilo de aging de esta query
        }

        if (q->prioridad > 0) {
            int anterior = q->prioridad;
            q->prioridad--;
            /* Log obligatorio: cambio de prioridad */
            log_info(log_master,
                     "## %d Cambio de prioridad: %d - %d",
                     q->id, anterior, q->prioridad);
        }
        pthread_mutex_unlock(&mutex_cola_ready);

        /* Cada cambio de prioridad puede provocar un desalojo */
        if (strcmp(ALGORITMO_PLANIFICACION, "PRIORIDADES") == 0) {
            evaluar_desalojo_si_corresponde(q);
        }
    }

    return NULL;
}


/* ────────────────────────────────
   Globals auxiliares para búsquedas
   ──────────────────────────────── */
//    static int global_socket_qc_busqueda;
//    static int global_socket_worker_busqueda;
//    static int global_id_query_busqueda;

/* ────────────────────────────────
   Funciones auxiliares para list_find
   ──────────────────────────────── */

//    * t_person* find_by_name_contains(t_list* people, char* substring) {
// 	*     bool _name_contains(void* ptr) {
// 	*         t_person* person = (t_person*) ptr;
// 	*         return string_contains(person->name, substring);
// 	*     }
// 	*     return list_find(people, _name_contains);
// 	* }
   t_query* buscar_query_por_socket(t_list* queries, int socket_q){
        bool match_qc_by_socket(void* element) {
        t_query* q = (t_query*) element;
        return q->socket_qc == socket_q;
        }
    return list_find(queries, match_qc_by_socket);
   }
   
    t_worker* buscar_worker_por_socket(t_list* workers, int socket_w){
        bool match_worker_by_socket(void* element) {
        t_worker* w = (t_worker*) element;
        return w->socket_worker == socket_w;
        }
    return list_find(workers, match_worker_by_socket);
   }


    t_query* buscar_query_por_id(t_list* queries, int id_q){
        bool match_qc_by_id(void* element) {
        t_query* q = (t_query*) element;
        return q->id == id_q;
        }
    return list_find(queries, match_qc_by_id);
   }
    
   t_worker* buscar_worker_por_query_id(int id_q) {
    bool match_worker_by_qid(void* element) {
        t_worker* w = (t_worker*) element;
        return w->id_query == id_q;
    }
    return list_find(lista_workers, match_worker_by_qid);
}

/*──────────────────────────────────────────
 *   MANEJO EJEC → sacar query de cola_exec
 *──────────────────────────────────────────*/
void sacar_de_cola_exec(t_query* q) {
    pthread_mutex_lock(&mutex_cola_exec);

    int      n   = queue_size(cola_exec);
    t_queue* aux = queue_create();

    for (int i = 0; i < n; i++) {
        t_query* tmp = queue_pop(cola_exec);
        if (tmp->id != q->id)
            queue_push(aux, tmp);
    }

    while (!queue_is_empty(aux)) {
        queue_push(cola_exec, queue_pop(aux));
    }
    queue_destroy(aux);

    pthread_mutex_unlock(&mutex_cola_exec);
}

/*──────────────────────────────────────────
 *     OBTENER PEOR QUERY EN EXEC (PRIO MAX)
 *──────────────────────────────────────────*/
static t_query* obtener_peor_query_en_exec(void) {
    pthread_mutex_lock(&mutex_cola_exec);

    if (queue_is_empty(cola_exec)) {
        pthread_mutex_unlock(&mutex_cola_exec);
        return NULL;
    }

    int      n   = queue_size(cola_exec);
    t_queue* aux = queue_create();

    t_query* peor       = NULL;
    int      peor_prio  = INT_MIN;

    for (int i = 0; i < n; i++) {
        t_query* q = queue_pop(cola_exec);

        // Solo consideramos EXEC
        if (q->estado == EXEC && q->prioridad > peor_prio) {
            peor      = q;
            peor_prio = q->prioridad;
        }

        queue_push(aux, q);
    }

    while (!queue_is_empty(aux)) {
        queue_push(cola_exec, queue_pop(aux));
    }
    queue_destroy(aux);

    pthread_mutex_unlock(&mutex_cola_exec);

    return peor;
}

/*──────────────────────────────────────────
 *       DESALOJO POR PRIORIDADES
 *──────────────────────────────────────────*/
static void evaluar_desalojo_si_corresponde(t_query* q_nueva) {
    if (strcmp(ALGORITMO_PLANIFICACION, "PRIORIDADES") != 0)
        return;

    // Si hay worker libre, no hace falta desalojar a nadie
    pthread_mutex_lock(&mutex_workers_libres);
    bool hay_worker_libre = !queue_is_empty(cola_workers_libres);
    pthread_mutex_unlock(&mutex_workers_libres);
    if (hay_worker_libre)
        return;

    // Busco la peor query en EXEC (mayor número de prioridad)
    t_query* peor = obtener_peor_query_en_exec();
    if (!peor)
        return;

    if (q_nueva->prioridad >= peor->prioridad) {
        // La nueva no es mejor que la peor en EXEC → no se desaloja
        return;
    }

    // Hay que desalojar "peor"
    t_worker* w = buscar_worker_por_query_id(peor->id);
    if (!w)
        return;
    
    log_info(log_master,
             "## Se desaloja la Query %d (%d) del Worker %d - Motivo: PRIORIDAD",
             peor->id, peor->prioridad, w->id);

    t_buffer* b = crear_buffer();
    cargar_int_al_buff(b, peor->id); // Worker sabe qué Query desalojar

    t_paquete* p = crear_super_paquete(WORKER_DESALOJAR_QUERY, b);
    enviar_paquete(p, w->socket_worker);
    eliminar_paquete(p);

    // log_info(log_master,
    //          "## Se solicita desalojo de la Query %d al Worker %d",
    //          peor->id, w->id); 
}

/* ────────────────────────────────
   Comunicación con QC / Worker
   ──────────────────────────────── */

   void manejar_desconexion_worker(int socket_worker) {
    pthread_mutex_lock(&mutex_lista_workers);
    for (int i = 0; i < list_size(lista_workers); i++) {
        t_worker* w = list_get(lista_workers, i);
        if (w->socket_worker == socket_worker) {
            int qid = w->id_query;
            log_info(log_master,
                     "## Se desconecta el Worker %d - Se finaliza la Query %d - Cantidad total de Workers: %d",
                     w->id, qid, list_size(lista_workers));

            // Si tenía una Query ejecutando, la marcamos EXIT y
            // notificamos al Query Control con error genérico.
            if (qid != -1) {
                t_query* q = buscar_query_por_id(lista_querys, qid);
                if (q) {
                    // La query termina con error
                    sacar_de_cola_exec(q);
                    q->estado = EXIT;

                    if (q->socket_qc > 0) {
                    t_buffer* bfin = crear_buffer();
                    cargar_string_al_buff(bfin, "Error: Worker desconectado");
                    t_paquete* p = crear_super_paquete(QC_MSG_FIN, bfin);
                    enviar_paquete(p, q->socket_qc);
                    eliminar_paquete(p);
                    }
                }
            }

            w->libre    = false;
            w->id_query = -1;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_lista_workers);
}

   static void comunicacion_con_query_control(int* socket_qc){
    int sock_qc = *(int*)socket_qc;
    // global_socket_qc_busqueda = sock_qc;
    bool conectado = true;
    while(conectado){
        op_code cod = recibir_operacion(sock_qc);
        if(cod == -1){
            // desconexión
            pthread_mutex_lock(&mutex_lista_querys);
            t_query* q = buscar_query_por_socket(lista_querys, sock_qc);
            pthread_mutex_unlock(&mutex_lista_querys);
            if(q){
                pthread_mutex_lock(&mutex_lista_workers);
                int cant = list_size(lista_workers);
                pthread_mutex_unlock(&mutex_lista_workers);

                log_info(log_master,
                    "## Se desconecta un Query Control. Se finaliza la Query %d con prioridad %d. Nivel multiprocesamiento %d",
                    q->id, q->prioridad, cant);
                q->estado = EXIT;
            }
            conectado = false;
            close(sock_qc);
            break;
        }
    }
}

static void comunicacion_con_worker(int* socket_w){
    int sock_w = *(int*)socket_w;
    // global_socket_worker_busqueda = sock_w;
    bool key = true;
    while (key) {
        op_code cod_op_w = recibir_operacion(sock_w);
        if (cod_op_w == -1) {
            manejar_desconexion_worker(sock_w);
            close(sock_w);
            break;
        }


        t_worker* w = buscar_worker_por_socket(lista_workers, sock_w);

        switch (cod_op_w) {
        case WORKER_ENVIA_LECTURA: {
            t_buffer* b = recibir_buffer(sock_w);
            int qid = extraer_int_buffer(b);
            char* file = extraer_string_buffer(b);
            char* tag  = extraer_string_buffer(b);
            char* contenido = extraer_string_buffer(b);
            free(b);

            // global_id_query_busqueda = qid;
            t_query* q = buscar_query_por_id(lista_querys, qid);
            if (q && w) {
                log_info(log_master,
                         "## Se envía un mensaje de lectura de la Query %d en el Worker %d al Query Control",
                         q->id, w->id);

                t_buffer* bsend = crear_buffer();
                cargar_string_al_buff(bsend, contenido);
                t_paquete* p = crear_super_paquete(QC_MSG_READ, bsend);
                enviar_paquete(p, q->socket_qc);
                eliminar_paquete(p);
            }

            free(file);
            free(tag);
            free(contenido);
        } break;

        case WORKER_FIN_QUERY: {
            t_buffer* b = recibir_buffer(sock_w);
            int qid = extraer_int_buffer(b);
            free(b);

            // global_id_query_busqueda = qid;
            t_query* q = buscar_query_por_id(lista_querys, qid);
            if (q && w) {
                log_info(log_master, "## Se terminó la Query %d en el Worker %d", q->id, w->id);
                // Sacarla de EXEC
                sacar_de_cola_exec(q);
                q->estado = EXIT;
                t_buffer* bfin = crear_buffer();
                cargar_string_al_buff(bfin, "OK");
                t_paquete* p = crear_super_paquete(QC_MSG_FIN, bfin);
                enviar_paquete(p, q->socket_qc);
                eliminar_paquete(p);
                
                // LIBERAR EL WORKER para que pueda tomar otra query
                w->libre = true;
                w->id_query = -1;
                
                pthread_mutex_lock(&mutex_workers_libres);
                queue_push(cola_workers_libres, w);
                pthread_mutex_unlock(&mutex_workers_libres);
                
                sem_post(&sem_worker_disponible);
                
                // log_info(log_master, "## Worker %d liberado y listo para nueva query", w->id);
            }
        } break;

        case WORKER_DESALOJAR_QUERY: {
            t_buffer* b = recibir_buffer(sock_w);
            int qid = extraer_int_buffer(b);
            int pc = extraer_int_buffer(b);
            free(b);
        
            t_query* q = buscar_query_por_id(lista_querys, qid);
        
            if (q && w) {
        
                log_info(log_master,
                    "## Se desaloja la Query %d (%d) del Worker %d - Motivo: PRIORIDAD",
                    q->id, q->prioridad, w->id);

                // Sacar de EXEC
                sacar_de_cola_exec(q);

                // Actualizar contexto y volver a READY
                q->pc     = pc;
                q->estado = READY;
                push_ready(q);

                // Liberar worker
                w->id_query = -1;
                w->libre    = true;
        
                pthread_mutex_lock(&mutex_workers_libres);
                queue_push(cola_workers_libres, w);
                pthread_mutex_unlock(&mutex_workers_libres);
        
                sem_post(&sem_worker_disponible);
        
                log_info(log_master,
                    "## Query %d volvió a READY (PC=%d, prioridad %d). "
                    "Worker %d liberado.",
                    q->id, q->pc, q->prioridad, w->id);
            }
        } break;
        
        default:
            log_warning(log_master, "Operacion desconocida de Worker: %d", cod_op_w);
            key = false;
        }
    }
}

/*──────────────────────────────────────────
 *          MANEJO DE CONEXIONES
 *──────────────────────────────────────────*/

 static void manejar_conexiones_entrantes(void){
    while(!finalizar_master){
        int* sock_cliente = malloc(sizeof(int));
        *sock_cliente = accept(fd_master,NULL,NULL);
        if(*sock_cliente<0){ free(sock_cliente); continue; }
        op_code cod = recibir_operacion(*sock_cliente);
        switch(cod){
            case HANDSHAKE_MASTER_QUERY_CONTROL:{
                t_buffer* b = recibir_buffer(*sock_cliente);
                char* mensaje = extraer_string_buffer(b);  // Extraer mensaje de handshake
                char* path = extraer_string_buffer(b);
                int prio = extraer_int_buffer(b);
                // log_info(log_master,"Handshake QC recibido: %s", mensaje);
                free(mensaje);
                free(b);
                t_query* q = crear_estructura_query(path,prio,*sock_cliente);
                pthread_mutex_lock(&mutex_lista_workers);
                int cant = list_size(lista_workers);
                pthread_mutex_unlock(&mutex_lista_workers);
                log_info(log_master,"## Se conecta un Query Control para ejecutar la Query %s con prioridad %d - Id asignado: %d. Nivel multiprocesamiento %d",
                         path,prio,q->id,cant);
                push_ready(q);

                t_buffer* br = crear_buffer();
                cargar_string_al_buff(br,"QUERY_RECIBIDA");
                t_paquete* p = crear_super_paquete(HANDSHAKE_MASTER_QUERY_CONTROL,br);
                enviar_paquete(p,*sock_cliente);
                eliminar_paquete(p);
                pthread_t th;
                pthread_create(&th,NULL,(void*)comunicacion_con_query_control,sock_cliente);
                pthread_detach(th);
            } break;
            case HANDSHAKE_MASTER_WORKER:{
                t_buffer* b = recibir_buffer(*sock_cliente);
                int id = extraer_int_buffer(b);
                free(b);
                t_worker* w = crear_estructura_worker(id,*sock_cliente);
                (void)w;
                pthread_mutex_lock(&mutex_lista_workers);
                int cant = list_size(lista_workers);
                pthread_mutex_unlock(&mutex_lista_workers);
                log_info(log_master,"## Se conecta el Worker %d - Cantidad total de Workers: %d",id,cant);
                t_buffer* br = crear_buffer();
                cargar_string_al_buff(br,"WORKER_OK");
                t_paquete* p = crear_super_paquete(HANDSHAKE_MASTER_WORKER,br);
                enviar_paquete(p,*sock_cliente);
                eliminar_paquete(p);
                pthread_t th;
                pthread_create(&th,NULL,(void*)comunicacion_con_worker,sock_cliente);
                pthread_detach(th);
            } break;
            default:
                log_warning(log_master,"Handshake desconocido.");
                close(*sock_cliente);
                free(sock_cliente);
                break;
        }
    }
}

/* ────────────────────────────────
   INICIALIZACIÓN: colas, listas, sync, config
   ──────────────────────────────── */
   void iniciar_colas(void){
    cola_ready = queue_create();
    cola_exec = queue_create();
    cola_exit = queue_create();
    cola_workers_libres = queue_create();
}

void iniciar_listas(void){
    lista_querys = list_create();
    lista_workers = list_create();
}

void iniciar_mutex(void){
    pthread_mutex_init(&mutex_pid_query,NULL);
    pthread_mutex_init(&mutex_cola_ready,NULL);
    pthread_mutex_init(&mutex_cola_exec,NULL);
    pthread_mutex_init(&mutex_cola_exit,NULL);
    pthread_mutex_init(&mutex_lista_querys,NULL);
    pthread_mutex_init(&mutex_lista_workers,NULL);
    pthread_mutex_init(&mutex_workers_libres,NULL);
    pthread_mutex_init(&mutex_aging,NULL);
    pthread_cond_init(&cond_aging,NULL);
}

void iniciar_sem(void){
    sem_init(&sem_query_ready,0,0);
    sem_init(&sem_worker_disponible,0,0);
}

void iniciar_config_master(char* path){
    config_master = config_create(path);
    if(!config_master){printf("No se pudo abrir config\n"); exit(EXIT_FAILURE);}
    PUERTO_ESCUCHA = config_get_string_value(config_master,"PUERTO_ESCUCHA");
    ALGORITMO_PLANIFICACION = config_get_string_value(config_master,"ALGORITMO_PLANIFICACION");
    TIEMPO_AGING = config_get_int_value(config_master,"TIEMPO_AGING");
}

void inicializar_master(char* path){
    log_master = iniciar_logger("master.log","MASTER",LOG_LEVEL_INFO);
    iniciar_config_master(path);
    iniciar_mutex();
    iniciar_colas();
    iniciar_listas();
    iniciar_sem();
}

/*──────────────────────────────────────────
 *          MAIN
 *──────────────────────────────────────────*/
int main(int argc,char* argv[]){
    inicializar_master(argv[1]);
    log_info(log_master,"Iniciando Master");
    fd_master = iniciar_servidor(PUERTO_ESCUCHA,log_master,"SERVIDOR MASTER");
    pthread_t th_conex, th_plan;
    pthread_create(&th_conex,NULL,(void*)manejar_conexiones_entrantes,NULL);
    pthread_detach(th_conex);
    pthread_create(&th_plan,NULL,(void*)planificador_corto_plazo,NULL);
    // if (strcmp(ALGORITMO_PLANIFICACION, "PRIORIDADES") == 0 && TIEMPO_AGING > 0) { // ⬅️ CAMBIO: Se elimina el hilo de aging global
    //     pthread_t hilo_aging;
    //     pthread_create(&hilo_aging, NULL, (void*)aging_ready, NULL);
    //     pthread_detach(hilo_aging);
    // }
    pthread_join(th_plan,NULL);
    return 0;
}