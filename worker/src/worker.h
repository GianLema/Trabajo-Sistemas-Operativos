
#ifndef WORKER_H
#define WORKER_H
#include "../../utils/src/utils/utils.h"

extern t_log* log_worker;
extern t_config* config_worker;

extern char* IP_MASTER;
extern char* PUERTO_MASTER;
extern char* IP_STORAGE;
extern char* PUERTO_STORAGE;
extern char* ALGORITMO_REEMPLAZO;

extern int TAM_MEMORIA;
extern int RETARDO_MEMORIA;
extern char* PATH_SCRIPTS;
extern int LOG_LEVEL;
extern int BLOCK_SIZE; // Tamaño de bloque obtenido de Storage

// Conexiones
extern int fd_master;
extern int fd_storage;
extern int worker_id;

// Estructuras de memoria interna
typedef struct {
    char* file_name;
    char* tag;
} t_file_tag;

typedef struct {
    t_file_tag file_tag;
    int page_number;
    bool modified;
    bool present;
    void* content;
    time_t last_access; // Para LRU
    int clock_bit; // Para CLOCK-M
    int frame_number; 
    int modified_bit;
} t_page;

typedef struct {
    t_list* pages; // Lista de t_page*
    pthread_mutex_t mutex;
} t_page_table;

typedef struct {
    void* memory_space;
    int total_pages;
    int used_pages;
    t_dictionary* page_tables; // file:tag -> t_page_table*
    t_list* physical_pages; // Para algoritmos de reemplazo
    int clock_pointer; // Para CLOCK-M
    pthread_mutex_t memory_mutex;
    bool* frame_table; // Array de marcos libres/ocupados
} t_internal_memory;

// Estructura para instrucciones
typedef enum {
    INSTR_CREATE,
    INSTR_TRUNCATE,
    INSTR_WRITE,
    INSTR_READ,
    INSTR_TAG,
    INSTR_COMMIT,
    INSTR_FLUSH,
    INSTR_DELETE,
    INSTR_END
} instruction_type;

typedef struct {
    instruction_type type;
    char* file_name;
    char* tag;
    int address;
    int size;
    char* content;
    char* dest_file_name;
    char* dest_tag;
} t_instruction;

// Variables globales
extern t_internal_memory* internal_memory;
extern int current_query_id; // ID de la query actual para logs

// Funciones principales
void iniciar_config_worker(char* ruta_config);
void conectarse_a_master(int id_worker);
void conectarse_a_storage(int id_worker);
void inicializar_memoria_interna(void);
void procesar_query(int query_id, char* query_path);
void procesar_query_hilo(void* args_ptr);

// Query Interpreter
t_instruction* parsear_instruccion(char* linea);
bool ejecutar_instruccion(t_instruction* instruccion);
bool ejecutar_create(char* file_name, char* tag);
bool ejecutar_truncate(char* file_name, char* tag, int size);
bool ejecutar_write(char* file_name, char* tag, int address, char* content);
bool ejecutar_read(char* file_name, char* tag, int address, int size);
bool ejecutar_tag(char* src_file, char* src_tag, char* dest_file, char* dest_tag);
bool ejecutar_commit(char* file_name, char* tag);
bool ejecutar_flush(char* file_name, char* tag);
bool ejecutar_delete(char* file_name, char* tag);
void liberar_instruccion(t_instruction* instruccion);

// Memoria interna
t_page* obtener_pagina(char* file_name, char* tag, int page_number);
t_page* cargar_pagina_desde_storage(char* file_name, char* tag, int page_number);
bool escribir_pagina_en_storage(t_page* page);
t_page* seleccionar_victima(void);
void algoritmo_lru_actualizar(t_page* page);
t_page* algoritmo_lru_seleccionar_victima(void);
t_page* algoritmo_clock_seleccionar_victima(void);
void simular_retardo_memoria(void);
void flush_todas_las_paginas_modificadas(void);

bool enviar_create_storage(char* file_name, char* tag);
bool enviar_truncate_storage(char* file_name, char* tag, int size);
bool enviar_tag_storage(char* src_file, char* src_tag, char* dest_file, char* dest_tag);
bool enviar_commit_storage(char* file_name, char* tag);
bool enviar_delete_storage(char* file_name, char* tag);
bool enviar_write_block_storage(char* file_name, char* tag, int block_number, void* data);
void* recibir_read_block_storage(char* file_name, char* tag, int block_number);

void enviar_resultado_read_master(char* file_name, char* tag, void* data, int size);
void manejar_comunicacion_master(void);

// Utilidades
char* crear_file_tag_key(char* file_name, char* tag);
t_file_tag crear_file_tag(char* file_name, char* tag);
int calcular_numero_pagina(int address);
int calcular_offset_pagina(int address);
int asignar_marco_libre(void);
void liberar_marco(int frame_number);
int calcular_direccion_fisica(int frame_number, int offset);

#endif