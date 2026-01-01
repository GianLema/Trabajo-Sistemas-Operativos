#include "utils.h"


// ---------------------------- SERVIDOR  Y CLIENTE ---------------------------------
int iniciar_servidor(char* puerto,t_log *aux_log ,char* msj_server)
{

	int socket_servidor;

	struct addrinfo hints, *servinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(NULL, puerto, &hints, &servinfo);

	// Creamos el socket de escucha del servidor
	socket_servidor= socket(servinfo->ai_family,
                    servinfo->ai_socktype,
                    servinfo->ai_protocol);

	if (setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
    herror("setsockopt(SO_REUSEADDR) failed");

	// Asociamos el socket a un puerto
	bind(socket_servidor, servinfo->ai_addr, servinfo->ai_addrlen);

	//printf(" MENSAJE LLEGO AL IF \n");

	// Escuchamos las conexiones entrantes
	listen(socket_servidor, SOMAXCONN);
	freeaddrinfo(servinfo);
	log_trace(aux_log, "Server: %s",msj_server);

	return socket_servidor;
}

int esperar_cliente(int socket_servidor, t_log *aux_log, char* msj_server)
{
	// Aceptamos un nuevo cliente
	setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
	int socket_cliente = accept(socket_servidor, NULL, NULL);
	log_info(aux_log,"%s", msj_server);
	return socket_cliente;
}

int crear_conexion(char *ip, char* puerto)
{
	struct addrinfo hints;
	struct addrinfo *server_info;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(ip, puerto, &hints, &server_info);

	// Ahora vamos a crear el socket.
	int socket_cliente = socket(server_info->ai_family,
                    server_info->ai_socktype,
                    server_info->ai_protocol);

	// Ahora que tenemos el socket, vamos a conectarlo

	setsockopt(socket_cliente, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

	if(connect(socket_cliente, server_info->ai_addr, server_info->ai_addrlen) < 0 )
	{
		printf( " Salio del programa \n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(server_info);

	return socket_cliente;
}

void liberar_conexion(int socket_cliente)
{
	close(socket_cliente);
}
// ---------------------------------------------------------------------------

// --------------------------- ENVIAR Y RECIBIR DATOS, OPERACION ----------
int recibir_operacion(int socket_cliente)
{
	int cod_op;
	if(socket_cliente < 0){
		perror("Error al recibir el codigo de operacion");
		exit(EXIT_FAILURE);
	}
    ssize_t bytes_recibidos = recv(socket_cliente, &cod_op, sizeof(int), MSG_WAITALL);
	//printf("bytes_recibidos: %d \n", bytes_recibidos);
    if (bytes_recibidos > 0) {
        return cod_op;
    } else if (bytes_recibidos == 0) {
        printf("El cliente cerró la conexión. \n");
    } else {
        printf("%s",strerror(errno));
    }
    close(socket_cliente);
    return -1;
}

// ------------------------------ CREAR PAQUETE, SERIALIZAR ----------------- 

void* serializar_paquete(t_paquete* paquete, int bytes)
{
	void * magic = malloc(bytes);
	int desplazamiento = 0;

	memcpy(magic + desplazamiento, &(paquete->codigo_operacion), sizeof(int));
	desplazamiento+= sizeof(int);
	memcpy(magic + desplazamiento, &(paquete->buffer->size), sizeof(int));
	desplazamiento+= sizeof(int);
	memcpy(magic + desplazamiento, paquete->buffer->stream, paquete->buffer->size);
	desplazamiento+= paquete->buffer->size;

	return magic;
}

t_paquete* crear_paquete(op_code codigo)
{
	t_paquete* paquete = malloc(sizeof(t_paquete));
	paquete->codigo_operacion = codigo;
	crear_buffer(paquete);
	return paquete;
}

void agregar_a_paquete(t_paquete* paquete, void* valor, int tamanio)
{
	paquete->buffer->stream = realloc(paquete->buffer->stream, paquete->buffer->size + tamanio + sizeof(int));

	memcpy(paquete->buffer->stream + paquete->buffer->size, &tamanio, sizeof(int));
	memcpy(paquete->buffer->stream + paquete->buffer->size + sizeof(int), valor, tamanio);

	paquete->buffer->size += tamanio + sizeof(int);
}

void enviar_paquete(t_paquete* paquete, int socket_cliente)
{
	if (paquete == NULL || paquete->buffer == NULL) {
		printf("Paquete o buffer es NULL");
		return;
	}
	if (socket <= 0) {
		printf("Socket inválido: %d", socket_cliente);
		return;
	}
	int bytes = paquete->buffer->size + 2*sizeof(int);
	void* a_enviar = serializar_paquete(paquete, bytes);

	send(socket_cliente, a_enviar, bytes, 0);

	free(a_enviar);
}

void eliminar_paquete(t_paquete* paquete)
{	
	//eliminar_buffer(paquete->buffer);
	free(paquete);
}

t_paquete* crear_super_paquete(op_code codigo, t_buffer* buff)
{
	t_paquete* paquete = malloc(sizeof(t_paquete));
    if (paquete == NULL) return NULL;
    paquete->codigo_operacion = codigo;
    paquete->buffer = buff;  // Asegúrate de que buffer no sea NULL
    return paquete;
}



// --------------------      LOGS Y CONFIGS ------------------------
t_log* iniciar_logger(char* ruta_Log, char* nombre, t_log_level log_level) 
{
	t_log* nuevo_logger;

	if((nuevo_logger = log_create(ruta_Log,nombre,true,log_level)) == NULL){
		printf("No se puede crear el logger\n");
		exit(1);
	}

	return nuevo_logger;
}

t_config* iniciar_config(char* ruta_config)
{
	t_config* nuevo_config;

	if((nuevo_config = config_create(ruta_config)) == NULL){
		printf("No se pudo encontrar la config\n");
		exit(2);
	}

	return nuevo_config;
}

void eliminar_config(t_config* config) {
    if (config != NULL) {
        config_destroy(config); // Libera la memoria de la configuración
    }
}

// ---------------------------------------------------------------------

void terminar_programa(int conexion, t_log* logger, t_config* config)
{
	/* Y por ultimo, hay que liberar lo que utilizamos (conexion, log y config) 
	  con las funciones de las commons y del TP mencionadas en el enunciado */
		if(logger != NULL){
		log_destroy(logger);
	  }
		if(config != NULL){
		config_destroy(config);
	  }
	  liberar_conexion(conexion);
}
// -----------------------------  BUFFERS -----------------------------------

t_buffer* crear_buffer()
{
	t_buffer* buffer = malloc(sizeof(t_buffer));
	buffer->size = 0;
	buffer->stream = NULL;
	return buffer;
}

void eliminar_buffer(t_buffer* buffer){

	while(buffer->stream!=NULL){
		free(buffer->stream);
	}
	if(buffer!= NULL){
		free(buffer);
	}
	
}

void agregar_al_buffer(t_buffer* buff, void* stream, int tamanio)
{
    if (buff->size == 0) {
        buff->stream = malloc(sizeof(int) + tamanio);
        if (buff->stream == NULL) {
            // Manejo de error: malloc falló
            perror("Error al asignar memoria con malloc");
            exit(EXIT_FAILURE);
        }
        memcpy(buff->stream, &tamanio, sizeof(int));
        memcpy(buff->stream + sizeof(int), stream, tamanio);
    } else {
        void* temp = realloc(buff->stream, buff->size + tamanio + sizeof(int));
        if (temp == NULL) {
            // Manejo de error: realloc falló
            perror("Error al asignar memoria con realloc");
            exit(EXIT_FAILURE);
        }
        buff->stream = temp;
        memcpy(buff->stream + buff->size, &tamanio, sizeof(int));
        memcpy(buff->stream + buff->size + sizeof(int), stream, tamanio);
    }
    buff->size += sizeof(int);
    buff->size += tamanio;

}

void cargar_int_al_buff(t_buffer* buff, int tamanio)
{
	agregar_al_buffer(buff, &tamanio, sizeof(int));
}
void cargar_uint32_t_al_buff(t_buffer* buff, uint32_t tamanio)
{
	agregar_al_buffer(buff, &tamanio, sizeof(uint32_t));
}
void cargar_void_al_buff(t_buffer* buff, void* tamanio)
{
	agregar_al_buffer(buff, tamanio, sizeof(tamanio));
}
void cargar_uint8_t_al_buff(t_buffer* buff, uint8_t tamanio)
{
	agregar_al_buffer(buff, &tamanio, sizeof(uint8_t));
}
void cargar_string_al_buff(t_buffer* buff, char* tamanio)
{
	agregar_al_buffer(buff, tamanio, strlen(tamanio)+1);
}
t_buffer* recibir_buffer(int socket_cliente)
{
	t_buffer* buffer = malloc(sizeof(t_buffer));
	if(	recv(socket_cliente, &(buffer->size), sizeof(int), MSG_WAITALL)>0){
		buffer->stream = malloc(buffer->size);
		if(recv(socket_cliente, buffer->stream, buffer->size, MSG_WAITALL)>0){
			return buffer;
		}else{
			printf("Error al recibir el void stream del buffer del socket cliente");
			exit(EXIT_FAILURE);
		}
	}else{
		printf("Error al recibir el tamaño del buffer del socket cliente");
		exit(EXIT_FAILURE);		
	}
	return buffer;
}
void* extraer_buffer(t_buffer* buff) {
    if (buff->size <= 0) {
        perror("ERROR al intentar extraer un contenido de t_buffer vacío o con tamaño negativo");
        exit(EXIT_FAILURE);
    }

    int tamanio;
    memcpy(&tamanio, buff->stream, sizeof(int));

    if (tamanio > buff->size - (int)sizeof(int)) {
        perror("ERROR: el tamaño a extraer es mayor que el tamaño del buffer");
        exit(EXIT_FAILURE);
    }

    void* caja = malloc(tamanio);
    if (caja == NULL) {
        perror("ERROR al asignar memoria para extraer el contenido del buffer");
        exit(EXIT_FAILURE);
    }

    memcpy(caja, (char*)buff->stream + sizeof(int), tamanio);

    int nuevo_tamanio = buff->size - sizeof(int) - tamanio;
    if (nuevo_tamanio < 0) {
        perror("ERROR: nuevo tamaño negativo");
        exit(EXIT_FAILURE);
    }

    if (nuevo_tamanio > 0) {
        void* nuevo_stream = malloc(nuevo_tamanio);
        if (nuevo_stream == NULL) {
            perror("ERROR al asignar memoria para nuevo stream");
            exit(EXIT_FAILURE);
        }
        memcpy(nuevo_stream, (char*)buff->stream + sizeof(int) + tamanio, nuevo_tamanio);
        free(buff->stream);
        buff->stream = nuevo_stream;
    } else {
        free(buff->stream);
        buff->stream = NULL;
    }

    buff->size = nuevo_tamanio;

    return caja;
}


char* extraer_string_buffer(t_buffer* buff){
	char* un_string = extraer_buffer(buff);
	return un_string;
}
uint32_t extraer_uint32_t_buffer(t_buffer* buff){
	uint32_t* valor = extraer_buffer(buff);
	uint32_t valor_retorno = *valor;
	free(valor);
	return valor_retorno;
}
uint8_t extraer_uint8_t_buffer(t_buffer* buff){
	uint8_t* valor = extraer_buffer(buff);
	uint8_t valor_retorno = *valor;
	free(valor);
	return valor_retorno;
}
int extraer_int_buffer(t_buffer* buff)
{
	int* valor = extraer_buffer(buff);
	int valor_retorno = *valor;
	free(valor);
	return valor_retorno;
}

char* op_code_to_string(op_code code) {
    switch (code) {
        default:
            return "INVALID_OP_CODE";
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* array_to_string(char** array) {
    if (array == NULL) {
        return strdup("{}");
    }

    // Calcular el tamaño necesario para el string final
    size_t total_length = 2; // Para los '{' y '}'
    for (int i = 0; array[i] != NULL; i++) {
        total_length += strlen(array[i]) + 1; // Longitud del string + 1 para la coma
    }

    // Reservar memoria para el string final
    char* result = malloc(total_length);
    if (result == NULL) {
        perror("Error al asignar memoria");
        exit(EXIT_FAILURE);
    }

    // Construir el string
    strcpy(result, "[");
    for (int i = 0; array[i] != NULL; i++) {
        strcat(result, array[i]);
        if (array[i + 1] != NULL) {
            strcat(result, ",");
        }
    }
    strcat(result, "]");

    return result;
}
char* list_to_string(t_list* lista) {
    if (lista == NULL || list_is_empty(lista)) {
        return strdup("[]"); // Retorna una lista vacía si la lista es NULL o está vacía
    }

    // Crear el string inicial con el corchete de apertura
    char* result = string_new();
    string_append(&result, "[");

    // Recorrer la lista y agregar cada elemento al string
    for (int i = 0; i < list_size(lista); i++) {
        char* elemento = list_get(lista, i);
        string_append(&result, elemento);

        // Agregar una coma si no es el último elemento
        if (i < list_size(lista) - 1) {
            string_append(&result, ",");
        }
    }

    // Agregar el corchete de cierre
    string_append(&result, "]");

    return result; // Retorna el string resultante
}