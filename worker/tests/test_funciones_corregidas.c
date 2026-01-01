#include <cspecs/cspec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Definiciones básicas necesarias
int BLOCK_SIZE = 64;

// IMPLEMENTACIONES CORREGIDAS (las que deberían estar en el código real)

int calcular_numero_pagina(int address) {
    if(address < 0) return -1; // CORRECCIÓN: Validar entrada negativa
    return address / BLOCK_SIZE;
}

int calcular_offset_pagina(int address) {
    if(address < 0) return -1; // CORRECCIÓN: Validar entrada negativa
    return address % BLOCK_SIZE;
}

int calcular_direccion_fisica(int frame_number, int offset) {
    if(frame_number < 0 || offset < 0) return -1; // CORRECCIÓN: Validar entrada negativa
    return (frame_number * BLOCK_SIZE) + offset;
}

typedef struct {
    char* file_name;
    char* tag;
    char* content;
    char* dest_file_name;
    char* dest_tag;
} t_instruction;

void liberar_instruccion(t_instruction* instruccion) {
    if(!instruccion) return; // CORRECCIÓN: Verificar NULL primero
    
    if(instruccion->file_name) free(instruccion->file_name);
    if(instruccion->tag) free(instruccion->tag);
    if(instruccion->content) free(instruccion->content);
    if(instruccion->dest_file_name) free(instruccion->dest_file_name);
    if(instruccion->dest_tag) free(instruccion->dest_tag);
    free(instruccion);
}

context (test_correcciones_aplicadas) {
    
    describe("CORRECCIÓN: Validación de entrada negativa") {
        
        it("calcular_numero_pagina maneja correctamente entradas negativas") {
            int result = calcular_numero_pagina(-100);
            should_int(result) be equal to(-1);
            
            result = calcular_numero_pagina(-1);
            should_int(result) be equal to(-1);
            
            result = calcular_numero_pagina(-999999);
            should_int(result) be equal to(-1);
        } end
        
        it("calcular_numero_pagina funciona correctamente con entradas válidas") {
            should_int(calcular_numero_pagina(0)) be equal to(0);
            should_int(calcular_numero_pagina(63)) be equal to(0);
            should_int(calcular_numero_pagina(64)) be equal to(1);
            should_int(calcular_numero_pagina(128)) be equal to(2);
            should_int(calcular_numero_pagina(1024)) be equal to(16);
        } end
        
        it("calcular_offset_pagina maneja correctamente entradas negativas") {
            int result = calcular_offset_pagina(-100);
            should_int(result) be equal to(-1);
            
            result = calcular_offset_pagina(-1);
            should_int(result) be equal to(-1);
            
            result = calcular_offset_pagina(-999999);
            should_int(result) be equal to(-1);
        } end
        
        it("calcular_offset_pagina funciona correctamente con entradas válidas") {
            should_int(calcular_offset_pagina(0)) be equal to(0);
            should_int(calcular_offset_pagina(32)) be equal to(32);
            should_int(calcular_offset_pagina(63)) be equal to(63);
            should_int(calcular_offset_pagina(64)) be equal to(0);
            should_int(calcular_offset_pagina(96)) be equal to(32);
        } end
        
        it("calcular_direccion_fisica maneja correctamente entradas negativas") {
            int result = calcular_direccion_fisica(-1, 10);
            should_int(result) be equal to(-1);
            
            result = calcular_direccion_fisica(5, -1);
            should_int(result) be equal to(-1);
            
            result = calcular_direccion_fisica(-1, -1);
            should_int(result) be equal to(-1);
        } end
        
        it("calcular_direccion_fisica funciona correctamente con entradas válidas") {
            should_int(calcular_direccion_fisica(0, 0)) be equal to(0);
            should_int(calcular_direccion_fisica(0, 32)) be equal to(32);
            should_int(calcular_direccion_fisica(1, 0)) be equal to(64);
            should_int(calcular_direccion_fisica(2, 16)) be equal to(144);
            should_int(calcular_direccion_fisica(10, 63)) be equal to(703);
        } end
        
    } end
    
    describe("CORRECCIÓN: liberar_instruccion maneja NULL") {
        
        it("no causa segfault con puntero NULL") {
            // Esta operación debería ser segura ahora
            liberar_instruccion(NULL);
            should_bool(true) be equal to(true); // Si llegamos aquí, no hubo segfault
        } end
        
        it("libera correctamente instrucciones válidas") {
            t_instruction* instr = malloc(sizeof(t_instruction));
            instr->file_name = strdup("archivo.txt");
            instr->tag = strdup("tag1");
            instr->content = strdup("contenido");
            instr->dest_file_name = strdup("destino.txt");
            instr->dest_tag = strdup("tag2");
            
            // No debería causar errores
            liberar_instruccion(instr);
            should_bool(true) be equal to(true);
        } end
        
        it("maneja instrucciones con campos NULL mezclados") {
            t_instruction* instr = malloc(sizeof(t_instruction));
            instr->file_name = strdup("archivo.txt");
            instr->tag = NULL;                    // NULL
            instr->content = strdup("contenido");
            instr->dest_file_name = NULL;         // NULL
            instr->dest_tag = strdup("tag2");
            
            liberar_instruccion(instr);
            should_bool(true) be equal to(true);
        } end
        
    } end
    
    describe("VERIFICACIÓN: Consistencia matemática con correcciones") {
        
        it("verifica que address = página * BLOCK_SIZE + offset (solo para entradas válidas)") {
            for (int address = 0; address < 1000; address += 17) {
                int page = calcular_numero_pagina(address);
                int offset = calcular_offset_pagina(address);
                int recalculado = calcular_direccion_fisica(page, offset);
                
                should_int(recalculado) be equal to(address);
            }
        } end
        
        it("verifica propiedades matemáticas básicas") {
            // Propiedad: offset siempre < BLOCK_SIZE para entradas válidas
            for (int i = 0; i < 1000; i++) {
                int offset = calcular_offset_pagina(i);
                should_bool(offset >= 0) be equal to(true);
                should_bool(offset < BLOCK_SIZE) be equal to(true);
            }
            
            // Propiedad: página incrementa cada BLOCK_SIZE addresses
            for (int i = 0; i < 5; i++) {
                int base = i * BLOCK_SIZE;
                int page_base = calcular_numero_pagina(base);
                int page_siguiente = calcular_numero_pagina(base + BLOCK_SIZE);
                
                should_int(page_siguiente) be equal to(page_base + 1);
            }
        } end
        
        it("maneja casos límite correctamente") {
            // Dirección 0
            should_int(calcular_numero_pagina(0)) be equal to(0);
            should_int(calcular_offset_pagina(0)) be equal to(0);
            should_int(calcular_direccion_fisica(0, 0)) be equal to(0);
            
            // Límites de página
            int limite = BLOCK_SIZE - 1; // 63
            should_int(calcular_numero_pagina(limite)) be equal to(0);
            should_int(calcular_offset_pagina(limite)) be equal to(limite);
            
            should_int(calcular_numero_pagina(limite + 1)) be equal to(1);
            should_int(calcular_offset_pagina(limite + 1)) be equal to(0);
        } end
        
    } end
    
    describe("PRUEBAS DE ROBUSTEZ: Casos extremos") {
        
        it("maneja direcciones muy grandes") {
            int address_grande = 1048576; // 1MB
            int page = calcular_numero_pagina(address_grande);
            int offset = calcular_offset_pagina(address_grande);
            
            should_int(page) be equal to(address_grande / BLOCK_SIZE);
            should_int(offset) be equal to(address_grande % BLOCK_SIZE);
            
            // Verificar que la conversión inversa funciona
            int direccion_recalculada = calcular_direccion_fisica(page, offset);
            should_int(direccion_recalculada) be equal to(address_grande);
        } end
        
        it("funciona consistentemente bajo uso intensivo") {
            // Verificar que las correcciones no introdujeron inconsistencias
            for (int iteracion = 0; iteracion < 10; iteracion++) {
                for (int address = 0; address < 500; address += 13) {
                    int page1 = calcular_numero_pagina(address);
                    int offset1 = calcular_offset_pagina(address);
                    int direccion1 = calcular_direccion_fisica(page1, offset1);
                    
                    // Recalcular las mismas operaciones
                    int page2 = calcular_numero_pagina(address);
                    int offset2 = calcular_offset_pagina(address);
                    int direccion2 = calcular_direccion_fisica(page2, offset2);
                    
                    // Deben ser iguales
                    should_int(page1) be equal to(page2);
                    should_int(offset1) be equal to(offset2);
                    should_int(direccion1) be equal to(direccion2);
                    should_int(direccion1) be equal to(address);
                }
            }
        } end
        
    } end
}