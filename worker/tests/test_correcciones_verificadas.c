#include <cspecs/cspec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

// Variables globales para tests
int BLOCK_SIZE = 64;

// Prototipos de las funciones corregidas
int calcular_numero_pagina(int address);
int calcular_offset_pagina(int address);
int calcular_direccion_fisica(int frame_number, int offset);

typedef struct {
    int type;
    char* file_name;
    char* tag;
    int address;
    int size;
    char* content;
    char* dest_file_name;
    char* dest_tag;
} t_instruction;

void liberar_instruccion(t_instruction* instruccion);

context (test_correcciones_bugs) {
    
    describe("✅ CORRECCIONES APLICADAS - Funciones de cálculo") {
        
        it("calcular_numero_pagina ahora valida entrada negativa") {
            int result = calcular_numero_pagina(-100);
            should_int(result) be equal to(-1);
            
            // Verificar que funciona normalmente con entrada válida
            should_int(calcular_numero_pagina(0)) be equal to(0);
            should_int(calcular_numero_pagina(64)) be equal to(1);
            should_int(calcular_numero_pagina(128)) be equal to(2);
        } end
        
        it("calcular_offset_pagina ahora valida entrada negativa") {
            int result = calcular_offset_pagina(-100);
            should_int(result) be equal to(-1);
            
            // Verificar que funciona normalmente con entrada válida
            should_int(calcular_offset_pagina(0)) be equal to(0);
            should_int(calcular_offset_pagina(32)) be equal to(32);
            should_int(calcular_offset_pagina(127)) be equal to(63); // 127 % 64 = 63
        } end
        
        it("calcular_direccion_fisica ahora valida entrada negativa") {
            should_int(calcular_direccion_fisica(-1, 10)) be equal to(-1);
            should_int(calcular_direccion_fisica(5, -1)) be equal to(-1);
            should_int(calcular_direccion_fisica(-1, -1)) be equal to(-1);
            
            // Verificar que funciona normalmente con entrada válida
            should_int(calcular_direccion_fisica(0, 0)) be equal to(0);
            should_int(calcular_direccion_fisica(1, 32)) be equal to(96); // 1*64 + 32
            should_int(calcular_direccion_fisica(2, 15)) be equal to(143); // 2*64 + 15
        } end
        
    } end
    
    describe("✅ CORRECCIÓN APLICADA - liberar_instruccion") {
        
        it("ahora maneja NULL sin fallar") {
            // Esto no debería causar segfault
            liberar_instruccion(NULL);
            should_bool(true) be equal to(true); // Si llegó aquí, no hubo segfault
        } end
        
        it("sigue funcionando normalmente con instrucciones válidas") {
            t_instruction* instr = malloc(sizeof(t_instruction));
            instr->type = 1;
            instr->file_name = strdup("test.txt");
            instr->tag = strdup("v1");
            instr->address = 100;
            instr->size = 50;
            instr->content = strdup("contenido");
            instr->dest_file_name = strdup("dest.txt");
            instr->dest_tag = strdup("v2");
            
            // Esto no debería causar errores
            liberar_instruccion(instr);
            should_bool(true) be equal to(true);
        } end
        
        it("maneja instrucciones parcialmente inicializadas") {
            t_instruction* instr = malloc(sizeof(t_instruction));
            instr->type = 2;
            instr->file_name = strdup("test.txt");
            instr->tag = NULL; // NULL
            instr->address = 200;
            instr->size = 100;
            instr->content = NULL; // NULL
            instr->dest_file_name = NULL; // NULL
            instr->dest_tag = strdup("tag");
            
            liberar_instruccion(instr);
            should_bool(true) be equal to(true);
        } end
        
    } end
    
    describe("🔍 VERIFICACIÓN - Consistencia matemática después de correcciones") {
        
        it("mantiene consistencia para valores válidos") {
            for (int address = 0; address < 1000; address += 13) {
                int page = calcular_numero_pagina(address);
                int offset = calcular_offset_pagina(address);
                int recalculado = calcular_direccion_fisica(page, offset);
                
                should_int(recalculado) be equal to(address);
            }
        } end
        
        it("maneja casos límite correctamente") {
            // Límite de páginas
            should_int(calcular_numero_pagina(BLOCK_SIZE - 1)) be equal to(0);
            should_int(calcular_numero_pagina(BLOCK_SIZE)) be equal to(1);
            
            // Límite de offsets
            should_int(calcular_offset_pagina(BLOCK_SIZE - 1)) be equal to(BLOCK_SIZE - 1);
            should_int(calcular_offset_pagina(BLOCK_SIZE)) be equal to(0);
            
            // Direcciones físicas límite
            should_int(calcular_direccion_fisica(0, BLOCK_SIZE - 1)) be equal to(BLOCK_SIZE - 1);
            should_int(calcular_direccion_fisica(1, 0)) be equal to(BLOCK_SIZE);
        } end
        
        it("rechaza correctamente valores inválidos") {
            // Todas estas deberían retornar -1
            should_int(calcular_numero_pagina(-1)) be equal to(-1);
            should_int(calcular_numero_pagina(-1000)) be equal to(-1);
            
            should_int(calcular_offset_pagina(-1)) be equal to(-1);
            should_int(calcular_offset_pagina(-500)) be equal to(-1);
            
            should_int(calcular_direccion_fisica(-1, 0)) be equal to(-1);
            should_int(calcular_direccion_fisica(0, -1)) be equal to(-1);
            should_int(calcular_direccion_fisica(-1, -1)) be equal to(-1);
        } end
        
    } end
    
    describe("🧪 PRUEBAS DE REGRESIÓN - Verificar que no rompimos nada") {
        
        it("funciones básicas siguen funcionando como antes") {
            // Casos que definitivamente deberían funcionar
            should_int(calcular_numero_pagina(0)) be equal to(0);
            should_int(calcular_numero_pagina(100)) be equal to(1); // 100/64 = 1
            should_int(calcular_numero_pagina(200)) be equal to(3); // 200/64 = 3
            
            should_int(calcular_offset_pagina(0)) be equal to(0);
            should_int(calcular_offset_pagina(100)) be equal to(36); // 100 % 64 = 36
            should_int(calcular_offset_pagina(200)) be equal to(8);  // 200 % 64 = 8
            
            should_int(calcular_direccion_fisica(0, 0)) be equal to(0);
            should_int(calcular_direccion_fisica(1, 36)) be equal to(100); // 1*64 + 36
            should_int(calcular_direccion_fisica(3, 8)) be equal to(200);  // 3*64 + 8
        } end
        
        it("mantiene propiedades matemáticas fundamentales") {
            // Propiedad: address = (address/BLOCK_SIZE) * BLOCK_SIZE + (address%BLOCK_SIZE)
            for (int addr = 0; addr < 500; addr += 7) {
                int page = calcular_numero_pagina(addr);
                int offset = calcular_offset_pagina(addr);
                int reconstructed = calcular_direccion_fisica(page, offset);
                
                should_int(reconstructed) be equal to(addr);
            }
        } end
        
    } end
}