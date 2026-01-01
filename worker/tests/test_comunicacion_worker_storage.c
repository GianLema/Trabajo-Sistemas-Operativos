 #include <cspecs/cspec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>

// Mock de estructuras necesarias
typedef enum {
    CREACION_FILE = 1,
    TRUNCADO_ARCHIVO = 2,
    TAG_FILE = 3,
    COMMIT_TAG = 4,
    ELIMINAR_TAG = 5,
    ESCRITURA_BLOQUE = 6,
    LECTURA_BLOQUE = 7,
    HANDSHAKE_STORAGE_WORKER = 8
} op_code;

typedef struct {
    void* stream;
    int size;
    int offset;
} t_buffer;

typedef struct {
    op_code codigo_operacion;
    t_buffer* buffer;
} t_paquete;

// Variables globales mock
int fd_storage = -1;
int BLOCK_SIZE = 64;
char mensaje_log[256];

// Mock de funciones de logging
void log_info_mock(void* logger, char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(mensaje_log, sizeof(mensaje_log), format, args);
    va_end(args);
    printf("LOG_INFO: %s\n", mensaje_log);
}

void log_error_mock(void* logger, char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(mensaje_log, sizeof(mensaje_log), format, args);
    va_end(args);
    printf("LOG_ERROR: %s\n", mensaje_log);
}

// Mock de funciones de comunicación
t_buffer* crear_buffer() {
    t_buffer* buffer = malloc(sizeof(t_buffer));
    buffer->size = 0;
    buffer->offset = 0;
    buffer->stream = malloc(1024); // Buffer inicial
    return buffer;
}

void cargar_string_al_buff(t_buffer* buffer, char* string) {
    int len = strlen(string) + 1;
    memcpy(buffer->stream + buffer->size, &len, sizeof(int));
    buffer->size += sizeof(int);
    memcpy(buffer->stream + buffer->size, string, len);
    buffer->size += len;
}

void cargar_int_al_buff(t_buffer* buffer, int number) {
    memcpy(buffer->stream + buffer->size, &number, sizeof(int));
    buffer->size += sizeof(int);
}

void cargar_void_al_buff(t_buffer* buffer, void* data) {
    // Simplificado: copiar BLOCK_SIZE bytes
    memcpy(buffer->stream + buffer->size, data, BLOCK_SIZE);
    buffer->size += BLOCK_SIZE;
}

t_paquete* crear_super_paquete(op_code codigo, t_buffer* buffer) {
    t_paquete* paquete = malloc(sizeof(t_paquete));
    paquete->codigo_operacion = codigo;
    paquete->buffer = buffer;
    return paquete;
}

void eliminar_paquete(t_paquete* paquete) {
    if (paquete->buffer) {
        if (paquete->buffer->stream) free(paquete->buffer->stream);
        free(paquete->buffer);
    }
    free(paquete);
}

// Variables para simular respuestas del storage
op_code respuesta_esperada = CREACION_FILE;
bool simular_error = false;

int enviar_paquete_mock(t_paquete* paquete, int socket_fd) {
    printf("MOCK: Enviando paquete con op_code %d al socket %d\n", paquete->codigo_operacion, socket_fd);
    printf("MOCK: Tamaño del buffer: %d bytes\n", paquete->buffer->size);
    return paquete->buffer->size;
}

op_code recibir_operacion_mock(int socket_fd) {
    if (simular_error) {
        return -1; // Simular error de conexión
    }
    printf("MOCK: Recibiendo respuesta del storage: op_code %d\n", respuesta_esperada);
    return respuesta_esperada;
}

t_buffer* recibir_buffer_mock(int socket_fd) {
    t_buffer* buffer = crear_buffer();
    // Simular datos recibidos
    char* data = "datos_simulados_del_storage";
    memcpy(buffer->stream, data, strlen(data) + 1);
    buffer->size = strlen(data) + 1;
    return buffer;
}

char* extraer_string_buffer(t_buffer* buffer) {
    int len;
    memcpy(&len, buffer->stream + buffer->offset, sizeof(int));
    buffer->offset += sizeof(int);
    
    char* string = malloc(len);
    memcpy(string, buffer->stream + buffer->offset, len);
    buffer->offset += len;
    
    return string;
}

int extraer_int_buffer(t_buffer* buffer) {
    int number;
    memcpy(&number, buffer->stream + buffer->offset, sizeof(int));
    buffer->offset += sizeof(int);
    return number;
}

void* extraer_buffer(t_buffer* buffer) {
    void* data = malloc(BLOCK_SIZE);
    memcpy(data, buffer->stream + buffer->offset, BLOCK_SIZE);
    buffer->offset += BLOCK_SIZE;
    return data;
}

// Reemplazar las funciones originales con mocks
#define enviar_paquete enviar_paquete_mock
#define recibir_operacion recibir_operacion_mock
#define recibir_buffer recibir_buffer_mock
#define log_info log_info_mock
#define log_error log_error_mock
#define log_worker NULL

// IMPLEMENTACIONES ORIGINALES PARA TESTEAR
void enviar_create_storage(char* file_name, char* tag) {
    t_buffer* buffer = crear_buffer();
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    
    t_paquete* paquete = crear_super_paquete(CREACION_FILE, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    // Recibir confirmacion del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == CREACION_FILE) {
        log_info(log_worker, "CREATE confirmado por storage para %s:%s", file_name, tag);
    } else {
        log_error(log_worker, "Error en CREATE para %s:%s", file_name, tag);
    }
}

void enviar_write_block_storage(char* file_name, char* tag, int block_number, void* data) {
    t_buffer* buffer = crear_buffer();
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    cargar_int_al_buff(buffer, block_number);
    cargar_void_al_buff(buffer, data);
    
    t_paquete* paquete = crear_super_paquete(ESCRITURA_BLOQUE, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    // Recibir confirmacion del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == ESCRITURA_BLOQUE) {
        log_info(log_worker, "WRITE_BLOCK confirmado por storage para %s:%s bloque %d", file_name, tag, block_number);
    } else {
        log_error(log_worker, "Error en WRITE_BLOCK para %s:%s bloque %d", file_name, tag, block_number);
    }
}

void* recibir_read_block_storage(char* file_name, char* tag, int block_number) {
    t_buffer* buffer = crear_buffer();
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    cargar_int_al_buff(buffer, block_number);
    
    t_paquete* paquete = crear_super_paquete(LECTURA_BLOQUE, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    // Recibir respuesta del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == LECTURA_BLOQUE) {
        t_buffer* buffer_respuesta = recibir_buffer(fd_storage);
        void* data = extraer_buffer(buffer_respuesta);
        log_info(log_worker, "READ_BLOCK recibido desde storage para %s:%s bloque %d", file_name, tag, block_number);
        free(buffer_respuesta);
        return data;
    } else {
        log_error(log_worker, "Error en READ_BLOCK para %s:%s bloque %d", file_name, tag, block_number);
        return NULL;
    }
}

context (test_comunicacion_worker_storage) {
    
    describe("Comunicación básica Worker-Storage") {
        
        before {
            fd_storage = 42; // Mock socket
            simular_error = false;
            memset(mensaje_log, 0, sizeof(mensaje_log));
        } end
        
        it("envía correctamente comando CREATE") {
            respuesta_esperada = CREACION_FILE;
            
            enviar_create_storage("test.txt", "v1");
            
            // Verificar que se loggea correctamente
            should_ptr(strstr(mensaje_log, "CREATE confirmado")) not be null;
            should_ptr(strstr(mensaje_log, "test.txt:v1")) not be null;
        } end
        
        it("maneja errores en comando CREATE") {
            respuesta_esperada = -1; // Error
            
            enviar_create_storage("test.txt", "v1");
            
            // Verificar que se loggea el error
            should_ptr(strstr(mensaje_log, "Error en CREATE")) not be null;
        } end
        
        it("envía correctamente comando WRITE_BLOCK") {
            respuesta_esperada = ESCRITURA_BLOQUE;
            
            char data[64];
            memset(data, 'A', 64);
            
            enviar_write_block_storage("archivo.txt", "v2", 5, data);
            
            should_ptr(strstr(mensaje_log, "WRITE_BLOCK confirmado")) not be null;
            should_ptr(strstr(mensaje_log, "bloque 5")) not be null;
        } end
        
        it("maneja errores en comando WRITE_BLOCK") {
            respuesta_esperada = -1; // Error
            
            char data[64];
            enviar_write_block_storage("archivo.txt", "v2", 5, data);
            
            should_ptr(strstr(mensaje_log, "Error en WRITE_BLOCK")) not be null;
        } end
        
    } end
    
    describe("Comunicación READ_BLOCK Worker-Storage") {
        
        before {
            fd_storage = 42;
            simular_error = false;
            memset(mensaje_log, 0, sizeof(mensaje_log));
        } end
        
        it("recibe correctamente datos con READ_BLOCK") {
            respuesta_esperada = LECTURA_BLOQUE;
            
            void* data = recibir_read_block_storage("lectura.txt", "v3", 2);
            
            should_ptr(data) not be null;
            should_ptr(strstr(mensaje_log, "READ_BLOCK recibido")) not be null;
            should_ptr(strstr(mensaje_log, "bloque 2")) not be null;
            
            free(data);
        } end
        
        it("maneja errores en READ_BLOCK") {
            respuesta_esperada = -1; // Error
            
            void* data = recibir_read_block_storage("lectura.txt", "v3", 2);
            
            should_ptr(data) be null;
            should_ptr(strstr(mensaje_log, "Error en READ_BLOCK")) not be null;
        } end
        
    } end
    
    describe("Pruebas de robustez de comunicación") {
        
        before {
            fd_storage = 42;
            simular_error = false;
        } end
        
        it("maneja nombres de archivo largos") {
            respuesta_esperada = CREACION_FILE;
            
            char archivo_largo[200];
            memset(archivo_largo, 'x', 199);
            archivo_largo[199] = '\0';
            
            enviar_create_storage(archivo_largo, "tag_corto");
            
            should_ptr(strstr(mensaje_log, "CREATE confirmado")) not be null;
        } end
        
        it("maneja caracteres especiales en nombres") {
            respuesta_esperada = CREACION_FILE;
            
            enviar_create_storage("archivo@#$.txt", "tag!%&()");
            
            should_ptr(strstr(mensaje_log, "CREATE confirmado")) not be null;
        } end
        
        it("maneja múltiples operaciones secuenciales") {
            // Simular secuencia: CREATE -> WRITE -> READ
            
            // CREATE
            respuesta_esperada = CREACION_FILE;
            enviar_create_storage("secuencial.txt", "v1");
            should_ptr(strstr(mensaje_log, "CREATE confirmado")) not be null;
            
            // WRITE
            respuesta_esperada = ESCRITURA_BLOQUE;
            char data[64] = "datos_de_prueba";
            enviar_write_block_storage("secuencial.txt", "v1", 0, data);
            should_ptr(strstr(mensaje_log, "WRITE_BLOCK confirmado")) not be null;
            
            // READ
            respuesta_esperada = LECTURA_BLOQUE;
            void* read_data = recibir_read_block_storage("secuencial.txt", "v1", 0);
            should_ptr(read_data) not be null;
            should_ptr(strstr(mensaje_log, "READ_BLOCK recibido")) not be null;
            
            free(read_data);
        } end
        
    } end
    
    describe("Validación de protocolos de comunicación") {
        
        it("verifica formato correcto de paquetes CREATE") {
            // Este test verificaría que el buffer se construya correctamente
            t_buffer* buffer = crear_buffer();
            cargar_string_al_buff(buffer, "test.txt");
            cargar_string_al_buff(buffer, "v1");
            
            // Verificar que se puede extraer correctamente
            char* file_name = extraer_string_buffer(buffer);
            char* tag = extraer_string_buffer(buffer);
            
            should_string(file_name) be equal to("test.txt");
            should_string(tag) be equal to("v1");
            
            free(file_name);
            free(tag);
            free(buffer->stream);
            free(buffer);
        } end
        
        it("verifica formato correcto de paquetes WRITE_BLOCK") {
            t_buffer* buffer = crear_buffer();
            cargar_string_al_buff(buffer, "archivo.txt");
            cargar_string_al_buff(buffer, "v2");
            cargar_int_al_buff(buffer, 123);
            
            char test_data[64] = "datos_de_prueba";
            cargar_void_al_buff(buffer, test_data);
            
            // Verificar extracción
            char* file_name = extraer_string_buffer(buffer);
            char* tag = extraer_string_buffer(buffer);
            int block_number = extraer_int_buffer(buffer);
            void* data = extraer_buffer(buffer);
            
            should_string(file_name) be equal to("archivo.txt");
            should_string(tag) be equal to("v2");
            should_int(block_number) be equal to(123);
            should_string((char*)data) be equal to("datos_de_prueba");
            
            free(file_name);
            free(tag);
            free(data);
            free(buffer->stream);
            free(buffer);
        } end
        
    } end
}