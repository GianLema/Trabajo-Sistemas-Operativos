#ifndef UTILS_H
#define UTILS_H
#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<unistd.h>
#include<netdb.h>
#include<commons/log.h>
#include<commons/collections/list.h>
#include<commons/collections/dictionary.h>
#include<commons/memory.h>
#include<commons/string.h>
#include<commons/bitarray.h>
#include<string.h>
#include<assert.h>
#include<commons/config.h>
// #include<readline/readline.h>
#include<pthread.h>
#include<commons/collections/queue.h>
#include<semaphore.h>
#include<errno.h>
#include<math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>

typedef enum
{
    // MASTER - QUERY CONTROL
    QC_MSG_READ,
    QC_MSG_FIN,
    PLANIFICAR_QUERY,
    HANDSHAKE_MASTER_QUERY_CONTROL,
    // MASTER - WORKER
    WORKER_FIN_QUERY,
    WORKER_ENVIA_LECTURA,
    WORKER_DESALOJAR_QUERY,
    HANDSHAKE_MASTER_WORKER,
    MASTER_INICIA_QUERY_EN_WORKER,
    // .....
    HANDSHAKE_STORAGE_WORKER,
    CREACION_FILE,
    TRUNCADO_ARCHIVO,
    TAG_FILE,
    COMMIT_TAG,
    ESCRITURA_BLOQUE,
    LECTURA_BLOQUE,
    ELIMINAR_TAG
} op_code;


typedef struct{
    op_code nombre;
    int direccion_logica; 
    int pc_nuevo;
    char* datos;
    int tamano_datos;
    char* dispositivo;
    int tiempo_IO;
    char* archivo_instrucciones;
    int tamano_archivo_instrucciones;
} instruccion_t;

typedef struct
{
	int size;
	void* stream;
} t_buffer;

typedef struct
{
	op_code codigo_operacion;
	t_buffer* buffer;
} t_paquete;



// ---------------------------- SERVIDOR  Y CLIENTE ---------------------------------
int iniciar_servidor(char*, t_log*, char*);
int esperar_cliente(int,t_log*,char*);
int crear_conexion(char* ip, char* puerto);
void liberar_conexion(int socket_cliente);

//------------------MENSAJE-------------------//
void enviar_mensaje(char* mensaje, int socket_cliente);


// -------------------- ENVIAR Y RECIBIR DATOS, OPERACION ----------
int recibir_operacion(int);


//----------- crear PAQUETE, SERIALIZAR -------------- 
t_paquete* crear_super_paquete(op_code codigo, t_buffer* buff);
t_paquete* crear_paquete(op_code);
void* serializar_paquete(t_paquete* paquete, int bytes);
void agregar_a_paquete(t_paquete* paquete, void* valor, int tamanio);
void enviar_paquete(t_paquete* paquete, int socket_cliente);
void eliminar_paquete(t_paquete* paquete);



// ---------- LOGS Y CONFIGS ---------------
t_config* iniciar_config(char* ruta_config);
t_log* iniciar_logger(char* ruta_Log, char* nombre, t_log_level log_level) ;



//-------------- BUFFERS --------------------
t_buffer* crear_buffer();
void eliminar_buffer(t_buffer* buffer);
void agregar_al_buffer(t_buffer* buff, void* stream, int tamanio);
void cargar_int_al_buff(t_buffer* buff, int tamanio);
void cargar_uint32_t_al_buff(t_buffer* buff, uint32_t tamanio);
void cargar_uint8_t_al_buff(t_buffer* buff, uint8_t tamanio);
void cargar_string_al_buff(t_buffer* buff, char* tamanio);
void cargar_void_al_buff(t_buffer* buff, void* tamanio);
void* extraer_buffer(t_buffer* buff);
int extraer_int_buffer(t_buffer* buff);
char* extraer_string_buffer(t_buffer* buff);
uint32_t extraer_uint32_t_buffer(t_buffer* buff);
uint8_t extraer_uint8_t_buffer(t_buffer* buff);
t_buffer* recibir_buffer(int socket_cliente);

// OTRAS FUNCIONES
char* op_code_to_string(op_code code);
char* array_to_string(char** array);
char* list_to_string(t_list* lista) ;
char** list_to_array(t_list* lista);
#endif