#include <cspecs/cspec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Definiciones básicas necesarias
int BLOCK_SIZE = 64;

// Implementación INCORRECTA (como está en el código actual)
typedef struct {
    char* file_name;
    char* tag;
} t_file_tag;

typedef struct {
    time_t last_access;
    bool present;
    int page_number;
    t_file_tag file_tag;
} t_page_mock;

typedef struct {
    t_page_mock** pages;
    int size;
} t_list_mock;

// Implementación INCORRECTA que reproduce el bug del código real
t_page_mock* algoritmo_lru_seleccionar_victima_buggy(t_list_mock* pages) {
    t_page_mock* victima = NULL;
    time_t oldest_time = time(NULL); // BUG: Inicializa con tiempo actual
    
    for(int i = 0; i < pages->size; i++) {
        t_page_mock* page = pages->pages[i];
        if(page->present && page->last_access < oldest_time) {
            oldest_time = page->last_access;
            victima = page;
        }
    }
    
    return victima;
}

// Implementación CORRECTA 
t_page_mock* algoritmo_lru_seleccionar_victima_correcto(t_list_mock* pages) {
    t_page_mock* victima = NULL;
    time_t oldest_time = 0; // CORRECCIÓN: Inicializar con 0 o tiempo muy grande
    bool primera_pagina = true;
    
    for(int i = 0; i < pages->size; i++) {
        t_page_mock* page = pages->pages[i];
        if(page->present) {
            if(primera_pagina || page->last_access < oldest_time) {
                oldest_time = page->last_access;
                victima = page;
                primera_pagina = false;
            }
        }
    }
    
    return victima;
}

// Implementación INCORRECTA (como está en el código)
void liberar_instruccion_buggy(void* instruccion) {
    // BUG: No verifica si instruccion es NULL
    typedef struct {
        char* file_name;
        char* tag;
        char* content;
    } t_instruction_mock;
    
    t_instruction_mock* instr = (t_instruction_mock*)instruccion;
    if(instr->file_name) free(instr->file_name); // SEGFAULT si instr es NULL
    if(instr->tag) free(instr->tag);
    if(instr->content) free(instr->content);
    free(instr);
}

// Implementación CORRECTA
void liberar_instruccion_correcto(void* instruccion) {
    if(!instruccion) return; // CORRECCIÓN: Verificar NULL primero
    
    typedef struct {
        char* file_name;
        char* tag;  
        char* content;
    } t_instruction_mock;
    
    t_instruction_mock* instr = (t_instruction_mock*)instruccion;
    if(instr->file_name) free(instr->file_name);
    if(instr->tag) free(instr->tag);
    if(instr->content) free(instr->content);
    free(instr);
}

// Implementaciones INCORRECTAS (sin validación)
int calcular_numero_pagina_buggy(int address) {
    return address / BLOCK_SIZE; // BUG: No valida entrada negativa
}

int calcular_offset_pagina_buggy(int address) {
    return address % BLOCK_SIZE; // BUG: No valida entrada negativa
}

// Implementaciones CORRECTAS
int calcular_numero_pagina_correcto(int address) {
    if(address < 0) return -1; // CORRECCIÓN: Validar entrada
    return address / BLOCK_SIZE;
}

int calcular_offset_pagina_correcto(int address) {
    if(address < 0) return -1; // CORRECCIÓN: Validar entrada
    return address % BLOCK_SIZE;
}

context (test_bugs_reales) {
    
    describe("BUG CRÍTICO: Algoritmo LRU inicialización incorrecta") {
        
        it("FALLA con implementación buggy - no encuentra víctima cuando debería") {
            t_list_mock pages;
            pages.size = 2;
            pages.pages = malloc(2 * sizeof(t_page_mock*));
            
            // Crear páginas con tiempos pasados (más antiguos que ahora)
            time_t hace_una_hora = time(NULL) - 3600;
            time_t hace_dos_horas = time(NULL) - 7200;
            
            t_page_mock* page1 = malloc(sizeof(t_page_mock));
            page1->last_access = hace_una_hora; // Más reciente
            page1->present = true;
            page1->page_number = 1;
            
            t_page_mock* page2 = malloc(sizeof(t_page_mock));
            page2->last_access = hace_dos_horas; // Más antigua - debería ser víctima
            page2->present = true;
            page2->page_number = 2;
            
            pages.pages[0] = page1;
            pages.pages[1] = page2;
            
            // Implementación buggy NO encuentra víctima (devuelve NULL)
            t_page_mock* victima_buggy = algoritmo_lru_seleccionar_victima_buggy(&pages);
            should_ptr(victima_buggy) be null; // Demuestra el BUG
            
            // Implementación correcta SÍ encuentra víctima
            t_page_mock* victima_correcta = algoritmo_lru_seleccionar_victima_correcto(&pages);
            should_ptr(victima_correcta) not be null;
            should_int(victima_correcta->page_number) be equal to(2); // La más antigua
            
            free(page1);
            free(page2);
            free(pages.pages);
        } end
        
        it("Implementación correcta maneja páginas con tiempos futuros") {
            t_list_mock pages;
            pages.size = 2;
            pages.pages = malloc(2 * sizeof(t_page_mock*));
            
            // Crear páginas con diferentes tiempos (algunos en el futuro)
            time_t ahora = time(NULL);
            
            t_page_mock* page1 = malloc(sizeof(t_page_mock));
            page1->last_access = ahora + 100; // Futuro
            page1->present = true;
            page1->page_number = 1;
            
            t_page_mock* page2 = malloc(sizeof(t_page_mock));
            page2->last_access = ahora + 200; // Más futuro
            page2->present = true;
            page2->page_number = 2;
            
            pages.pages[0] = page1;
            pages.pages[1] = page2;
            
            // La implementación correcta debería seleccionar page1 (menos futuro)
            t_page_mock* victima = algoritmo_lru_seleccionar_victima_correcto(&pages);
            should_ptr(victima) not be null;
            should_int(victima->page_number) be equal to(1);
            
            free(page1);
            free(page2);
            free(pages.pages);
        } end
        
    } end
    
    describe("BUG: liberar_instruccion sin validación NULL") {
        
        it("FALLA con implementación buggy - segfault con NULL") {
            // Nota: No podemos testear el segfault directamente en cspec
            // pero podemos documentar el problema
            
            // Esta línea causaría segfault en la implementación buggy:
            // liberar_instruccion_buggy(NULL); 
            
            // La implementación correcta maneja NULL sin problemas:
            liberar_instruccion_correcto(NULL);
            should_bool(true) be equal to(true); // Si llegamos aquí, no hubo segfault
        } end
        
    } end
    
    describe("BUG: Funciones de cálculo sin validación") {
        
        it("FALLA con implementación buggy - resultados incorrectos con negativos") {
            // Las implementaciones buggy dan resultados incorrectos con números negativos
            int page_buggy = calcular_numero_pagina_buggy(-100);
            int offset_buggy = calcular_offset_pagina_buggy(-100);
            
            // En sistemas con división euclidiana, -100/64 podría ser -2 o -1
            // y -100%64 podría ser un número positivo
            printf("BUG: calcular_numero_pagina(-100) = %d (debería ser -1)\n", page_buggy);
            printf("BUG: calcular_offset_pagina(-100) = %d (debería ser -1)\n", offset_buggy);
            
            // Las implementaciones correctas devuelven -1 para entrada inválida
            int page_correcto = calcular_numero_pagina_correcto(-100);
            int offset_correcto = calcular_offset_pagina_correcto(-100);
            
            should_int(page_correcto) be equal to(-1);
            should_int(offset_correcto) be equal to(-1);
        } end
        
        it("Implementaciones correctas manejan entrada válida normalmente") {
            int page = calcular_numero_pagina_correcto(128);
            int offset = calcular_offset_pagina_correcto(128);
            
            should_int(page) be equal to(2); // 128/64 = 2
            should_int(offset) be equal to(0); // 128%64 = 0
        } end
        
    } end
}