#include "./storage.h"
#include <fcntl.h>
#include <sys/mman.h>

#define SUPERBLOCK_FILE "superblock.config"
#define BITMAP_FILE "bitmap.bin"
#define HASH_INDEX_FILE "blocks_hash_index.config"
#define DIR_BLOCKS "physical_blocks"
#define DIR_FILES "files"

void iniciar_config_storage(char* ruta_config){
    config_storage = config_create(ruta_config);
    if(config_storage  == NULL){
        printf("No se pudo encontrar la config\n");
        exit(EXIT_FAILURE);
    }
    PUERTO_ESCUCHA = config_get_string_value(config_storage,"PUERTO_ESCUCHA");
    FRESH_START = config_get_string_value(config_storage, "FRESH_START");//en caso de true borro todo menos superblock. en base a el superbloque, reconstruis
    PUNTO_MONTAJE = config_get_string_value(config_storage, "PUNTO_MONTAJE");
    RETARDO_OPERACION = config_get_int_value(config_storage, "RETARDO_OPERACION");
    RETARDO_ACCESO_BLOQUE = config_get_int_value(config_storage, "RETARDO_ACCESO_BLOQUE");
    LOG_LEVEL = config_get_int_value(config_storage, "LOG_LEVEL");
}
void comenzar_archivo_configuracion(char* configuracion){
    log_storage = iniciar_logger("storage.log","STORAGE", LOG_LEVEL_INFO);
	iniciar_config_storage(configuracion);    
}



void creacion_de_hilos_para_escuchar_workers(int fd_escucha){
    while(1) {
        pthread_t thread;
        int *fd_conexion_ptr = malloc(sizeof(int));
        *fd_conexion_ptr = accept(fd_escucha, NULL, NULL);
        
        pthread_create(&thread,
                       NULL,
                       (void*) se_conecta_un_worker_al_storage,
                       fd_conexion_ptr);
        pthread_detach(thread);
    }
}

void se_conecta_un_worker_al_storage(int* socket_del_worker){
    bool key = true;
    int worker_id;
    while (key) {
        op_code cod_operacion = recibir_operacion(*socket_del_worker);
        switch (cod_operacion) {
        case HANDSHAKE_STORAGE_WORKER:
            // RECIBIR RESPUESTA DEL WORKER
            t_buffer* buffer_ejemplo = recibir_buffer(*socket_del_worker);
            worker_id = extraer_int_buffer(buffer_ejemplo);
            cantidad_total_de_workers++; 
            log_info(log_storage,"##Se conecta el Worker <%d> - Cantidad de Workers: <%d>", worker_id,cantidad_total_de_workers);

            // ENVIAR RESPUESTA AL WORKER
            t_buffer* buffer_enviar_ejemplo_al_worker = crear_buffer();
            cargar_int_al_buff(buffer_enviar_ejemplo_al_worker, BLOCK_SIZE);
            t_paquete* paquete_para_enviar_al_worker = crear_super_paquete(HANDSHAKE_STORAGE_WORKER, buffer_enviar_ejemplo_al_worker);
            enviar_paquete(paquete_para_enviar_al_worker, *socket_del_worker);
            eliminar_paquete(paquete_para_enviar_al_worker);
            break;
        case CREACION_FILE: 
            t_buffer* buffer_creacion_file = recibir_buffer(*socket_del_worker);
            int query_Id = extraer_int_buffer(buffer_creacion_file);
            char* nombre_file = extraer_string_buffer(buffer_creacion_file);
            char* creacion_tag = extraer_string_buffer(buffer_creacion_file);
            usleep(RETARDO_OPERACION * 1000);
            if( funcion_para_create_file (nombre_file, creacion_tag)){
                log_info(log_storage,"##<%d> - File Creado <%s>:<%s>", query_Id,nombre_file,creacion_tag);
                enviar_respuesta_a_worker("OK", socket_del_worker, CREACION_FILE);
            }else{
                log_info(log_storage, "File:Tag preexistente");
                enviar_respuesta_a_worker("File:Tag preexistente",socket_del_worker,CREACION_FILE);
            }
            free(nombre_file);
            free(creacion_tag);         
            break;
        case TRUNCADO_ARCHIVO:
            t_buffer* buffer_truncado = recibir_buffer(*socket_del_worker);
            int query_identificador=extraer_int_buffer(buffer_truncado);
            char* file_name = extraer_string_buffer(buffer_truncado);
            char* tag = extraer_string_buffer(buffer_truncado);
            int size_new = extraer_int_buffer(buffer_truncado);
            usleep(RETARDO_OPERACION * 1000);
            if( truncate_file(file_name,tag,size_new,query_identificador)){
                enviar_respuesta_a_worker("OK", socket_del_worker, TRUNCADO_ARCHIVO);
            }else{
                enviar_respuesta_a_worker("File:Tag inexistente",socket_del_worker,TRUNCADO_ARCHIVO);
            }
            log_info(log_storage, "##<%d> - File Truncado <%s>:<%s> - Tamanio: <%d>", query_identificador,file_name,tag,size_new);
            free(file_name);
            free(tag);
            break;
        case TAG_FILE:
            t_buffer* buff_tag = recibir_buffer(*socket_del_worker);
            int query_tag_file_ID = extraer_int_buffer(buff_tag); 
            char* src_file = extraer_string_buffer(buff_tag); // origen
            char* src_tag = extraer_string_buffer(buff_tag); //origen
            char* dest_file = extraer_string_buffer(buff_tag);
            char* dest_tag = extraer_string_buffer(buff_tag);
            usleep(RETARDO_OPERACION * 1000);
            if(duplicar_tag(src_file,src_tag,dest_file,dest_tag,query_tag_file_ID)){
                log_info(log_storage, "##<%d> - Tag creado <%s>:<%s>", query_tag_file_ID,dest_file,dest_tag);
                enviar_respuesta_a_worker("OK",socket_del_worker,TAG_FILE);
            }else{
                log_info(log_storage, "File:Tag preexistente");
                enviar_respuesta_a_worker("File:Tag preexistente",socket_del_worker,TAG_FILE);
            }
            free(src_file);
            free(src_tag);
            free(dest_file);
            free(dest_tag);
            break;
        case COMMIT_TAG:
            t_buffer* buffer_commited_tag = recibir_buffer(*socket_del_worker);
            int QUERY_ID = extraer_int_buffer(buffer_commited_tag);
            char* src_file_commit = extraer_string_buffer(buffer_commited_tag);
            char* src_tag_commit = extraer_string_buffer(buffer_commited_tag);
            usleep(RETARDO_OPERACION * 1000);
            log_info(log_storage, "##<%d> - Commit de File:Tag <%s>:<%s>", QUERY_ID,src_file_commit,src_tag_commit);
            if(commitear_tag(src_file_commit,src_tag_commit,QUERY_ID)){
                enviar_respuesta_a_worker("OK",socket_del_worker,COMMIT_TAG);
            }else{
                 enviar_respuesta_a_worker("NO",socket_del_worker,COMMIT_TAG);
            }
            free(src_file_commit);
            free(src_tag_commit);
            break;
        case ESCRITURA_BLOQUE:
            t_buffer* buffer_escritura = recibir_buffer(*socket_del_worker);
            int query_id_escritura = extraer_int_buffer(buffer_escritura);
            char* nombre_file_escritura = extraer_string_buffer(buffer_escritura);
            char* tag_escritura = extraer_string_buffer(buffer_escritura);
            int numero_bloque_logico = extraer_int_buffer(buffer_escritura);
            void* data_a_escribir = extraer_buffer(buffer_escritura);
            escribir_bloque(query_id_escritura, nombre_file_escritura, tag_escritura, numero_bloque_logico, data_a_escribir, socket_del_worker);
            usleep(RETARDO_OPERACION * 1000);
            usleep(RETARDO_ACCESO_BLOQUE * 1000);
            free(nombre_file_escritura);
            free(tag_escritura);
            free(data_a_escribir);
            break;
        case LECTURA_BLOQUE:
            t_buffer* buffer_lectura = recibir_buffer(*socket_del_worker);
            int query_id_lectura = extraer_int_buffer(buffer_lectura);
            char* nombre_file_lectura = extraer_string_buffer(buffer_lectura);
            char* tag_lectura = extraer_string_buffer(buffer_lectura);
            int numero_bloque_logico_lectura = extraer_int_buffer(buffer_lectura);
            usleep(RETARDO_OPERACION * 1000);
            usleep(RETARDO_ACCESO_BLOQUE * 1000);
            lectura(query_id_lectura, nombre_file_lectura, tag_lectura, numero_bloque_logico_lectura, socket_del_worker);
            free(nombre_file_lectura);
            free(tag_lectura);
            free(buffer_lectura);
            break;
        case ELIMINAR_TAG:

            t_buffer* buffer_eliminar_tag = recibir_buffer(*socket_del_worker);
            int query_id_eliminar_tag = extraer_int_buffer(buffer_eliminar_tag);
            char* nombre_file_eliminar_tag = extraer_string_buffer(buffer_eliminar_tag);
            char* tag_eliminar_tag = extraer_string_buffer(buffer_eliminar_tag);
            usleep(RETARDO_OPERACION * 1000);
            char * ruta_file_eliminar = construir_ruta(path_files_dir, nombre_file_eliminar_tag);

            construir_ruta(ruta_file_eliminar, "metadata.config");
            eliminar_tag(ruta_file_eliminar, tag_eliminar_tag, socket_del_worker,query_id_eliminar_tag,nombre_file_eliminar_tag);
            log_info(log_storage, "##<%d> - Tag Eliminado <%s>:<%s>", query_id_eliminar_tag, nombre_file_eliminar_tag,tag_eliminar_tag);
            free(ruta_file_eliminar);
            free(buffer_eliminar_tag);
            free(nombre_file_eliminar_tag);
            free(tag_eliminar_tag);
            break;
        default:
            if (cod_operacion == -1) {
                pthread_mutex_lock(&mutex_cantidad_de_workers);
                cantidad_total_de_workers--;
                pthread_mutex_unlock(&mutex_cantidad_de_workers);
                log_info(log_storage, "##Se desconecta el Worker <%d> - Cantidad de Workers: <%d>", worker_id, cantidad_total_de_workers);
                key = false;
            } else {
                log_warning(log_storage, "Operacion desconocida de storage");
            }
            break;
        }
    }
    close(*socket_del_worker); // Cerrar el socket al finalizar
    free(socket_del_worker); // Liberar el socket
}

void lectura(int query_id_lectura, char* nombre_file_lectura, char* tag_lectura, int numero_bloque_logico_lectura, int* socket_del_worker) {
    struct stat st;
    char* ruta_lectura = construir_ruta(path_files_dir, nombre_file_lectura);
    if(stat(ruta_lectura, &st) == -1){
        enviar_respuesta_a_worker("File inexistente", socket_del_worker, LECTURA_BLOQUE);
        free(ruta_lectura);
        return;
    }
    ruta_lectura = construir_ruta(ruta_lectura, tag_lectura);
    if(stat(ruta_lectura, &st) == -1){
        enviar_respuesta_a_worker("Tag inexistente", socket_del_worker, LECTURA_BLOQUE);
        free(ruta_lectura);
        return;
    }
    char* ruta_meta_lectura = string_duplicate(ruta_lectura);
    ruta_meta_lectura = construir_ruta(ruta_meta_lectura, "metadata.config");
    t_config* config_meta_lectura = config_create(ruta_meta_lectura);
    char** bloques_asignados_lectura = config_get_array_value(config_meta_lectura, "BLOQUES");
    t_list* lista_bloques_lectura = list_create();
    for (int i = 0; bloques_asignados_lectura[i] != NULL; i++) {
        list_add(lista_bloques_lectura, strdup(bloques_asignados_lectura[i])); // strdup para copiar el string a la lista
    }
    log_info(log_storage, "nro de bloque logico a leer: %d, cant bloques logicos en file:tag: %d", numero_bloque_logico_lectura, list_size(lista_bloques_lectura));
    if(numero_bloque_logico_lectura >= list_size(lista_bloques_lectura)){
        log_info(log_storage, "lectura fuera de limite");
        enviar_respuesta_a_worker("Bloque logico no asignado / Lectura fuera de limite", socket_del_worker, LECTURA_BLOQUE);
        free(ruta_lectura);
        free(ruta_meta_lectura);
        free(bloques_asignados_lectura);
        list_destroy_and_destroy_elements(lista_bloques_lectura, free);
        return;
    }
    char* ruta_bloque_logico_especifico_lectura = string_duplicate(ruta_lectura);
    ruta_bloque_logico_especifico_lectura = construir_ruta(ruta_bloque_logico_especifico_lectura, "logical_blocks");
    if(strlen(string_itoa(numero_bloque_logico_lectura)) < 6){
        char* numero_bloque_formateado_lectura = string_repeat('0', 6 - strlen(string_itoa(numero_bloque_logico_lectura)));
        string_append(&numero_bloque_formateado_lectura, string_itoa(numero_bloque_logico_lectura));
        string_append(&numero_bloque_formateado_lectura, ".dat");
        ruta_bloque_logico_especifico_lectura = construir_ruta(ruta_bloque_logico_especifico_lectura, numero_bloque_formateado_lectura);
        free(numero_bloque_formateado_lectura);
    }
    else{
        ruta_bloque_logico_especifico_lectura = construir_ruta(ruta_bloque_logico_especifico_lectura, string_itoa(numero_bloque_logico_lectura));
        ruta_bloque_logico_especifico_lectura = construir_ruta(ruta_bloque_logico_especifico_lectura, ".dat");
    }
    FILE* archivo_bloque_logico_lectura = fopen(ruta_bloque_logico_especifico_lectura, "r");// esta bien r? o rb?
    char* data_leida = malloc(BLOCK_SIZE + 1);
    fread(data_leida, sizeof(char), BLOCK_SIZE, archivo_bloque_logico_lectura);
    data_leida[BLOCK_SIZE] = '\0';
    log_info(log_storage, "##<%d> - Bloque Logico Leido <%s>:<%s> - Numero de Bloque: <%d>", query_id_lectura, nombre_file_lectura, tag_lectura, numero_bloque_logico_lectura);
    fclose(archivo_bloque_logico_lectura);
    enviar_respuesta_a_worker(data_leida, socket_del_worker, LECTURA_BLOQUE);
    free(data_leida);
    free(ruta_lectura);
    free(ruta_meta_lectura);
    free(ruta_bloque_logico_especifico_lectura);
    string_array_destroy(bloques_asignados_lectura);
    list_destroy_and_destroy_elements(lista_bloques_lectura, free);
    config_destroy(config_meta_lectura);
}

// Función para eliminar un Tag específico dentro de un archivo
void eliminar_tag(char* path_file_name, char* nombre_tag,int* socket_del_worker,int query_id,char* file_name_eliminar) {

    struct stat st;
    if (stat(path_file_name, &st) != 0) {
        enviar_respuesta_a_worker("Tag inexistente", socket_del_worker, ELIMINAR_TAG);
        return ;
    }

    char* path_tag = construir_ruta(path_file_name, nombre_tag);

    if (access(path_tag, F_OK) != 0) {
        enviar_respuesta_a_worker("Tag inexistente", socket_del_worker, ELIMINAR_TAG);
        free(path_tag);
        return;
    }

    // Leer metadata del tag
    char* path_metadata = string_from_format("%s/metadata.config", path_tag);
    t_config* metadata = config_create(path_metadata);

    if (metadata == NULL) {
        free(path_tag);
        free(path_metadata);
        return;
    }

    // Obtener lista de bloques lógicos
    char** bloques = config_get_array_value(metadata, "BLOQUES");  // [2,1,0]

    // ruta de bloques lógicos
    char* path_logical_blocks = construir_ruta(path_tag, "logical_blocks");

    // Recorrer y hacer unlink de cada bloque lógico
    for (int i = 0; bloques[i] != NULL; i++) {
        char* path_logico = string_from_format("%s/%06d.dat", path_logical_blocks, i);
        char* bloque_fisico = string_from_format("block%04d.dat", atoi(bloques[i]));
        char* bloque_logico = string_from_format("%06d.dat", i);
        if (unlink(path_logico) == 0) {
           log_info(log_storage, "##<%d> - <%s>:<%s> Se eliminó el hard link del bloque lógico <%s> al bloque físico <%s>", query_id,file_name_eliminar,nombre_tag,bloque_logico,bloque_fisico);
        } 
        free(path_logico);
    }
    // Recorrer bloques fisicos asignados y liberar en bitmap si no estan referenciados

    for (int i = 0; bloques[i] != NULL; i++) {
        char* ruta_bloque = string_from_format("%s/block%04d.dat", path_physical_blocks_dir, atoi(bloques[i]));

        struct stat st;
        if (stat(ruta_bloque, &st) == 0) {
            if (st.st_nlink == 1) {

                cambiar_estado_bloque_a_libre(atoi(bloques[i]));
            } else {

            }
        } 
        free(ruta_bloque);
    }



    // Eliminar el directorio del tag recursivamente hata que no queden archivos
    eliminar_directorio_recursivo(path_tag);
    enviar_respuesta_a_worker("OK", socket_del_worker, ELIMINAR_TAG);

    // Liberar memoria
    config_destroy(metadata);
    string_array_destroy(bloques);
    free(path_tag);
    free(path_metadata);
    free(path_logical_blocks);
}

void eliminar_directorio_recursivo(char* path) {
    DIR* dir = opendir(path);
    if (!dir) return;

    struct dirent* entry;
    char full_path[512];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                eliminar_directorio_recursivo(full_path);
                rmdir(full_path);
            } else {
                unlink(full_path);
            }
        }
    }

    closedir(dir);
    rmdir(path);
}


void cambiar_estado_bloque_a_libre( int indice_bloque) {
    pthread_mutex_lock(&mutex_global_bitmap);

    if (indice_bloque < 0 || indice_bloque >= bitarray_get_max_bit(global_bitmap)) {

        pthread_mutex_unlock(&mutex_global_bitmap);
        return;
    }

    bool estado_actual = bitarray_test_bit(global_bitmap, indice_bloque);

    if (estado_actual) {
        bitarray_clean_bit(global_bitmap, indice_bloque);


    } 

    // Abrir archivo bitmap en modo escritura binaria
    FILE* archivo_bitmap = fopen(path_bitmap_bin_file, "wb");
    if (archivo_bitmap == NULL) {

        pthread_mutex_unlock(&mutex_global_bitmap);
        return;
    }

    size_t tamanio_bytes = global_bitmap->size;
    size_t escritos = fwrite(global_bitmap->bitarray, sizeof(uint8_t), tamanio_bytes, archivo_bitmap);
    fclose(archivo_bitmap);

    pthread_mutex_unlock(&mutex_global_bitmap);
}

void cambiar_estado_bloque_a_ocupado(int indice_bloque) {
    pthread_mutex_lock(&mutex_global_bitmap);

    if (indice_bloque < 0 || indice_bloque >= bitarray_get_max_bit(global_bitmap)) {
        pthread_mutex_unlock(&mutex_global_bitmap);
        return;
    }

    bool estado_actual = bitarray_test_bit(global_bitmap, indice_bloque);

    if (!estado_actual) {
        bitarray_set_bit(global_bitmap, indice_bloque);
    }

    // Abrir archivo bitmap en modo escritura binaria
    FILE* archivo_bitmap = fopen(path_bitmap_bin_file, "wb");
    if (archivo_bitmap == NULL) {
        pthread_mutex_unlock(&mutex_global_bitmap);
        return;
    }

    size_t tamanio_bytes = global_bitmap->size;
    size_t escritos = fwrite(global_bitmap->bitarray, sizeof(uint8_t), tamanio_bytes, archivo_bitmap);
    fclose(archivo_bitmap);

    pthread_mutex_unlock(&mutex_global_bitmap);
}

void crear_directorio( char* path) {
    if (mkdir(path, 0777) == -1) {
        if (errno == EEXIST) {
            return;
        } else {
            exit(EXIT_FAILURE);
        }
    } 
}

char* construir_ruta( char* raiz,  char* nombre) {
    size_t len = strlen(raiz) + strlen(nombre) + 2; // +1 para '/' y +1 para '\0'
    char* ruta = malloc(len);
    if (!ruta) {
        exit(EXIT_FAILURE);
    }
    ruta[0] = '\0';
    strcat(ruta, raiz);
    strcat(ruta, "/");
    strcat(ruta, nombre);
    return ruta;
}

void inicializar_superblock(char* ruta_superblock) {
    //  PARA ESTE CASO EL ARCHIVO YA EXISTE, ENTONCES LEO Y OBTENGO SUS VALORES
    if (access(ruta_superblock, F_OK) != -1) { 


        t_config* config_superblock = config_create(ruta_superblock);
        if (config_superblock == NULL) {

            exit(EXIT_FAILURE);
        }

        FS_SIZE = config_get_int_value(config_superblock, "FS_SIZE"); // VARIABLE GLOBAL
        BLOCK_SIZE = config_get_int_value(config_superblock, "BLOCK_SIZE");// VARIABLE GLOBAL
        config_save(config_superblock);

        config_destroy(config_superblock);
            // Calcular cantidad de bloques físicos
        int cantidad_bloques = FS_SIZE / BLOCK_SIZE;

        // Crear los bloques físicos
        for (int i = 1; i < cantidad_bloques; i++) {
            char nombre_bloque[20]; // "block" + 4 dígitos + ".dat" + '\0' = 15, un poco más para seguridad
            sprintf(nombre_bloque, "block%04d.dat", i);
        
            char ruta_bloque[256]; // Ajustar según longitud máxima de path
            snprintf(ruta_bloque, sizeof(ruta_bloque), "%s/%s", path_physical_blocks_dir, nombre_bloque);
        
            // Si el bloque no existe, crearlo
            if (access(ruta_bloque, F_OK) == -1) {
                FILE* bloque = fopen(ruta_bloque, "w+"); // w+ para crear y permitir lectura/escritura
                if (bloque == NULL) {
                    continue;
                }
                
                fclose(bloque);

            }
        }
            return;
        }
}

int buscar_primer_bit_libre(t_bitarray* bitarray) {
    for (size_t i = 0; i < bitarray_get_max_bit(bitarray); i++) {
        if (!bitarray_test_bit(bitarray, i)) {
            return i; // Retorna la posición del primer bit en 0
        }
    }
    return -1; // No se encontró ningún bit en 0
}

void inicializar_bitmap(char* ruta_bitMap) {
    FILE* archivo_bitmap = fopen(ruta_bitMap, "rb+");

    // Caso A: Inicialización desde cero (archivo no existe o se eliminó)
    if (archivo_bitmap == NULL) {

        // Calcular cantidad de bloques
        uint32_t cantidad_bloques = FS_SIZE / BLOCK_SIZE;

        // Calcular tamaño en bytes del bitarray
        size_t tamanio_bytes = (cantidad_bloques + 7) / 8; // Redondeo hacia arriba


        // Crear buffer inicializado en ceros
        uint8_t* buffer = calloc(tamanio_bytes, sizeof(uint8_t));
        if (buffer == NULL) {

            return;
        }

        // Crear la estructura bitarray
        global_bitmap = bitarray_create_with_mode((char *)buffer, tamanio_bytes, LSB_FIRST);
        if (global_bitmap == NULL) {

            free(buffer);
            return;
        }

        // Marcar bloque 0 como ocupado (para el initial_file)
        bitarray_set_bit(global_bitmap, 0);


        // Persistir el bitmap en disco
        archivo_bitmap = fopen(ruta_bitMap, "wb");
        if (archivo_bitmap == NULL) {
            bitarray_destroy(global_bitmap);
            free(buffer);
            return;
        }

        size_t escritos = fwrite(buffer, sizeof(uint8_t), tamanio_bytes, archivo_bitmap);
        if (escritos != tamanio_bytes) {

            fclose(archivo_bitmap);
            bitarray_destroy(global_bitmap);
            free(buffer);
            return;
        }

        fclose(archivo_bitmap);

    } 
    // Caso B: Restauración de FS existente
    else {


        // Obtener tamaño del archivo
        fseek(archivo_bitmap, 0, SEEK_END);
        long file_size = ftell(archivo_bitmap);
        rewind(archivo_bitmap);

        // Calcular cantidad de bloques esperada
        uint32_t cantidad_bloques = FS_SIZE / BLOCK_SIZE;
        size_t tamanio_esperado = (cantidad_bloques + 7) / 8;

        // Leer el contenido del archivo
        size_t tamanio_leer = (file_size < tamanio_esperado) ? file_size : tamanio_esperado;
        uint8_t* buffer = malloc(tamanio_leer);
        if (buffer == NULL) {
            fclose(archivo_bitmap);
            return;
        }

        size_t leidos = fread(buffer, sizeof(uint8_t), tamanio_leer, archivo_bitmap);
        if (leidos != tamanio_leer) {
            free(buffer);
            fclose(archivo_bitmap);
            return;
        }

        fclose(archivo_bitmap);

        // Si el archivo era más pequeño, completar con ceros
        if (tamanio_leer < tamanio_esperado) {
            uint8_t* buffer_realloc = realloc(buffer, tamanio_esperado);
            if (buffer_realloc == NULL) {
                free(buffer);
                return;
            }
            buffer = buffer_realloc;
            memset(buffer + tamanio_leer, 0, tamanio_esperado - tamanio_leer);
        }

        // Crear la estructura bitarray
        global_bitmap = bitarray_create_with_mode((char*)buffer, tamanio_esperado, LSB_FIRST);
        if (global_bitmap == NULL) {
            free(buffer);
            return;
        }
    }
}



void eliminar_contenido(char *path,  char *preservar) {
    DIR *dir = opendir(path);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    char full_path[1024];

    while ((entry = readdir(dir)) != NULL) {
        // ignorar "." y ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        // Si es el archivo que queremos preservar (superblock.config), saltar
        if (preservar && strcmp(entry->d_name, preservar) == 0) {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) == -1) {
            perror("stat");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Eliminar contenido recursivamente
            eliminar_contenido(full_path, NULL);
            if (rmdir(full_path) == -1) {

            } 
        } else {
            // Es archivo → eliminar
            if (remove(full_path) == -1) {

            } 
        }
    }

    closedir(dir);
}
  
void inicializar_el_fs ( ){

    // Crear directorio raíz 
    crear_directorio(PUNTO_MONTAJE);
    
    // Si FRESH_START=TRUE → limpiar FS previo
    if (strcmp(FRESH_START, "TRUE") == 0) {

        eliminar_contenido(PUNTO_MONTAJE, SUPERBLOCK_FILE);
    }

    // CREACION DE RUTAS 
    path_superblock_config_file = construir_ruta(PUNTO_MONTAJE, SUPERBLOCK_FILE);
    path_bitmap_bin_file = construir_ruta(PUNTO_MONTAJE, BITMAP_FILE);
    path_blocks_hash_index_config_file = construir_ruta(PUNTO_MONTAJE, HASH_INDEX_FILE);
    path_physical_blocks_dir = construir_ruta(PUNTO_MONTAJE, DIR_BLOCKS);
    path_files_dir = construir_ruta(PUNTO_MONTAJE, DIR_FILES);

    // SE CREAN LOS DIRECTORIOS Y ARCHIVOS 

    if (access(path_blocks_hash_index_config_file, F_OK) == -1) {
        FILE* f = fopen(path_blocks_hash_index_config_file, "a");
        fclose(f);

    } 
    crear_directorio(path_physical_blocks_dir);

  
    crear_directorio(path_files_dir);


    // INICIALIZACION DE SUPERBLOQUE Y BITMAP

    inicializar_superblock(path_superblock_config_file);
    inicializar_bitmap(path_bitmap_bin_file);

    funcion_para_create_file ("initial_file", "BASE"); 
    crear_bloque_inicial();
    usleep(RETARDO_ACCESO_BLOQUE * 1000);

}

void crear_bloque_inicial() {
    char* block0 = construir_ruta(path_physical_blocks_dir, "block0000.dat");
    FILE* archivo = fopen(block0, "r+");  // Intenta abrirlo en modo lectura/escritura

    if (archivo == NULL) {
        // El archivo no existe → se crea
        archivo = fopen(block0, "a+");
        if (archivo == NULL) {
            free(block0);
            return;
        }
        char cero = '0';// NOSE SI EL BLOQUE FISICO DEBERÁ CREAR 
        for (int i = 0; i < BLOCK_SIZE; i++) {
            fwrite(&cero, sizeof(char), 1, archivo);
        }

        fflush(archivo);
        fseek(archivo, 0, SEEK_SET);  // volver al inicio para leer después
    }

    // --- Lectura del contenido ---
    char* contenido = malloc(BLOCK_SIZE + 1);
    memset(contenido, 0, BLOCK_SIZE + 1);
    fread(contenido, sizeof(char), BLOCK_SIZE, archivo);
    fclose(archivo);
    // --- Hash ---
    char* hash = crypto_md5(contenido, BLOCK_SIZE);


    // --- Guardar o actualizar el hash ---
    t_config* hash_config = config_create(path_blocks_hash_index_config_file);
    config_set_value(hash_config, "block0000", hash);
    config_save(hash_config);
    config_destroy(hash_config);

    // --- Crear enlace simbólico si no existe ---
    char* ruta_file_init_ = buscar_directorio(path_files_dir, "initial_file", "BASE");
    char* ruta_bloques_file_init = construir_ruta(ruta_file_init_, "logical_blocks");
    char* ruta_logical_block = construir_ruta(ruta_bloques_file_init, "000000.dat");

    if (access(ruta_logical_block, F_OK) != 0) {
        // Si no existe el enlace simbolico, lo crea
        link(block0, ruta_logical_block);
        cambiar_estado_bloque_a_ocupado(0);
    } 
    // --- Actualizar metadata ---
    char* ruta_meta = construir_ruta(ruta_file_init_, "metadata.config");
    t_config* metadata_config = config_create(ruta_meta);

    char* tamanio_del_metadata = string_itoa(BLOCK_SIZE);
    config_set_value(metadata_config, "TAMANIO", tamanio_del_metadata);
    config_set_value(metadata_config, "ESTADO", "COMMITED");
    config_set_value(metadata_config, "BLOQUES", "[0]");
    config_save(metadata_config);

    // --- Liberación de memoria ---
    config_destroy(metadata_config);
    free(block0);
    free(ruta_logical_block);
    free(ruta_file_init_);
    free(ruta_bloques_file_init);
    free(ruta_meta);
    free(tamanio_del_metadata);
    free(hash);
    free(contenido);
}
bool funcion_para_create_file(char* nombre_file, char* tag) {
    pthread_mutex_lock(&mutex_global_files_metadata);
    // --- Construir rutas ---
    char* ruta_file = construir_ruta(path_files_dir, nombre_file);
    char* ruta_tag = construir_ruta(ruta_file, tag);
    struct stat st_create_tag;
    if(stat(ruta_tag, &st_create_tag) >=0){
        free(ruta_file);
        free(ruta_tag);
        pthread_mutex_unlock(&mutex_global_files_metadata);
        return false;
    }
    char* ruta_logical_blocks = construir_ruta(ruta_tag, "logical_blocks");
    char* ruta_metadata = construir_ruta(ruta_tag, "metadata.config");
    // Verificar si el directorio file existe, si existe, retornara un false. 
    if (access(ruta_file, F_OK) == 0) {
        // El directorio del file ya existe
        pthread_mutex_unlock(&mutex_global_files_metadata);
        t_config* metadata_config = config_create(ruta_metadata);
        if (metadata_config == NULL) {
            pthread_mutex_unlock(&mutex_global_files_metadata);
        }
        char* tamanio = config_get_string_value(metadata_config, "TAMANIO");
        char* estado = config_get_string_value(metadata_config, "ESTADO");
        char* bloques = config_get_string_value(metadata_config, "BLOQUES");

        // Reconstruir estructuras internas (file y tag)
        t_file* file = malloc(sizeof(t_file));
        file->file_name = strdup(nombre_file);
        file->tags = dictionary_create();
        pthread_mutex_init(&file->mutex_tags_map, NULL);

        t_tag_metadata* tag_metadata = malloc(sizeof(t_tag_metadata));
        tag_metadata->tag_name = strdup(tag);
        tag_metadata->size = atoi(tamanio);
        tag_metadata->status = strcmp(estado, "COMMITED") == 0 ? COMMITED : WORK_IN_PROGRESS;
        tag_metadata->physical_blocks = list_create(); // podrías parsear "BLOQUES" si querés rearmar la lista
        pthread_mutex_init(&tag_metadata->mutex_metadata, NULL);

        dictionary_put(file->tags, tag, tag_metadata);
        dictionary_put(global_files_metadata, nombre_file, file);

        config_destroy(metadata_config);
    
        free(ruta_file);
        free(ruta_tag);
        free(ruta_metadata);
        free(ruta_logical_blocks);
        return false;
    }

    // --- Crear nuevo archivo ---

    crear_directorio(ruta_file);
    crear_directorio(ruta_tag);
    crear_directorio(ruta_logical_blocks);

    // Crear y configurar metadata nueva
    FILE* archivo_metadata = fopen(ruta_metadata, "w");
    if (archivo_metadata == NULL) {
        pthread_mutex_unlock(&mutex_global_files_metadata);
    }

    fclose(archivo_metadata);

    t_config* metadata_config = config_create(ruta_metadata);
    config_set_value(metadata_config, "TAMANIO", "0");
    config_set_value(metadata_config, "ESTADO", "WORK_IN_PROGRESS");
    config_set_value(metadata_config, "BLOQUES", "[]");
    config_save(metadata_config);
    config_destroy(metadata_config);

    // Crear estructuras internas
    t_file* file = malloc(sizeof(t_file));
    file->file_name = strdup(nombre_file);
    file->tags = dictionary_create();
    pthread_mutex_init(&file->mutex_tags_map, NULL);

    t_tag_metadata* tag_metadata = malloc(sizeof(t_tag_metadata));
    tag_metadata->tag_name = strdup(tag);
    tag_metadata->size = 0;
    tag_metadata->status = WORK_IN_PROGRESS;
    tag_metadata->physical_blocks = list_create();
    pthread_mutex_init(&tag_metadata->mutex_metadata, NULL);

    dictionary_put(file->tags, tag, tag_metadata);
    dictionary_put(global_files_metadata, nombre_file, file);



    pthread_mutex_unlock(&mutex_global_files_metadata);

    free(ruta_file);
    free(ruta_tag);
    free(ruta_metadata);
    free(ruta_logical_blocks);
    return true;
}

int main(int argc, char* argv[]) {
    
    pthread_mutex_init(&mutex_global_bitmap, NULL);
    pthread_mutex_init(&mutex_global_blocks_hash_index, NULL);
    pthread_mutex_init(&mutex_global_files_metadata, NULL);
    pthread_mutex_init(&mutex_connected_workers_list, NULL);
    pthread_mutex_init(&mutex_cantidad_de_workers, NULL);
    
    pthread_mutex_lock(&mutex_cantidad_de_workers);
    cantidad_total_de_workers=0; 
    pthread_mutex_unlock(&mutex_cantidad_de_workers);

    global_files_metadata = dictionary_create();



    comenzar_archivo_configuracion(argv[1]);
    int fd_storage = iniciar_servidor(PUERTO_ESCUCHA, log_storage, "SERVIDOR STORAGE");
    inicializar_el_fs ( );
    creacion_de_hilos_para_escuchar_workers(fd_storage);
    pthread_mutex_destroy(&mutex_global_bitmap);
    pthread_mutex_destroy(&mutex_global_blocks_hash_index);
    pthread_mutex_destroy(&mutex_global_files_metadata);
    pthread_mutex_destroy(&mutex_connected_workers_list);
    pthread_mutex_destroy(&mutex_cantidad_de_workers);
    return 0;
}

char* buscar_directorio(char* base_path, char* padre, char* objetivo) {
    DIR* dir;
    struct dirent* entry;
    char ruta[2048];

    // Construir la ruta del "padre" dentro del punto de montaje
    snprintf(ruta, sizeof(ruta), "%s/%s", base_path, padre);

    dir = opendir(ruta);
    if (!dir) {

        return NULL;
    }

    // Recorremos lo que esta dentro del directorio padre
    while ((entry = readdir(dir)) != NULL) {
        // Ignorar "." y ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char subruta[2048];
        snprintf(subruta, sizeof(subruta), "%s/%s", ruta, entry->d_name);

        struct stat st;
        if (stat(subruta, &st) == -1)
            continue;

        if (S_ISDIR(st.st_mode)) {
            // Si encontramos el directorio objetivo --> devolver ruta completa
            if (strcmp(entry->d_name, objetivo) == 0) {
                closedir(dir);
                return strdup(subruta);
            }

            // Buscar recursivamente dentro
            char* encontrado = buscar_directorio(subruta, "", objetivo);
            if (encontrado) {
                closedir(dir);
                return encontrado;
            }
        }
    }

    closedir(dir);
    return NULL;
}


bool duplicar_tag(char* nombre_archivo_origen, char* tag_origen, char* nombre_archivo_destino, char* tag_destino,int query_id) {
    // Construir rutas base
    char* path_archivo_origen = construir_ruta(path_files_dir, nombre_archivo_origen);
    char* path_tag_origen = construir_ruta(path_archivo_origen, tag_origen);

    char* path_archivo_destino = construir_ruta(path_files_dir, nombre_archivo_destino);
    char* path_tag_destino = construir_ruta(path_archivo_destino, tag_destino);
    struct stat st_dup_tag;
    // Verificar si el tag destino ya existe dentro del archivo destino
    if (stat(path_tag_destino, &st_dup_tag) >= 0) {
        log_info(log_storage, "ya existe este file tag");
        free(path_archivo_origen);
        free(path_tag_origen);
        free(path_archivo_destino);
        free(path_tag_destino);
        return false;
    }

    // Crear el nuevo tag dentro del archivo destino
    mkdir(path_tag_destino, 0777);
    char* path_logical_destino = construir_ruta(path_tag_destino, "logical_blocks");
    mkdir(path_logical_destino, 0777);

    // Copiar metadata.config
    char* path_metadata_origen = construir_ruta(path_tag_origen, "metadata.config");
    char* path_metadata_destino = construir_ruta(path_tag_destino, "metadata.config");

    FILE* src = fopen(path_metadata_origen, "r+");
    FILE* dst = fopen(path_metadata_destino, "w+");
    if (!src || !dst) {

        if (src) fclose(src);
        if (dst) fclose(dst);
        free(path_archivo_origen);
        free(path_tag_origen);
        free(path_archivo_destino);
        free(path_tag_destino);
        free(path_logical_destino);
        free(path_metadata_origen);
        free(path_metadata_destino);

    }

    char buffer[1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) { //copiando todo el contenido del metadata.config del archivo original
        fwrite(buffer, 1, n, dst);
    }
    fclose(src);
    fclose(dst);

    // Modificar el estado del nuevo metadata
    t_config* metadata = config_create(path_metadata_destino);
    config_set_value(metadata, "ESTADO", "WORK_IN_PROGRESS");
    char** bloques = config_get_array_value(metadata, "BLOQUES"); // "[14,4,5,2,1]"
    config_save(metadata);
    config_destroy(metadata);
    t_list* lista_bloques = list_create();
    for (int i = 0; bloques[i] != NULL; i++) {
        list_add(lista_bloques, strdup(bloques[i])); // strdup para copiar el string a la lista
    }

    char* num_bloque = string_new();
    for (int i = 0; i < list_size(lista_bloques); i++) {
        char* num_bloque = list_get(lista_bloques, i); // "14", "4", etc.

        // Ruta del bloque físico original
        char* bloque_fisico = string_new();
        string_append(&bloque_fisico, path_physical_blocks_dir);
        string_append(&bloque_fisico, "/block");

        char num_str[10];
        sprintf(num_str, "%04d", atoi(num_bloque));
        string_append(&bloque_fisico, num_str);
        string_append(&bloque_fisico, ".dat");

        // Nombre lógico nuevo (secuencial)
        char bloque_logico_dest[512];
        sprintf(bloque_logico_dest, "%s/%06d.dat", path_logical_destino, i);

        // Crear hard link
        if (link(bloque_fisico, bloque_logico_dest) == -1) {
            log_info(log_storage, "Error al crear hard link de %s --> %s (%s)",
                     bloque_fisico, bloque_logico_dest, strerror(errno));
        } else {
            log_info(log_storage, "##<%d> - <%s>:<%s> Se agregó el hard link del bloque lógico <%s> al bloque físico <%s>", query_id,nombre_archivo_destino,tag_destino,bloque_logico_dest,bloque_fisico);
            cambiar_estado_bloque_a_ocupado(atoi(num_bloque));
        }

        free(bloque_fisico);
    }
    free(num_bloque);
    // ---------------- LIBERAR LISTA DE BLOQUES

    for (int i = 0; i < list_size(lista_bloques); i++) {
        free(list_get(lista_bloques, i));
    }
    list_destroy(lista_bloques);

    // -----------------------------------------


    free(path_archivo_origen);
    free(path_tag_origen);
    free(path_archivo_destino);
    free(path_tag_destino);
    free(path_logical_destino);
    free(path_metadata_origen);
    free(path_metadata_destino);
    if (bloques != NULL) {
        for (int i = 0; bloques[i] != NULL; i++) {
            free(bloques[i]); // liberás cada string
        }
        free(bloques); // liberás el array en sí
    }

    return true;
}

bool truncate_file(char* file_name,char* tag,int size_new,int query_identificador){ 
    char* ruta_file = construir_ruta(path_files_dir, file_name);
    struct stat st;
    if (stat(ruta_file, &st) != 0) {
        free(ruta_file);
        return false;
    }
    char* ruta_tag = construir_ruta(ruta_file, tag);
    if (stat(ruta_tag, &st) != 0) {
        free(ruta_file);
        free(ruta_tag);
        return false;
    }
    char* ruta_metadata = construir_ruta(ruta_tag, "metadata.config");
    t_config* config_file_tag = config_create(ruta_metadata);
    char* viejo_size = config_get_string_value(config_file_tag, "TAMANIO");
    int size_old = atoi(viejo_size);
    int cant_bloques_previos = size_old / BLOCK_SIZE;
    char* size_str = string_itoa(size_new);
    config_set_value(config_file_tag, "TAMANIO", size_str);
    char** array_bloques_fisicos = config_get_array_value(config_file_tag, "BLOQUES");
    t_list* lista_bloques_fisicos_truncate = list_create();
    for (int i = 0; array_bloques_fisicos[i] != NULL; i++) {
        list_add(lista_bloques_fisicos_truncate, strdup(array_bloques_fisicos[i])); // strdup para copiar el string a la lista
    }
    char* ruta_logical_blocks = construir_ruta(ruta_tag, "logical_blocks");
    int cant_bloques_actuales = size_old / BLOCK_SIZE;
    if(size_new > size_old){
        int cant_bloques_a_asignar = ((size_new- size_old) / BLOCK_SIZE);
        
        char* block0 = NULL;
        char* nuevo_bloque = NULL;
        char* ruta_nuevo_bloque = NULL;
        for(int i=0; i < cant_bloques_a_asignar; i++){
            
            char* str_cant_bloques_actuales = string_itoa(cant_bloques_actuales);
            int cant_digitos = strlen(str_cant_bloques_actuales);
            if(cant_digitos == 1){ // 000+ num
                nuevo_bloque = string_from_format("00000%s.dat", str_cant_bloques_actuales);
                ruta_nuevo_bloque = construir_ruta(ruta_logical_blocks, nuevo_bloque);
                block0 = construir_ruta(path_physical_blocks_dir, "block0000.dat");
                
            }else if(cant_digitos == 2){
                nuevo_bloque = string_from_format("0000%s.dat", str_cant_bloques_actuales);
                ruta_nuevo_bloque = construir_ruta(ruta_logical_blocks, nuevo_bloque);
                block0 = construir_ruta(path_physical_blocks_dir, "block0000.dat");

            }else if(cant_digitos == 3){
                nuevo_bloque = string_from_format("000%s.dat", str_cant_bloques_actuales);
                ruta_nuevo_bloque = construir_ruta(ruta_logical_blocks, nuevo_bloque);
                block0 = construir_ruta(path_physical_blocks_dir, "block0000.dat");

            }else if(cant_digitos == 4){
                nuevo_bloque = string_from_format("00%s.dat", str_cant_bloques_actuales);
                ruta_nuevo_bloque = construir_ruta(ruta_logical_blocks, nuevo_bloque);
                block0 = construir_ruta(path_physical_blocks_dir, "block0000.dat");

            }else if(cant_digitos == 5){
                nuevo_bloque = string_from_format("0%s.dat", str_cant_bloques_actuales);
                ruta_nuevo_bloque = construir_ruta(ruta_logical_blocks, nuevo_bloque);
                block0 = construir_ruta(path_physical_blocks_dir, "block0000.dat");

            }
            else if(cant_digitos == 6){
                nuevo_bloque = string_from_format("%s.dat", str_cant_bloques_actuales);
                ruta_nuevo_bloque = construir_ruta(ruta_logical_blocks, nuevo_bloque);
                block0 = construir_ruta(path_physical_blocks_dir, "block0000.dat");

            }
            free(str_cant_bloques_actuales);


            link(block0, ruta_nuevo_bloque);
            list_add(lista_bloques_fisicos_truncate,"0");
            log_info(log_storage, "##<%d> - <%s>:<%s> Se agregó el hard link del bloque lógico <%s> al bloque físico <block0000.dat>", query_identificador,file_name,tag,nuevo_bloque);
            free(block0);
            free(ruta_nuevo_bloque);
            free(nuevo_bloque);
            cant_bloques_actuales++;
        }
    }else if(size_new < size_old){
        int cant_bloques_a_desasignar = ((size_old - size_new )/ BLOCK_SIZE);
        for(int i=0; i < cant_bloques_a_desasignar; i++){
            int cant_bloques_actuales = size_old / BLOCK_SIZE;
            char* str_cant_bloques_actuales = string_itoa(cant_bloques_actuales);
            int cant_digitos = strlen(str_cant_bloques_actuales);
            char* bloque_a_eliminar = NULL;
            char* ruta_bloque_a_eliminar = NULL;
            if(cant_digitos == 1){
                bloque_a_eliminar = string_from_format("block000%s.dat", str_cant_bloques_actuales);
                ruta_bloque_a_eliminar = construir_ruta(ruta_logical_blocks, bloque_a_eliminar);
                remove(ruta_bloque_a_eliminar);
            }else if(cant_digitos == 2){
                bloque_a_eliminar = string_from_format("block00%s.dat", str_cant_bloques_actuales);
                ruta_bloque_a_eliminar = construir_ruta(ruta_logical_blocks, bloque_a_eliminar);
                remove(ruta_bloque_a_eliminar);
            }else if(cant_digitos == 3){
                bloque_a_eliminar = string_from_format("block0%s.dat", str_cant_bloques_actuales);
                ruta_bloque_a_eliminar = construir_ruta(ruta_logical_blocks, bloque_a_eliminar);
                remove(ruta_bloque_a_eliminar);
            }else if(cant_digitos == 4){
                bloque_a_eliminar = string_from_format("block%s.dat", str_cant_bloques_actuales);
                ruta_bloque_a_eliminar = construir_ruta(ruta_logical_blocks, bloque_a_eliminar);
                remove(ruta_bloque_a_eliminar);
            }
            struct stat status_bloque_fisico;
            
            char* num_bloque_fisico =NULL;
            num_bloque_fisico = array_bloques_fisicos[cant_bloques_actuales - 1 - i];
            int cant_digitos_fis = strlen(num_bloque_fisico);
            char* bloque_fisico_a_eliminar = NULL;
            if(cant_digitos_fis == 1){
                bloque_fisico_a_eliminar = string_from_format("block000%s.dat", num_bloque_fisico);
            }else if(cant_digitos_fis == 2){
                bloque_fisico_a_eliminar = string_from_format("block00%s.dat", num_bloque_fisico);
            }else if(cant_digitos_fis == 3){
                bloque_fisico_a_eliminar = string_from_format("block0%s.dat", num_bloque_fisico);
            }else if(cant_digitos_fis == 4){
                bloque_fisico_a_eliminar = string_from_format("block%s.dat", num_bloque_fisico);
            }
            char* ruta_bloque_fisico_a_eliminar = NULL;
            ruta_bloque_fisico_a_eliminar = construir_ruta(path_physical_blocks_dir, bloque_fisico_a_eliminar);
            stat(ruta_bloque_fisico_a_eliminar, &status_bloque_fisico);
            if(status_bloque_fisico.st_nlink == 1){
                // ES EL UNICO ENLACE QUE TIENE, ENTONCES LIBERO EL BLOQUE EN EL BITMAP
                int indice_bloque = atoi(num_bloque_fisico);
                pthread_mutex_lock(&mutex_global_bitmap);
                cambiar_estado_bloque_a_libre(indice_bloque);
                pthread_mutex_unlock(&mutex_global_bitmap);
            }
            free(ruta_bloque_fisico_a_eliminar);
            
            free(bloque_fisico_a_eliminar);
            
            free(bloque_a_eliminar);
            free(ruta_bloque_a_eliminar);  
            free(str_cant_bloques_actuales);
        }
        
    }

    char* string_de_bloques_truncate = string_new();
    string_append(&string_de_bloques_truncate, "[");
    for(int i = 0; i < list_size(lista_bloques_fisicos_truncate); i++) {
        if(i > 0) string_append(&string_de_bloques_truncate, ",");
        char* bloque_num = list_get(lista_bloques_fisicos_truncate, i);
        string_append(&string_de_bloques_truncate, bloque_num);
    }
    string_append(&string_de_bloques_truncate, "]");
    config_set_value(config_file_tag, "BLOQUES", string_de_bloques_truncate); 
    config_save(config_file_tag);
    config_destroy(config_file_tag);
    string_array_destroy(array_bloques_fisicos);
    free(ruta_logical_blocks);
    free(ruta_file);
    free(size_str);
    free(ruta_tag);
    

    free(ruta_metadata);
    return true;
}




void enviar_respuesta_a_worker(char* mensaje, int* socket_worker,op_code opcion){
    // ENVIAR RESPUESTA AL WORKER

    t_buffer* buffer_enviar_ejemplo_al_worker = crear_buffer();
    cargar_string_al_buff(buffer_enviar_ejemplo_al_worker, mensaje);
    t_paquete* paquete_para_enviar_al_worker = crear_super_paquete(opcion, buffer_enviar_ejemplo_al_worker);
    enviar_paquete(paquete_para_enviar_al_worker, *socket_worker);
    eliminar_paquete(paquete_para_enviar_al_worker);
    free(buffer_enviar_ejemplo_al_worker);
}

bool commitear_tag(char* src_file, char* src_tag,int query_identificador){
    char* ruta_commit = construir_ruta(path_files_dir, src_file);
    struct stat st;
    if(stat(ruta_commit, &st) != 0){
        log_error(log_storage, "El file no existe en el commit");
        free(ruta_commit);

        return false;
        
    }
    char* nueva_ruta_commit = construir_ruta(ruta_commit, src_tag);
    free(ruta_commit);  // Liberar memoria previa
    ruta_commit = nueva_ruta_commit;
    if(stat(ruta_commit, &st) != 0){
        log_error(log_storage, "El tag no existe en el commit");
        
        free(ruta_commit);
        return false;
    }
    char* ruta_commit_logical_blocks = construir_ruta(ruta_commit, "logical_blocks");
    char* ruta_commit_meta = construir_ruta(ruta_commit, "metadata.config");
    t_config* metadata_commit = config_create(ruta_commit_meta);
    char* estado_commit = config_get_string_value(metadata_commit, "ESTADO");
    int bloque_fisico_int;
    int bloque_fisico_desdup_int;
    if(strcmp(estado_commit, "COMMITED") == 0){

        config_save(metadata_commit);

        config_destroy(metadata_commit);
        free(ruta_commit);
        free(ruta_commit_logical_blocks);
        free(ruta_commit_meta);
        return false;
        
    }else{
        // Marcar como COMMITED y aplicar deduplicacion
        config_set_value(metadata_commit, "ESTADO", "COMMITED");
        
        // Obtener array de bloques fisicos asignados a este tag
        char** bloques_commit = config_get_array_value(metadata_commit, "BLOQUES");
        
        // Crear lista para trabajar con los bloques
        t_list* lista_bloques_commit = list_create();
        int cantidad_bloques = 0;
        for(int i = 0; bloques_commit[i] != NULL; i++){
            list_add(lista_bloques_commit, string_duplicate(bloques_commit[i]));
            cantidad_bloques++;
        }

        // Cargar el indice de hashes
        t_config* hash_index_config = config_create(path_blocks_hash_index_config_file);
        
        // Crear array nuevo para almacenar bloques despues de deduplicacion
        char** bloques_deduplicados = malloc(sizeof(char*) * (cantidad_bloques + 1));
        for(int i = 0; i <= cantidad_bloques; i++){
            bloques_deduplicados[i] = NULL;
        }
        // Procesar cada bloque logico
        for(int bloque_logico = 0; bloque_logico < cantidad_bloques; bloque_logico++){
            char* numero_bloque_fisico = list_get(lista_bloques_commit, bloque_logico);
            bloque_fisico_int = atoi(numero_bloque_fisico);
            // Formatear numero de bloque fisico con padding
            char bloque_fisico_key[10];
            snprintf(bloque_fisico_key, sizeof(bloque_fisico_key), "block%04d", atoi(numero_bloque_fisico));

            // Obtener hash del bloque fisico actual
            char* hash_actual = config_get_string_value(hash_index_config, bloque_fisico_key);
            
            bool duplicado_encontrado = false;
            char* bloque_fisico_existente_key = NULL;

            // DEDUPLICACION GLOBAL: Iterar sobre los bloques ocupados del bitmap
            int max_bits = bitarray_get_max_bit(global_bitmap);
            for(int k=0; k < max_bits; k++){
                if(bitarray_test_bit(global_bitmap, k)){
                    char other_key[20];
                    sprintf(other_key, "block%04d", k);
                    
                    // No compararse a sí mismo
                    if(strcmp(other_key, bloque_fisico_key) == 0) continue;

                    if(config_has_property(hash_index_config, other_key)){
                        char* other_hash = config_get_string_value(hash_index_config, other_key);
                        if(strcmp(hash_actual, other_hash) == 0){
                            duplicado_encontrado = true;
                            bloque_fisico_existente_key = strdup(other_key); 
                            bloque_fisico_desdup_int = bloque_fisico_int;
                            break; 
                        }
                    }
                }
            }
            
            // Si los hashes coinciden, es un duplicado
            if(duplicado_encontrado){
                char* ruta_bloque_fisico_auxiliar = construir_ruta(path_physical_blocks_dir, string_from_format("%s.dat", bloque_fisico_key));
                // Eliminar el archivo logico actual
                char nombre_bloque_logico[15];
                snprintf(nombre_bloque_logico, sizeof(nombre_bloque_logico), "%06d.dat", bloque_logico);
                char* ruta_bloque_logico = construir_ruta(ruta_commit_logical_blocks, nombre_bloque_logico);
                
                if(unlink(ruta_bloque_logico) == 0){
                    // BITMAP UPDATED: Si nlink baja a 1 (solo ref fs), liberar
                    char* ruta_bloque_fisico_check_ref = construir_ruta(path_physical_blocks_dir, string_from_format("%s.dat", bloque_fisico_key));
                    struct stat st_bfcr;
                    
                    if (stat(ruta_bloque_fisico_check_ref, &st_bfcr) == 0) {
                        if(st_bfcr.st_nlink == 1){
                            log_info(log_storage, "marcando bloque %d como libre", bloque_fisico_desdup_int);
                            cambiar_estado_bloque_a_libre(bloque_fisico_desdup_int);
                            log_info(log_storage, "Bloque %s liberado por deduplicación", bloque_fisico_key);
                        }
                    }
                    free(ruta_bloque_fisico_check_ref);
                }
                
                // Crear hard link al bloque fisico YA EXISTENTE (global)
                // bloque_fisico_existente_key tiene "blockXXXX"
                char bloque_fisico_file[20];
                snprintf(bloque_fisico_file, sizeof(bloque_fisico_file), "%s.dat", bloque_fisico_existente_key);
                char* ruta_bloque_fisico = construir_ruta(path_physical_blocks_dir, bloque_fisico_file);
                
                if(link(ruta_bloque_fisico, ruta_bloque_logico) == 0){
                    // Guardamos el numero del bloque REUTILIZADO
                    // bloque_fisico_existente_key es "blockXXXX", queremos "XXXX"
                    char* numero_reutilizado = string_substring_from(bloque_fisico_existente_key, 5); // saltar "block"
                    bloques_deduplicados[bloque_logico] = string_duplicate(numero_reutilizado);
                    free(numero_reutilizado);
                }else{
                    log_error(log_storage, "Error creando hard link. Manteniendo bloque original");
                    bloques_deduplicados[bloque_logico] = string_duplicate(numero_bloque_fisico);
                }
                
                free(ruta_bloque_fisico);
                free(ruta_bloque_logico);
                free(bloque_fisico_existente_key);
                free(ruta_bloque_fisico_auxiliar);
            } else {
                // Si no es duplicado, mantener el bloque original
                bloques_deduplicados[bloque_logico] = string_duplicate(numero_bloque_fisico);
            }
        }
        
        char* string_bloques = string_new();
        string_append(&string_bloques, "[");
        
        for(int i = 0; i < cantidad_bloques; i++){
            int valor = atoi(bloques_deduplicados[i]);   // <-- QUITA CEROS
            char* valor_str = string_itoa(valor);
        
            string_append(&string_bloques, valor_str);
        
            if(i < cantidad_bloques - 1){
                string_append(&string_bloques, ",");
            }
        
            free(valor_str);
        }
        
        string_append(&string_bloques, "]");
        
        config_set_value(metadata_commit, "BLOQUES", string_bloques);
        log_info(log_storage, "Deduplicacion completada. Nuevos bloques: %s", string_bloques);
        
        // Limpiar memoria
        free(string_bloques);
        for(int i = 0; bloques_deduplicados[i] != NULL; i++){
            free(bloques_deduplicados[i]);
        }
        free(bloques_deduplicados);
        
        list_destroy_and_destroy_elements(lista_bloques_commit, free);
        string_array_destroy(bloques_commit);
        config_destroy(hash_index_config);
    }

    config_save(metadata_commit);
    config_destroy(metadata_commit);
    free(ruta_commit);
    free(ruta_commit_logical_blocks);
    free(ruta_commit_meta);
    return true;
}


void escribir_bloque(int query_id_escritura, char* nombre_file_escritura, char* tag_escritura, int numero_bloque_logico, void* data_a_escribir, int* socket_del_worker) {
    struct stat st_0;
    char* ruta = construir_ruta(path_files_dir, nombre_file_escritura);
    if(stat(ruta, &st_0) == -1){
        log_warning(log_storage, "El File %s no existe", nombre_file_escritura);
        enviar_respuesta_a_worker("File inexistente", socket_del_worker, ESCRITURA_BLOQUE);
        return;
    }
    ruta = construir_ruta(ruta, tag_escritura);
    if(stat(ruta, &st_0) == -1){
        log_warning(log_storage, "El Tag %s no existe en el File %s", tag_escritura, nombre_file_escritura);
        enviar_respuesta_a_worker("Tag inexistente", socket_del_worker, ESCRITURA_BLOQUE);
        return;
    }
    char* ruta_meta = string_duplicate(ruta);
    ruta_meta = construir_ruta(ruta_meta, "metadata.config");
    t_config* config_meta = config_create(ruta_meta);
    char* estado = config_get_string_value(config_meta, "ESTADO");
    char** bloques_asignados = config_get_array_value(config_meta, "BLOQUES");
    t_list* lista_bloques = list_create();
    for (int i = 0; bloques_asignados[i] != NULL; i++) {
        list_add(lista_bloques, strdup(bloques_asignados[i])); // strdup para copiar el string a la lista
    }
    if(numero_bloque_logico >= list_size(lista_bloques)){
        log_warning(log_storage, "El bloque lógico %d no está asignado en %s:%s, escritura fuera de limite", numero_bloque_logico, nombre_file_escritura, tag_escritura);
        enviar_respuesta_a_worker("Bloque lógico no asignado", socket_del_worker, ESCRITURA_BLOQUE);
        list_destroy_and_destroy_elements(lista_bloques, free);
        return;
    }
    if (strcmp(estado, "COMMITED") == 0) {
        log_warning(log_storage, "No se puede escribir en un File:Tag en estado COMMITED");
        enviar_respuesta_a_worker("File:Tag en estado COMMITED", socket_del_worker, ESCRITURA_BLOQUE);
        return;
    }
    char* ruta_bloque_logico_especifico = string_duplicate(ruta);
    ruta_bloque_logico_especifico = construir_ruta(ruta_bloque_logico_especifico, "logical_blocks");
    if(strlen(string_itoa(numero_bloque_logico)) < 6){
        char* numero_bloque_formateado = string_repeat('0', 6 - strlen(string_itoa(numero_bloque_logico)));
        string_append(&numero_bloque_formateado, string_itoa(numero_bloque_logico));
        string_append(&numero_bloque_formateado, ".dat");
        ruta_bloque_logico_especifico = construir_ruta(ruta_bloque_logico_especifico, numero_bloque_formateado);
        free(numero_bloque_formateado);
    }
    else{
        ruta_bloque_logico_especifico = construir_ruta(ruta_bloque_logico_especifico, string_itoa(numero_bloque_logico));
    }
    struct stat st_1;
    if (stat(ruta_bloque_logico_especifico, &st_1) == -1) {
        log_warning(log_storage, "El bloque lógico %d no está asignado en %s:%s", numero_bloque_logico, nombre_file_escritura, tag_escritura);
        enviar_respuesta_a_worker("Bloque lógico no asignado", socket_del_worker, ESCRITURA_BLOQUE);
        return;
    }
    if( numero_bloque_logico >= list_size(lista_bloques)) {
        log_warning(log_storage, "Error al obtener bloque físico para bloque lógico %d", numero_bloque_logico);
        enviar_respuesta_a_worker("Error al obtener bloque físico", socket_del_worker, ESCRITURA_BLOQUE);
        string_array_destroy(bloques_asignados);
        list_destroy_and_destroy_elements(lista_bloques, free);
        config_destroy(config_meta);
        return;
    }
    char* bloque_fisico_numero = list_get(lista_bloques,numero_bloque_logico);
    int bloque_fisico = atoi(bloque_fisico_numero);
    // Construir la ruta completa del bloque físico
    char* bloque_fisico_path = construir_ruta(path_physical_blocks_dir, "block");
    char* numero_formateado = string_repeat('0', 4 - strlen(bloque_fisico_numero));
    string_append(&numero_formateado, bloque_fisico_numero);
    string_append(&numero_formateado, ".dat");
    string_append(&bloque_fisico_path, numero_formateado);
    free(numero_formateado);
    
    char* bloque_fisico_asignado = bloque_fisico_path;

    log_info(log_storage, "ruta del BLOQUE FISICO : %s", bloque_fisico_asignado);
    struct stat st_2;
    stat(bloque_fisico_asignado, &st_2);
    if( st_2.st_nlink > 2){//el bloque fisico esta linkeado a si mismo, al bloque logico pasado por buffer y minimo a un bloque logico mas, entonces debo buscar un bloque fisico libre en el bitmap
        pthread_mutex_lock(&mutex_global_bitmap);
        int nuevo_bloque_fisico_index = buscar_primer_bit_libre(global_bitmap);
        pthread_mutex_unlock(&mutex_global_bitmap);
        if(nuevo_bloque_fisico_index == -1){
            log_error(log_storage, "No hay bloques físicos libres disponibles en el bitmap");
            enviar_respuesta_a_worker("No hay bloques físicos libres", socket_del_worker, ESCRITURA_BLOQUE);
            return;
        }
        cambiar_estado_bloque_a_ocupado(nuevo_bloque_fisico_index);
        log_info(log_storage, "##<%d> - Bloque Físico Reservado - Número de Bloque: <%d>", query_id_escritura, nuevo_bloque_fisico_index);
        //deslinkear el bloque logico del bloque fisico 0
        unlink(ruta_bloque_logico_especifico);
        log_info(log_storage, "##<%d> - <%s>:<%s> Se eliminó el hard link del bloque lógico <%d> al bloque físico <0>", query_id_escritura, nombre_file_escritura, tag_escritura, numero_bloque_logico);
        //bloques_asignados[numero_bloque_logico] = string_itoa(nuevo_bloque_fisico_index);
        list_replace(lista_bloques, numero_bloque_logico, strdup(string_itoa(nuevo_bloque_fisico_index)));
        char* nuevo_bloque_fisico_path = construir_ruta(path_physical_blocks_dir, "block");
        char* nuevo_bloque_fisico_formateado = string_repeat('0', 4 - strlen(string_itoa(nuevo_bloque_fisico_index)));
        string_append(&nuevo_bloque_fisico_formateado, string_itoa(nuevo_bloque_fisico_index));
        string_append(&nuevo_bloque_fisico_formateado, ".dat");
        string_append(&nuevo_bloque_fisico_path, nuevo_bloque_fisico_formateado);
        free(nuevo_bloque_fisico_formateado);

        log_info(log_storage, "ruta del nuevo bloque fisico path: %s", nuevo_bloque_fisico_path);
        FILE* archivo_nuevo_bloque_fisico = fopen(nuevo_bloque_fisico_path, "r+");
        if (archivo_nuevo_bloque_fisico == NULL) {
            log_error(log_storage, "No se pudo abrir el bloque físico %s para escritura", bloque_fisico_asignado);
            enviar_respuesta_a_worker("Error al abrir bloque físico", socket_del_worker, ESCRITURA_BLOQUE);
            return;
        } else {
            fseek(archivo_nuevo_bloque_fisico, 0, SEEK_SET);
            fwrite(data_a_escribir, sizeof(char), BLOCK_SIZE, archivo_nuevo_bloque_fisico);
            fflush(archivo_nuevo_bloque_fisico);
            fclose(archivo_nuevo_bloque_fisico);
            log_info(log_storage, "##<%d> - Bloque Lógico Escrito <%s>:<%s> - Número de Bloque: <%d>", query_id_escritura, nombre_file_escritura, tag_escritura, numero_bloque_logico);
            //AGREGAR NUEVO VALOR HASH A LA CONFIG
            // --- Lectura del contenido ---

            log_info(log_storage, "DATA A ESCRIBIR a HASHEAR %s",    data_a_escribir);

            // --- Hash ---
            char* hash = crypto_md5(data_a_escribir, BLOCK_SIZE);
            log_info(log_storage, "HASH : %s", hash);

            // --- Guardar o actualizar el hash ---
            char* key_new_bloque_fisico = malloc(20);
            strcpy(key_new_bloque_fisico, "block");        
            if(nuevo_bloque_fisico_index<1000){
                if(nuevo_bloque_fisico_index<100){
                    if(nuevo_bloque_fisico_index<10){
                        strcat(key_new_bloque_fisico, "0");
                    }
                    strcat(key_new_bloque_fisico, "0");
                }
                strcat(key_new_bloque_fisico, "0");
            }
            char* index_str = string_itoa(nuevo_bloque_fisico_index);
            strcat(key_new_bloque_fisico, index_str);
            free(index_str);
            t_config* hash_config = config_create(path_blocks_hash_index_config_file);
            config_set_value(hash_config, key_new_bloque_fisico, hash);
            config_save(hash_config);
            config_destroy(hash_config);
            free(key_new_bloque_fisico);
            //linkear el bloque logico al nuevo bloque fisico
            if (link(nuevo_bloque_fisico_path, ruta_bloque_logico_especifico) == 0) {
                cambiar_estado_bloque_a_ocupado(nuevo_bloque_fisico_index);
                log_info(log_storage, "##<%d> - <%s>:<%s> Se agregó el hard link del bloque lógico <%d> al bloque físico <%d>", query_id_escritura, nombre_file_escritura, tag_escritura, numero_bloque_logico, nuevo_bloque_fisico_index);
            } else {
                log_error(log_storage, "Error al vincular bloque lógico al nuevo bloque físico");
            }
            enviar_respuesta_a_worker("OK", socket_del_worker, ESCRITURA_BLOQUE);
        }

        
    }else{//escribo directo en el bloque asignado
        FILE* archivo_bloque_fisico = fopen(bloque_fisico_asignado, "r+");
        if (archivo_bloque_fisico == NULL) {
            log_error(log_storage, "No se pudo abrir el bloque físico %s para escritura", bloque_fisico_asignado);
            enviar_respuesta_a_worker("Error al abrir bloque físico", socket_del_worker, ESCRITURA_BLOQUE);
        } else {
            fseek(archivo_bloque_fisico, 0, SEEK_SET);
            fwrite(data_a_escribir, sizeof(char), BLOCK_SIZE, archivo_bloque_fisico);
            fflush(archivo_bloque_fisico);
            fclose(archivo_bloque_fisico);
            log_info(log_storage, "##<%d> - Bloque Lógico Escrito <%s>:<%s> - Número de Bloque: <%d>", query_id_escritura, nombre_file_escritura, tag_escritura, numero_bloque_logico);

            // --- Hash ---
            char* hash = crypto_md5(data_a_escribir, BLOCK_SIZE);
            log_info(log_storage, "HASH : %s", hash);

            // --- Guardar o actualizar el hash ---
            char* key_bloque_fisico = malloc(20);
            strcpy(key_bloque_fisico, "block");
            if(bloque_fisico<1000){
                if(bloque_fisico<100){
                    if(bloque_fisico<10){
                        strcat(key_bloque_fisico, "0");
                    }
                    strcat(key_bloque_fisico, "0");
                }
                strcat(key_bloque_fisico, "0");
            }
            char* index_str_2 = string_itoa(bloque_fisico);
            strcat(key_bloque_fisico, index_str_2);
            free(index_str_2);
            t_config* hash_config = config_create(path_blocks_hash_index_config_file);
            config_set_value(hash_config, key_bloque_fisico, hash);
            config_save(hash_config);
            config_destroy(hash_config);
            //COMENTO ESTO YA QUE JUSTAMENTE SI EL BLOQUE YA ESTA ASIGNADO, YA DEBERIA ESTAR OCUPADO EN EL BITMAP Y LINKEADO AL BLOQUE LOGICO O VICEVERSA
            /*free(key_bloque_fisico);
            //linkear el bloque logico al nuevo bloque fisico
            if (link(bloque_fisico_asignado, ruta_bloque_logico_especifico) == 0) {
                cambiar_estado_bloque_a_ocupado(atoi(bloque_fisico));
                log_info(log_storage, "##<%d> - <%s>:<%s> Se agregó el hard link del bloque lógico <%d> al bloque físico <%d>", query_id_escritura, nombre_file_escritura, tag_escritura, numero_bloque_logico, bloque_fisico_numero);
            } else {
                log_error(log_storage, "Error al vincular bloque lógico al nuevo bloque físico");
            }
            */
            enviar_respuesta_a_worker("OK", socket_del_worker, ESCRITURA_BLOQUE);
        }
    }
    
    free(ruta_bloque_logico_especifico);
    
    char* string_de_bloques_truncate = string_new();
    string_append(&string_de_bloques_truncate, "[");
    for(int i = 0; i < list_size(lista_bloques); i++) {
        if(i > 0) string_append(&string_de_bloques_truncate, ",");
        char* bloque_num = list_get(lista_bloques, i);
        string_append(&string_de_bloques_truncate, bloque_num);
    }
    string_append(&string_de_bloques_truncate, "]");
    config_set_value(config_meta, "BLOQUES", string_de_bloques_truncate); 
    config_save(config_meta);
    config_destroy(config_meta);

    free(ruta_meta);
    return;
}


