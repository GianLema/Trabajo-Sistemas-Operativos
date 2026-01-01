#include "./worker.h"
#include <signal.h>
#include <stdbool.h>

t_log* log_worker;
t_config* config_worker;

char* IP_MASTER;
char* PUERTO_MASTER;
char* IP_STORAGE;
char* PUERTO_STORAGE;
char* ALGORITMO_REEMPLAZO;

int TAM_MEMORIA;
int RETARDO_MEMORIA;
char* PATH_SCRIPTS;
int LOG_LEVEL;
int BLOCK_SIZE;

int fd_master;
int fd_storage;
int worker_id;

t_internal_memory* internal_memory;
int current_query_id;
int current_query_pc = 0; // PC actual de la query en ejecución

// Variables para desalojo
volatile bool desalojo_solicitado = false;
pthread_mutex_t mutex_desalojo = PTHREAD_MUTEX_INITIALIZER;

// Mutex para evitar ejecución concurrente de queries en el mismo Worker
pthread_mutex_t mutex_query_execution = PTHREAD_MUTEX_INITIALIZER;

// Prototipos para cleanup
void cleanup_worker();
void signal_handler(int signum);
bool verificar_conexion_storage();
void reconectar_storage();

void comenzar_archivo_configuracion(char* configuracion){
    log_worker = iniciar_logger("worker.log","WORKER", LOG_LEVEL_INFO);
	iniciar_config_worker(configuracion);    
}

void iniciar_config_worker(char* ruta_config){
    config_worker = config_create(ruta_config);
    if(config_worker  == NULL){
        printf("No se pudo encontrar la config\n");
        exit(EXIT_FAILURE);
    }
    IP_MASTER = config_get_string_value(config_worker,"IP_MASTER");
    PUERTO_MASTER = config_get_string_value(config_worker, "PUERTO_MASTER");
    IP_STORAGE = config_get_string_value(config_worker, "IP_STORAGE");
    PUERTO_STORAGE = config_get_string_value(config_worker, "PUERTO_STORAGE");
    TAM_MEMORIA = config_get_int_value(config_worker, "TAM_MEMORIA");
    RETARDO_MEMORIA = config_get_int_value(config_worker, "RETARDO_MEMORIA");
    ALGORITMO_REEMPLAZO = config_get_string_value(config_worker, "ALGORITMO_REEMPLAZO");
    PATH_SCRIPTS = config_get_string_value(config_worker, "PATH_SCRIPTS");
    LOG_LEVEL = config_get_int_value(config_worker, "LOG_LEVEL");
}

bool verificar_conexion_storage() {
    // Verificar si fd_storage es válido enviando un mensaje pequeño
    // Si falla, devolver false
    if (fd_storage <= 0) {
        return false;
    }
    
    // Intentar verificar la conexión con fcntl
    int error = 0;
    socklen_t len = sizeof(error);
    int retval = getsockopt(fd_storage, SOL_SOCKET, SO_ERROR, &error, &len);
    
    if (retval != 0 || error != 0) {
        log_warning(log_worker, "Conexión con Storage inválida (fd=%d, error=%d)", fd_storage, error);
        return false;
    }
    
    return true;
}

void reconectar_storage() {
    log_info(log_worker, "Reconectando a Storage...");
    
    // Cerrar conexión antigua si existe
    if (fd_storage > 0) {
        close(fd_storage);
        fd_storage = -1;
    }
    
    // Reconectar
    conectarse_a_storage(worker_id);
    log_info(log_worker, "Reconexión a Storage exitosa");
}

void conectarse_a_storage(int id_worker){
    log_info(log_worker,"Conectandose al Storage");
    fd_storage = crear_conexion(IP_STORAGE,PUERTO_STORAGE);
    t_buffer* buffer_conectar_a_storage = crear_buffer();
    cargar_int_al_buff(buffer_conectar_a_storage, id_worker);
    t_paquete* paquete_conectar_a_storage = crear_super_paquete(HANDSHAKE_STORAGE_WORKER, buffer_conectar_a_storage);
    enviar_paquete(paquete_conectar_a_storage, fd_storage);
    eliminar_paquete(paquete_conectar_a_storage);
    
    // Recibir handshake del storage (sin esperar BLOCK_SIZE, ya que storage no lo envia)
    bool key = true;
    while (key){
        op_code cod_op = recibir_operacion(fd_storage);
        log_info(log_worker, "Codigo de operacion recibido de storage: %d",cod_op);
        switch (cod_op) {
        case HANDSHAKE_STORAGE_WORKER:
            t_buffer* buffer_hand_storage_worker = recibir_buffer(fd_storage);
            // El storage envía el worker_id de vuelta pero como string (bug del storage)
            // Lo ignoramos y simplemente confirmamos la conexión
            BLOCK_SIZE = extraer_int_buffer(buffer_hand_storage_worker);
            free(buffer_hand_storage_worker);
            // BLOCK_SIZE se obtendrá del superblock.config o se asume un valor por defecto
             // Valor por defecto típico (debe coincidir con el del storage)
            log_info(log_worker,"Handshake con STORAGE completado, BLOCK_SIZE: %d", BLOCK_SIZE);
            key=false;
            break;
        case -1:
            log_error(log_worker, "Cerrando conexion con el storage %d",fd_storage);
            exit(EXIT_FAILURE);
            break;
        default:
            log_warning(log_worker,"Respuesta desconocida del storage");
            continue;
            break;
        }
    }
}

void conectarse_a_master(int id_worker){
    log_info(log_worker,"Conectandose al Master");
    fd_master = crear_conexion(IP_MASTER,PUERTO_MASTER);
    worker_id = id_worker;
    
    t_buffer* buffer_conectar_a_master = crear_buffer();
    cargar_int_al_buff(buffer_conectar_a_master,id_worker);
    t_paquete* paquete_conectar_a_master = crear_super_paquete(HANDSHAKE_MASTER_WORKER, buffer_conectar_a_master);
    enviar_paquete(paquete_conectar_a_master, fd_master);
    eliminar_paquete(paquete_conectar_a_master);
    
    // Recibir confirmacion del handshake
    op_code cod_op = recibir_operacion(fd_master);
    if(cod_op == HANDSHAKE_MASTER_WORKER) {
        t_buffer* buffer_hand_master = recibir_buffer(fd_master);
        char* mensaje_recibido = extraer_string_buffer(buffer_hand_master);
        log_info(log_worker,"Recibi mensaje del Master: %s", mensaje_recibido);
        free(buffer_hand_master);
        free(mensaje_recibido);
    }
    
    // Iniciar hilo para manejar comunicacion continua con master
    pthread_t hilo_master;
    pthread_create(&hilo_master, NULL, (void*)manejar_comunicacion_master, NULL);
    pthread_detach(hilo_master);
}

void inicializar_memoria_interna(){
    internal_memory = malloc(sizeof(t_internal_memory));
    internal_memory->memory_space = malloc(TAM_MEMORIA);
    internal_memory->total_pages = TAM_MEMORIA / BLOCK_SIZE;
    internal_memory->used_pages = 0;
    internal_memory->page_tables = dictionary_create();
    internal_memory->physical_pages = list_create();
    internal_memory->clock_pointer = 0;
    internal_memory->frame_table = calloc(internal_memory->total_pages, sizeof(bool)); // false = libre
    pthread_mutex_init(&internal_memory->memory_mutex, NULL);
    
    log_info(log_worker, "Memoria interna inicializada. Tamaño: %d bytes, Páginas disponibles: %d", 
             TAM_MEMORIA, internal_memory->total_pages);
}

void manejar_comunicacion_master(){
    bool key = true;
    while (key){
        op_code cod_op = recibir_operacion(fd_master);
        log_info(log_worker, "Codigo de operacion recibido de master: %d",cod_op);
        switch (cod_op) {
        case HANDSHAKE_MASTER_WORKER:
            // manejado en conectarse_a_master
            break;
        case WORKER_DESALOJAR_QUERY: {
            // El Master pide desalojar una query en ejecución
            t_buffer* buffer_desalojo = recibir_buffer(fd_master);
            int query_id_desalojo = extraer_int_buffer(buffer_desalojo);
            free(buffer_desalojo);
            
            log_info(log_worker, "## Query %d: Recibida solicitud de desalojo desde Master", query_id_desalojo);
            
            // Marcar que se solicitó desalojo
            pthread_mutex_lock(&mutex_desalojo);
            desalojo_solicitado = true;
            pthread_mutex_unlock(&mutex_desalojo);
            
            // El Worker enviará la respuesta con el PC cuando termine de desalojar
            // (desde procesar_query cuando detecte desalojo_solicitado)
            
            break;
        }
        case -1:
            log_error(log_worker, "Cerrando conexion con el master %d",fd_master);
            key=false;
            break;
        default:
            // Asumo que es una solicitud de query
            // TODO: ver
            t_buffer* buffer_query = recibir_buffer(fd_master);
            int query_id = extraer_int_buffer(buffer_query);
            char* query_path = extraer_string_buffer(buffer_query);
            log_info(log_worker, "## Query %d: Se recibe la Query. El path de operaciones es: %s", query_id, query_path);
            
            // Procesar query en un hilo separado
            pthread_t hilo_query;
            typedef struct {
                int id;
                char* path;
            } query_args;
            
            query_args* args = malloc(sizeof(query_args));
            args->id = query_id;
            args->path = strdup(query_path);
            
            pthread_create(&hilo_query, NULL, (void*)procesar_query_hilo, args);
            pthread_detach(hilo_query);
            
            free(buffer_query);
            free(query_path);
            break;
        }
    }
}

void procesar_query(int query_id, char* query_path) {
    // BLOQUEAR: Solo una query a la vez en este Worker
    pthread_mutex_lock(&mutex_query_execution);
    //log_info(log_worker, "## Query %d: Adquirió el lock de ejecución", query_id);
    
    current_query_id = query_id; // Establecer ID actual para logs
    
    // Verificar y reconectar a Storage si es necesario
    if (!verificar_conexion_storage()) {
        log_warning(log_worker, "Conexión con Storage perdida, reconectando...");
        reconectar_storage();
    }
    
    // Construir ruta completa del archivo
    char* ruta_completa = malloc(strlen(PATH_SCRIPTS) + strlen(query_path) + 2);
    sprintf(ruta_completa, "%s/%s", PATH_SCRIPTS, query_path);
    
    FILE* archivo_query = fopen(ruta_completa, "r");
    if(!archivo_query) {
        log_error(log_worker, "No se pudo abrir el archivo de query: %s", ruta_completa);
        free(ruta_completa);
        pthread_mutex_unlock(&mutex_query_execution);
        return;
    }
    
    char linea[256];
    int pc = 0;
    bool query_error = false;
    bool query_desalojada = false;
    
    while(fgets(linea, sizeof(linea), archivo_query)) {
        
        // Actualizar PC global para desalojo
        current_query_pc = pc;
        
        // Verificar si se solicitó desalojo
        pthread_mutex_lock(&mutex_desalojo);
        if(desalojo_solicitado) {
            log_info(log_worker, "## Query %d: Desalojada por pedido del Master", query_id);
            query_desalojada = true;
            desalojo_solicitado = false; // Resetear flag
            pthread_mutex_unlock(&mutex_desalojo);
            break;
        }
        pthread_mutex_unlock(&mutex_desalojo);
        
        linea[strcspn(linea, "\n")] = 0;
        
        if(strlen(linea) == 0) continue; // Saltar lineas vacias
        
        // Log obligatorio: FETCH
        log_info(log_worker, "## Query %d: FETCH - Program Counter: %d - %s", query_id, pc, linea);
        
        t_instruction* instruccion = parsear_instruccion(linea);
        if(instruccion) {
            if(!ejecutar_instruccion(instruccion)) {
                log_error(log_worker, "Query %d terminada por error en Storage al ejecutar: %s", query_id, linea);
                query_error = true;
                liberar_instruccion(instruccion);
                break;
            }
            
            // Log obligatorio: Instruccion realizada
            log_info(log_worker, "## Query %d: - Instrucción realizada: %s", query_id, linea);
            
            if(instruccion->type == INSTR_END) {
                liberar_instruccion(instruccion);
                break;
            }
            
            liberar_instruccion(instruccion);
        }
        pc++;
    }
    
    fclose(archivo_query);
    free(ruta_completa);
    
    if(query_desalojada) {
        // Hacer FLUSH de todos los archivos modificados
        log_info(log_worker, "Query %d: Realizando FLUSH antes de desalojo", query_id);
        flush_todas_las_paginas_modificadas();
        
        // Responder con el PC actual al Master
        t_buffer* b_respuesta = crear_buffer();
        cargar_int_al_buff(b_respuesta, query_id);
        cargar_int_al_buff(b_respuesta, current_query_pc);
        
        t_paquete* paquete_resp = crear_super_paquete(WORKER_DESALOJAR_QUERY, b_respuesta);
        enviar_paquete(paquete_resp, fd_master);
        eliminar_paquete(paquete_resp);
        
        log_info(log_worker, "Query %d: Desalojo completado. PC enviado: %d", query_id, current_query_pc);
    } else if(query_error) {
        log_info(log_worker, "Query %d finalizada con error", query_id);
        t_buffer* buff_err = crear_buffer();
        cargar_int_al_buff(buff_err, query_id);
        cargar_int_al_buff(buff_err, 0); // 0 = error
        t_paquete* paquete_err = crear_super_paquete(WORKER_FIN_QUERY, buff_err);
        enviar_paquete(paquete_err, fd_master);
        eliminar_paquete(paquete_err);
    } else {
        log_info(log_worker, "Query %d finalizada exitosamente", query_id);
        
        // Notificar al Master que la query terminó normalmente
        t_buffer* buffer_fin = crear_buffer();
        cargar_int_al_buff(buffer_fin, query_id);
        cargar_int_al_buff(buffer_fin, 1); // 1 = éxito
        t_paquete* paquete_fin = crear_super_paquete(WORKER_FIN_QUERY, buffer_fin);
        enviar_paquete(paquete_fin, fd_master);
        eliminar_paquete(paquete_fin);
        
        log_info(log_worker, "## Query %d: Notificación de finalización enviada al Master", query_id);
    }
    
    // DESBLOQUEAR: Permitir que otra query se ejecute en este Worker
    pthread_mutex_unlock(&mutex_query_execution);
    //log_info(log_worker, "## Query %d: Liberó el lock de ejecución", query_id);
}

t_instruction* parsear_instruccion(char* linea) {
    t_instruction* instruccion = malloc(sizeof(t_instruction));
    memset(instruccion, 0, sizeof(t_instruction));
    
    char* token = strtok(linea, " ");
    if(!token) {
        free(instruccion);
        return NULL;
    }
    
    if(strcmp(token, "CREATE") == 0) {
        instruccion->type = INSTR_CREATE;
        char* file_tag = strtok(NULL, " ");
        char* colon = strchr(file_tag, ':');
        *colon = '\0';
        instruccion->file_name = strdup(file_tag);
        instruccion->tag = strdup(colon + 1);
        
    } else if(strcmp(token, "TRUNCATE") == 0) {
        instruccion->type = INSTR_TRUNCATE;
        char* file_tag = strtok(NULL, " ");
        char* size_str = strtok(NULL, " ");
        char* colon = strchr(file_tag, ':');
        *colon = '\0';
        instruccion->file_name = strdup(file_tag);
        instruccion->tag = strdup(colon + 1);
        instruccion->size = atoi(size_str);
        
    } else if(strcmp(token, "WRITE") == 0) {
        instruccion->type = INSTR_WRITE;
        char* file_tag = strtok(NULL, " ");
        char* address_str = strtok(NULL, " ");
        char* content = strtok(NULL, " ");
        char* colon = strchr(file_tag, ':');
        *colon = '\0';
        instruccion->file_name = strdup(file_tag);
        instruccion->tag = strdup(colon + 1);
        instruccion->address = atoi(address_str);
        instruccion->content = strdup(content);
        
    } else if(strcmp(token, "READ") == 0) {
        instruccion->type = INSTR_READ;
        char* file_tag = strtok(NULL, " ");
        char* address_str = strtok(NULL, " ");
        char* size_str = strtok(NULL, " ");
        char* colon = strchr(file_tag, ':');
        *colon = '\0';
        instruccion->file_name = strdup(file_tag);
        instruccion->tag = strdup(colon + 1);
        instruccion->address = atoi(address_str);
        instruccion->size = atoi(size_str);
        
    } else if(strcmp(token, "TAG") == 0) {
        instruccion->type = INSTR_TAG;
        char* src_file_tag = strtok(NULL, " ");
        char* dest_file_tag = strtok(NULL, " ");
        char* src_colon = strchr(src_file_tag, ':');
        char* dest_colon = strchr(dest_file_tag, ':');
        *src_colon = '\0';
        *dest_colon = '\0';
        instruccion->file_name = strdup(src_file_tag);
        instruccion->tag = strdup(src_colon + 1);
        instruccion->dest_file_name = strdup(dest_file_tag);
        instruccion->dest_tag = strdup(dest_colon + 1);
        
    } else if(strcmp(token, "COMMIT") == 0) {
        instruccion->type = INSTR_COMMIT;
        char* file_tag = strtok(NULL, " ");
        char* colon = strchr(file_tag, ':');
        *colon = '\0';
        instruccion->file_name = strdup(file_tag);
        instruccion->tag = strdup(colon + 1);
        
    } else if(strcmp(token, "FLUSH") == 0) {
        instruccion->type = INSTR_FLUSH;
        char* file_tag = strtok(NULL, " ");
        char* colon = strchr(file_tag, ':');
        *colon = '\0';
        instruccion->file_name = strdup(file_tag);
        instruccion->tag = strdup(colon + 1);
        
    } else if(strcmp(token, "DELETE") == 0) {
        instruccion->type = INSTR_DELETE;
        char* file_tag = strtok(NULL, " ");
        char* colon = strchr(file_tag, ':');
        *colon = '\0';
        instruccion->file_name = strdup(file_tag);
        instruccion->tag = strdup(colon + 1);
        
    } else if(strcmp(token, "END") == 0) {
        instruccion->type = INSTR_END;
        
    } else {
        log_warning(log_worker, "Instrucción desconocida: %s", token);
        free(instruccion);
        return NULL;
    }
    
    return instruccion;
}

bool ejecutar_instruccion(t_instruction* instruccion) {

    // Manejo de respuesta desde storage para terminar queries en caso de error
    switch(instruccion->type) {
        case INSTR_CREATE:
            return ejecutar_create(instruccion->file_name, instruccion->tag);
        case INSTR_TRUNCATE:
            return ejecutar_truncate(instruccion->file_name, instruccion->tag, instruccion->size);
        case INSTR_WRITE:
            return ejecutar_write(instruccion->file_name, instruccion->tag, instruccion->address, instruccion->content);
        case INSTR_READ:
            return ejecutar_read(instruccion->file_name, instruccion->tag, instruccion->address, instruccion->size);
        case INSTR_TAG:
            return ejecutar_tag(instruccion->file_name, instruccion->tag, instruccion->dest_file_name, instruccion->dest_tag);
        case INSTR_COMMIT:
            return ejecutar_commit(instruccion->file_name, instruccion->tag);
        case INSTR_FLUSH:
            return ejecutar_flush(instruccion->file_name, instruccion->tag);
        case INSTR_DELETE:
            return ejecutar_delete(instruccion->file_name, instruccion->tag);
        case INSTR_END:
            log_info(log_worker, "Finalizando query");
            return true;
    }
    return false;
}

bool ejecutar_create(char* file_name, char* tag) {
    log_info(log_worker, "CREATE %s:%s", file_name, tag);
    return enviar_create_storage(file_name, tag);
}

bool ejecutar_truncate(char* file_name, char* tag, int size) {
    log_info(log_worker, "TRUNCATE %s:%s %d", file_name, tag, size);
    return enviar_truncate_storage(file_name, tag, size);
}

bool ejecutar_write(char* file_name, char* tag, int address, char* content) {
    log_info(log_worker, "WRITE %s:%s %d %s", file_name, tag, address, content);
    
    int content_len = strlen(content);
    int bytes_written = 0;
    
    // Procesar la escritura, que puede abarcar multiples paginas
    while(bytes_written < content_len) {
        int current_address = address + bytes_written;
        int page_number = calcular_numero_pagina(current_address);
        int offset = calcular_offset_pagina(current_address);
        
        // Calcular cuantos bytes caben en esta pagina
        int bytes_left_in_page = BLOCK_SIZE - offset;
        int bytes_to_write = (content_len - bytes_written < bytes_left_in_page) 
                             ? (content_len - bytes_written) 
                             : bytes_left_in_page;
        
        // Obtener pagina (cargarla si es necesario)
        t_page* page = obtener_pagina(file_name, tag, page_number);
        if(!page) {
            log_error(log_worker, "Query %d: No se pudo obtener la página %d para WRITE - Abortando query", 
                     current_query_id, page_number);
            return false;
        }
        
        simular_retardo_memoria();
        
        // Calcular direccion fisica
        int direccion_fisica = calcular_direccion_fisica(page->frame_number, offset);
        
        // Escribir en la pagina
        memcpy((char*)page->content + offset, content + bytes_written, bytes_to_write);
        page->modified = true;
        algoritmo_lru_actualizar(page);
        
        log_info(log_worker, "Query %d: Acción: ESCRIBIR - Dirección Física: %d - Valor: %.*s", 
                 current_query_id, direccion_fisica, bytes_to_write, content + bytes_written);
        
        bytes_written += bytes_to_write;
    }
    
    log_info(log_worker, "WRITE completado: %d bytes escritos desde dirección %d", content_len, address);
    return true;
}

bool ejecutar_read(char* file_name, char* tag, int address, int size) {
    log_info(log_worker, "READ %s:%s %d %d", file_name, tag, address, size);
    
    // Reservar buffer para todos los datos a leer
    void* data = malloc(size);
    if(!data) {
        log_error(log_worker, "Query %d: No se pudo asignar memoria para READ de %d bytes", 
                 current_query_id, size);
        return false;
    }
    
    int bytes_read = 0;
    
    // Procesar la lectura, que puede abarcar multiples paginas
    while(bytes_read < size) {
        int current_address = address + bytes_read;
        int page_number = calcular_numero_pagina(current_address);
        int offset = calcular_offset_pagina(current_address);
        
        // Calcular cuántos bytes quedan por leer en esta pagina
        int bytes_left_in_page = BLOCK_SIZE - offset;
        int bytes_to_read = (size - bytes_read < bytes_left_in_page) 
                            ? (size - bytes_read) 
                            : bytes_left_in_page;
        
        // Obtener pagina (cargarla si es necesario)
        t_page* page = obtener_pagina(file_name, tag, page_number);
        if(!page) {
            log_error(log_worker, "Query %d: No se pudo obtener la página %d para READ - Abortando query", 
                     current_query_id, page_number);
            free(data);
            return false;
        }
        
        simular_retardo_memoria();
        
        // Calcular direccion fisica
        int direccion_fisica = calcular_direccion_fisica(page->frame_number, offset);
        
        // Leer contenido de la pagina
        memcpy((char*)data + bytes_read, (char*)page->content + offset, bytes_to_read);
        
        log_info(log_worker, "Query %d: Acción: LEER - Dirección Física: %d - Valor: %.*s", 
                 current_query_id, direccion_fisica, bytes_to_read, (char*)data + bytes_read);
        
        algoritmo_lru_actualizar(page);
        
        bytes_read += bytes_to_read;
    }
    
    // Enviar resultado al master
    enviar_resultado_read_master(file_name, tag, data, size);
    
    free(data);
    log_info(log_worker, "READ completado: %d bytes leídos desde dirección %d", size, address);
    return true;
}

bool ejecutar_tag(char* src_file, char* src_tag, char* dest_file, char* dest_tag) {
    log_info(log_worker, "TAG %s:%s %s:%s", src_file, src_tag, dest_file, dest_tag);
    return enviar_tag_storage(src_file, src_tag, dest_file, dest_tag);
}

bool ejecutar_commit(char* file_name, char* tag) {
    log_info(log_worker, "COMMIT %s:%s", file_name, tag);
    
    // Hacer FLUSH antes del COMMIT
    if(!ejecutar_flush(file_name, tag)) {
        return false;
    }
    
    // Enviar COMMIT al storage
    return enviar_commit_storage(file_name, tag);
}

bool ejecutar_flush(char* file_name, char* tag) {
    log_info(log_worker, "FLUSH %s:%s", file_name, tag);
    
    pthread_mutex_lock(&internal_memory->memory_mutex);
    
    // Buscar todas las paginas modificadas del file:tag y escribirlas al storage
    char* key = crear_file_tag_key(file_name, tag);
    t_page_table* page_table = dictionary_get(internal_memory->page_tables, key);
    
    bool exito = true;
    if(page_table) {
        for(int i = 0; i < list_size(page_table->pages); i++) {
            t_page* page = list_get(page_table->pages, i);
            if(page->modified && page->present) {
                if(!escribir_pagina_en_storage(page)) {
                    exito = false;
                    break;  // Si falla una escritura, detener el flush
                }
                page->modified = false;
            }
        }
    }
    
    free(key);
    pthread_mutex_unlock(&internal_memory->memory_mutex);
    return exito;
}

bool ejecutar_delete(char* file_name, char* tag) {
    log_info(log_worker, "DELETE %s:%s", file_name, tag);
    
    // Limpiar paginas de memoria interna del file:tag
    pthread_mutex_lock(&internal_memory->memory_mutex);
    char* key = crear_file_tag_key(file_name, tag);
    t_page_table* page_table = dictionary_get(internal_memory->page_tables, key);
    
    if(page_table) {
        // Liberar marcos y paginas del file:tag
        for(int i = 0; i < list_size(page_table->pages); i++) {
            t_page* page = list_get(page_table->pages, i);
            if(page->present) {
                log_info(log_worker, "Query %d: Se libera el Marco: %d perteneciente al - File: %s - Tag: %s", 
                         current_query_id, page->frame_number, file_name, tag);
                
                liberar_marco(page->frame_number);
                internal_memory->used_pages--;
                
                // Remover de lista fisica
                for(int j = 0; j < list_size(internal_memory->physical_pages); j++) {
                    t_page* p = list_get(internal_memory->physical_pages, j);
                    if(p == page) {
                        list_remove(internal_memory->physical_pages, j);
                        
                        // Ajustar clock_pointer si se removio un elemento antes o en la posicion actual
                        if(strcmp(ALGORITMO_REEMPLAZO, "CLOCK-M") == 0) {
                            if(j < internal_memory->clock_pointer) {
                                internal_memory->clock_pointer--;
                            } else if(j == internal_memory->clock_pointer) {
                                // El puntero ya apunta al siguiente elemento
                                if(internal_memory->clock_pointer >= list_size(internal_memory->physical_pages)) {
                                    internal_memory->clock_pointer = 0;
                                }
                            }
                        }
                        
                        break;
                    }
                }
            }
            free(page->file_tag.file_name);
            free(page->file_tag.tag);
            free(page->content);
        }
        
        list_destroy_and_destroy_elements(page_table->pages, free);
        pthread_mutex_destroy(&page_table->mutex);
        free(page_table);
        dictionary_remove(internal_memory->page_tables, key);
    }
    
    free(key);
    pthread_mutex_unlock(&internal_memory->memory_mutex);
    
    // Enviar DELETE al storage
    return enviar_delete_storage(file_name, tag);
}
    
int main(int argc, char* argv[]) {
    if(argc < 3) {
        printf("Uso: %s <archivo_config> <ID_worker>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // Registrar manejador de señales para cleanup
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    comenzar_archivo_configuracion(argv[1]);
    int id_worker = atoi(argv[2]);
    conectarse_a_storage(id_worker);
    inicializar_memoria_interna();
    conectarse_a_master(id_worker);
    
    // Mantener el programa activo
    log_info(log_worker, "Worker %d iniciado correctamente", id_worker);
    while(1) {
        sleep(1);
    }
    
    cleanup_worker();
    return 0;
}

void cleanup_worker() {
    log_info(log_worker, "Liberando memoria del worker...");
    
    if(internal_memory) {
        pthread_mutex_lock(&internal_memory->memory_mutex);
        
        // Liberar todas las páginas en memoria física
        if(internal_memory->physical_pages) {
            for(int i = 0; i < list_size(internal_memory->physical_pages); i++) {
                t_page* page = list_get(internal_memory->physical_pages, i);
                if(page) {
                    free(page->file_tag.file_name);
                    free(page->file_tag.tag);
                    free(page->content);
                    free(page);
                }
            }
            list_destroy(internal_memory->physical_pages);
        }
        
        // Liberar page_tables (solo las estructuras, las páginas ya se liberaron)
        if(internal_memory->page_tables) {
            void _cleanup_page_table(void* element) {
                t_page_table* pt = (t_page_table*)element;
                list_destroy(pt->pages); // Solo destruir lista, no elementos
                pthread_mutex_destroy(&pt->mutex);
                free(pt);
            }
            dictionary_destroy_and_destroy_elements(internal_memory->page_tables, _cleanup_page_table);
        }
        
        // Liberar frame_table y memory_space
        if(internal_memory->frame_table) {
            free(internal_memory->frame_table);
        }
        if(internal_memory->memory_space) {
            free(internal_memory->memory_space);
        }
        
        pthread_mutex_unlock(&internal_memory->memory_mutex);
        pthread_mutex_destroy(&internal_memory->memory_mutex);
        free(internal_memory);
    }
    
    log_info(log_worker, "Memoria liberada correctamente");
    log_destroy(log_worker);
    config_destroy(config_worker);
}

void signal_handler(int signum) {
    log_info(log_worker, "Señal %d recibida, cerrando worker...", signum);
    cleanup_worker();
    exit(0);
}
