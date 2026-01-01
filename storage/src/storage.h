#ifndef STORAGE_H
#define STORAGE_H
#include "../../utils/src/utils/utils.h"
#include <commons/crypto.h>

t_log* log_storage;
t_config* config_storage;

char* PUERTO_ESCUCHA;
char* FRESH_START;
bool is_fresh_start; // Versión booleana de FRESH_START para facilitar la lógica de inicialización.
char* PUNTO_MONTAJE;
int RETARDO_OPERACION;
int RETARDO_ACCESO_BLOQUE;
int LOG_LEVEL;
int cantidad_total_de_workers; 

void se_conecta_un_worker_al_storage(int* socket_del_worker);
bool funcion_para_create_file (char* nombre_file, char* tag);
void crear_bloque_inicial();
char* buscar_directorio(char* punto_montaje, char* directorio_padre, char* directorio_hijo);
bool duplicar_tag(char* nombre_archivo_origen, char* tag_origen, char* nombre_archivo_destino, char* tag_destino,int query_id);
bool truncate_file(char* file_name,char* tag,int size_new,int identificador_query);
void enviar_respuesta_a_worker(char* mensaje, int* socket_worker,op_code opcion);
char* construir_ruta( char* raiz,  char* nombre);
bool commitear_tag(char* src_file, char* src_tag,int query_identificador);
void inicializar_bitmap(char* ruta_bitMap) ;
void eliminar_tag(char* path_file_name, char* nombre_tag,int* socket_del_worker,int query_id,char * nombre_file);
void eliminar_directorio_recursivo(char* path);
void cambiar_estado_bloque_a_libre(int indice_bloque);
void cambiar_estado_bloque_a_ocupado(int indice_bloque);
int buscar_primer_bit_libre(t_bitarray* bitarray) ;
void escribir_bloque(int query_id_escritura, char* nombre_file_escritura, char* tag_escritura, int numero_bloque_logico, void* data_a_escribir, int* socket_del_worker) ;
void lectura(int query_id_lectura, char* nombre_file_lectura, char* tag_lectura, int numero_bloque_logico_lectura, int* socket_del_worker);
// ESTRUCTURAS ADMINISTRATIVAS
typedef enum {
    WORK_IN_PROGRESS,
    COMMITED
} file_status;

//NO SE SI ES NECESARIO LA ESTRUCTURA T_TAG PERO ME CIERRA MAS QUE T_FIL
typedef struct
{
    char* tag_name;
    int size;
    file_status status;
    t_list* physical_blocks;
    pthread_mutex_t mutex_metadata;
} t_tag_metadata;

//NO SE SI ES NECESARIO LA ESTRUCTURA T_FILE
typedef struct
{
    char* file_name;
    t_dictionary* tags;
    pthread_mutex_t mutex_tags_map;
} t_file;
typedef struct
{
    int socket_fd;
    int worker_id;
} t_worker_connection_info;
int FS_SIZE; // Tamaño total del File System en bytes, cargado de superblock.config.
int BLOCK_SIZE; // Tamaño de cada bloque en bytes, cargado de superblock.config.
t_bitarray* global_bitmap; // Puntero al bitarray que gestiona el bitmap.bin (estado libre/ocupado de bloques físicos).
t_dictionary* global_blocks_hash_index; // Puntero al diccionario que gestiona el blocks_hash_index.config (mapeo de hashes a nombres de bloques físicos).
t_dictionary* global_files_metadata; // Diccionario principal que mapea char* file_name a t_file*.
t_list* connected_workers_list; // Lista de t_worker_connection_info* de todos los Workers conectados.

// RUTAS
char* path_physical_blocks_dir; // Ruta al directorio physical_blocks.
char* path_files_dir; // Ruta al directorio files.
char* path_superblock_config_file; // Ruta al archivo superblock.config.
char* path_bitmap_bin_file; // Ruta al archivo bitmap.bin.
char* path_blocks_hash_index_config_file; // Ruta al archivo blocks_hash_index.config.

// SEMAFOROS
pthread_mutex_t mutex_global_bitmap; // Para proteger el acceso al global_bitmap.
pthread_mutex_t mutex_global_blocks_hash_index; // Para proteger el acceso al global_blocks_hash_index.
pthread_mutex_t mutex_global_files_metadata; // Para proteger el acceso al diccionario global_files_metadata.
pthread_mutex_t mutex_connected_workers_list; // Para proteger la lista de Workers conectados.

pthread_mutex_t mutex_cantidad_de_workers; // Proteger el autoincremento de la variable global ( cantidad_total_de_workers ) 

#endif