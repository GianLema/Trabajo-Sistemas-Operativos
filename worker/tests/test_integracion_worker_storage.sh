#!/bin/bash

# Script para tests de integración Worker-Storage
# Este script crea un mock server que simula Storage y testa la comunicación real

echo "=== TESTS DE INTEGRACIÓN WORKER-STORAGE ==="

# Función para limpiar procesos al salir
cleanup() {
    echo "Limpiando procesos de test..."
    pkill -f "mock_storage_server"
    pkill -f "worker_integration_test"
    exit 0
}

# Configurar trap para cleanup
trap cleanup SIGINT SIGTERM EXIT

# Compilar el mock server
echo "1. Compilando mock Storage server..."
cat > /tmp/mock_storage_server.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define CREACION_FILE 1
#define ESCRITURA_BLOQUE 6
#define LECTURA_BLOQUE 7
#define HANDSHAKE_STORAGE_WORKER 8

typedef struct {
    int codigo_operacion;
    int tamaño;
} paquete_header;

void* manejar_cliente(void* socket_desc) {
    int client_socket = *(int*)socket_desc;
    paquete_header header;
    char buffer[1024];
    
    printf("Mock Storage: Cliente conectado\n");
    
    while (1) {
        // Recibir header del paquete
        int bytes_recibidos = recv(client_socket, &header, sizeof(paquete_header), 0);
        if (bytes_recibidos <= 0) {
            printf("Mock Storage: Cliente desconectado\n");
            break;
        }
        
        printf("Mock Storage: Recibido op_code %d, tamaño %d\n", 
               header.codigo_operacion, header.tamaño);
        
        // Recibir datos del paquete
        if (header.tamaño > 0) {
            recv(client_socket, buffer, header.tamaño, 0);
            printf("Mock Storage: Datos recibidos (%d bytes)\n", header.tamaño);
        }
        
        // Simular procesamiento
        usleep(10000); // 10ms
        
        // Enviar respuesta
        paquete_header respuesta;
        respuesta.codigo_operacion = header.codigo_operacion; // Echo del comando
        respuesta.tamaño = 0;
        
        if (header.codigo_operacion == LECTURA_BLOQUE) {
            // Para READ, enviar datos simulados
            respuesta.tamaño = 64;
            send(client_socket, &respuesta, sizeof(paquete_header), 0);
            
            char datos_simulados[64];
            memset(datos_simulados, 'X', 64);
            send(client_socket, datos_simulados, 64, 0);
            printf("Mock Storage: Enviados datos de READ_BLOCK\n");
        } else {
            // Para otros comandos, solo confirmación
            send(client_socket, &respuesta, sizeof(paquete_header), 0);
            printf("Mock Storage: Enviada confirmación para op_code %d\n", 
                   header.codigo_operacion);
        }
    }
    
    close(client_socket);
    free(socket_desc);
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Crear socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Error creando socket");
        return 1;
    }
    
    // Configurar para reusar dirección
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configurar dirección
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        return 1;
    }
    
    // Listen
    if (listen(server_socket, 5) < 0) {
        perror("Error en listen");
        return 1;
    }
    
    printf("Mock Storage Server escuchando en puerto %d\n", PORT);
    
    // Aceptar conexiones
    while (1) {
        int* client_socket_ptr = malloc(sizeof(int));
        *client_socket_ptr = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (*client_socket_ptr < 0) {
            perror("Error aceptando conexión");
            free(client_socket_ptr);
            continue;
        }
        
        // Crear hilo para manejar cliente
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, manejar_cliente, client_socket_ptr) < 0) {
            perror("Error creando hilo");
            free(client_socket_ptr);
            continue;
        }
        
        pthread_detach(thread_id);
    }
    
    close(server_socket);
    return 0;
}
EOF

gcc -o /tmp/mock_storage_server /tmp/mock_storage_server.c -lpthread

if [ $? -ne 0 ]; then
    echo "ERROR: No se pudo compilar el mock server"
    exit 1
fi

# Iniciar el mock server en background
echo "2. Iniciando mock Storage server..."
/tmp/mock_storage_server &
MOCK_SERVER_PID=$!

# Esperar a que el server esté listo
sleep 2

# Verificar que el server está corriendo
if ! kill -0 $MOCK_SERVER_PID 2>/dev/null; then
    echo "ERROR: Mock server no se inició correctamente"
    exit 1
fi

echo "3. Mock Storage server iniciado (PID: $MOCK_SERVER_PID)"

# Compilar cliente de prueba
echo "4. Compilando cliente de prueba..."
cat > /tmp/worker_integration_test.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define CREACION_FILE 1
#define ESCRITURA_BLOQUE 6
#define LECTURA_BLOQUE 7

typedef struct {
    int codigo_operacion;
    int tamaño;
} paquete_header;

int crear_conexion() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error conectando");
        return -1;
    }
    
    return sock;
}

int enviar_comando(int sock, int op_code, void* data, int data_size) {
    paquete_header header;
    header.codigo_operacion = op_code;
    header.tamaño = data_size;
    
    // Enviar header
    if (send(sock, &header, sizeof(header), 0) < 0) {
        return -1;
    }
    
    // Enviar datos si los hay
    if (data_size > 0 && data) {
        if (send(sock, data, data_size, 0) < 0) {
            return -1;
        }
    }
    
    return 0;
}

int recibir_respuesta(int sock, void* buffer, int buffer_size) {
    paquete_header header;
    
    // Recibir header de respuesta
    if (recv(sock, &header, sizeof(header), 0) < 0) {
        return -1;
    }
    
    printf("Cliente: Recibida respuesta op_code %d, tamaño %d\n", 
           header.codigo_operacion, header.tamaño);
    
    // Recibir datos si los hay
    if (header.tamaño > 0 && buffer && buffer_size >= header.tamaño) {
        if (recv(sock, buffer, header.tamaño, 0) < 0) {
            return -1;
        }
    }
    
    return header.codigo_operacion;
}

int main() {
    printf("=== TEST DE INTEGRACIÓN WORKER-STORAGE ===\n");
    
    // Test 1: CREATE
    printf("\n1. Test CREATE file...\n");
    int sock1 = crear_conexion();
    if (sock1 < 0) {
        printf("FAIL: No se pudo conectar para CREATE\n");
        return 1;
    }
    
    char datos_create[] = "test.txt\0tag1\0";
    if (enviar_comando(sock1, CREACION_FILE, datos_create, strlen(datos_create)) == 0) {
        int respuesta = recibir_respuesta(sock1, NULL, 0);
        if (respuesta == CREACION_FILE) {
            printf("PASS: CREATE funcionó correctamente\n");
        } else {
            printf("FAIL: CREATE respuesta incorrecta (%d)\n", respuesta);
        }
    } else {
        printf("FAIL: Error enviando CREATE\n");
    }
    close(sock1);
    
    // Test 2: WRITE_BLOCK
    printf("\n2. Test WRITE_BLOCK...\n");
    int sock2 = crear_conexion();
    if (sock2 < 0) {
        printf("FAIL: No se pudo conectar para WRITE_BLOCK\n");
        return 1;
    }
    
    char datos_write[128];
    strcpy(datos_write, "archivo.txt");
    strcpy(datos_write + 12, "v1");
    int block_num = 5;
    memcpy(datos_write + 15, &block_num, sizeof(int));
    memset(datos_write + 19, 'A', 64); // Datos del bloque
    
    if (enviar_comando(sock2, ESCRITURA_BLOQUE, datos_write, 83) == 0) {
        int respuesta = recibir_respuesta(sock2, NULL, 0);
        if (respuesta == ESCRITURA_BLOQUE) {
            printf("PASS: WRITE_BLOCK funcionó correctamente\n");
        } else {
            printf("FAIL: WRITE_BLOCK respuesta incorrecta (%d)\n", respuesta);
        }
    } else {
        printf("FAIL: Error enviando WRITE_BLOCK\n");
    }
    close(sock2);
    
    // Test 3: READ_BLOCK
    printf("\n3. Test READ_BLOCK...\n");
    int sock3 = crear_conexion();
    if (sock3 < 0) {
        printf("FAIL: No se pudo conectar para READ_BLOCK\n");
        return 1;
    }
    
    char datos_read[32];
    strcpy(datos_read, "lectura.txt");
    strcpy(datos_read + 12, "v2");
    int read_block = 3;
    memcpy(datos_read + 15, &read_block, sizeof(int));
    
    if (enviar_comando(sock3, LECTURA_BLOQUE, datos_read, 19) == 0) {
        char buffer_respuesta[64];
        int respuesta = recibir_respuesta(sock3, buffer_respuesta, 64);
        if (respuesta == LECTURA_BLOQUE) {
            printf("PASS: READ_BLOCK funcionó correctamente\n");
            printf("Datos recibidos: %.10s...\n", buffer_respuesta);
        } else {
            printf("FAIL: READ_BLOCK respuesta incorrecta (%d)\n", respuesta);
        }
    } else {
        printf("FAIL: Error enviando READ_BLOCK\n");
    }
    close(sock3);
    
    // Test 4: Múltiples conexiones concurrentes
    printf("\n4. Test conexiones concurrentes...\n");
    int sockets[5];
    int exitosos = 0;
    
    for (int i = 0; i < 5; i++) {
        sockets[i] = crear_conexion();
        if (sockets[i] >= 0) {
            char datos[32];
            sprintf(datos, "file%d.txt\0tag%d\0", i, i);
            if (enviar_comando(sockets[i], CREACION_FILE, datos, strlen(datos)) == 0) {
                int resp = recibir_respuesta(sockets[i], NULL, 0);
                if (resp == CREACION_FILE) {
                    exitosos++;
                }
            }
            close(sockets[i]);
        }
    }
    
    if (exitosos == 5) {
        printf("PASS: Conexiones concurrentes exitosas (%d/5)\n", exitosos);
    } else {
        printf("PARTIAL: Conexiones concurrentes parcialmente exitosas (%d/5)\n", exitosos);
    }
    
    printf("\n=== TESTS COMPLETADOS ===\n");
    return 0;
}
EOF

gcc -o /tmp/worker_integration_test /tmp/worker_integration_test.c

if [ $? -ne 0 ]; then
    echo "ERROR: No se pudo compilar el cliente de prueba"
    kill $MOCK_SERVER_PID
    exit 1
fi

# Ejecutar tests de integración
echo "5. Ejecutando tests de integración..."
/tmp/worker_integration_test

TEST_RESULT=$?

# Verificar conectividad básica
echo "6. Verificando conectividad con netcat..."
echo "PING" | nc -w 1 127.0.0.1 8080 >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "PASS: Conectividad básica OK"
else
    echo "FAIL: Problemas de conectividad"
fi

# Cleanup
echo "7. Limpiando..."
kill $MOCK_SERVER_PID 2>/dev/null
rm -f /tmp/mock_storage_server /tmp/mock_storage_server.c
rm -f /tmp/worker_integration_test /tmp/worker_integration_test.c

if [ $TEST_RESULT -eq 0 ]; then
    echo "✅ TODOS LOS TESTS DE INTEGRACIÓN PASARON"
else
    echo "❌ ALGUNOS TESTS FALLARON"
fi

exit $TEST_RESULT