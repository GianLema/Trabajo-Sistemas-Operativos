#include <cspecs/cspec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

// Definiciones básicas necesarias
int BLOCK_SIZE = 64;
int TAM_MEMORIA = 256;
int current_query_id = 1;

// Funciones básicas para testear
char* crear_file_tag_key(char* file_name, char* tag) {
    if (!file_name || !tag) return NULL;
    int len = strlen(file_name) + strlen(tag) + 2;
    char* key = malloc(len);
    sprintf(key, "%s:%s", file_name, tag);
    return key;
}

int calcular_numero_pagina(int address) {
    if (address < 0) return -1;
    return address / BLOCK_SIZE;
}

int calcular_offset_pagina(int address) {
    if (address < 0) return -1;
    return address % BLOCK_SIZE;
}

int calcular_direccion_fisica(int frame_number, int offset) {
    if (frame_number < 0 || offset < 0) return -1;
    return (frame_number * BLOCK_SIZE) + offset;
}

// Estructura simple para simular páginas
typedef struct {
    int page_number;
    time_t last_access;
    int clock_bit;
    bool present;
} simple_page;

// Simulación simple de LRU
simple_page* lru_seleccionar_victima_simple(simple_page* pages, int count) {
    if (!pages || count == 0) return NULL;
    
    simple_page* victima = NULL;
    time_t oldest_time = time(NULL) + 1; // Inicializar con tiempo futuro
    
    for (int i = 0; i < count; i++) {
        if (pages[i].present && pages[i].last_access < oldest_time) {
            oldest_time = pages[i].last_access;
            victima = &pages[i];
        }
    }
    
    return victima;
}

// Simulación simple de CLOCK
simple_page* clock_seleccionar_victima_simple(simple_page* pages, int count, int* clock_pointer) {
    if (!pages || count == 0) return NULL;
    
    int intentos = 0;
    
    while (intentos < count * 2) {
        if (*clock_pointer >= count) {
            *clock_pointer = 0;
        }
        
        simple_page* page = &pages[*clock_pointer];
        
        if (page->present) {
            if (page->clock_bit == 0) {
                (*clock_pointer)++;
                return page;
            } else {
                page->clock_bit = 0; // Segunda oportunidad
            }
        }
        
        (*clock_pointer)++;
        intentos++;
    }
    
    // Si no encuentra ninguna, selecciona la primera disponible
    for (int i = 0; i < count; i++) {
        if (pages[i].present) {
            return &pages[i];
        }
    }
    
    return NULL;
}

context (test_algoritmos_simple) {
    
    describe("Tests básicos de funciones utilitarias") {
        
        it("crear_file_tag_key funciona correctamente") {
            char* key = crear_file_tag_key("archivo.txt", "v1");
            should_string(key) be equal to("archivo.txt:v1");
            free(key);
        } end
        
        it("calcular_numero_pagina funciona correctamente") {
            should_int(calcular_numero_pagina(0)) be equal to(0);
            should_int(calcular_numero_pagina(64)) be equal to(1);
            should_int(calcular_numero_pagina(128)) be equal to(2);
        } end
        
        it("calcular_offset_pagina funciona correctamente") {
            should_int(calcular_offset_pagina(0)) be equal to(0);
            should_int(calcular_offset_pagina(32)) be equal to(32);
            should_int(calcular_offset_pagina(64)) be equal to(0);
        } end
        
        it("calcular_direccion_fisica funciona correctamente") {
            should_int(calcular_direccion_fisica(0, 0)) be equal to(0);
            should_int(calcular_direccion_fisica(1, 32)) be equal to(96);
        } end
        
    } end
    
    describe("Tests de algoritmo LRU simplificado") {
        
        it("selecciona la página más antigua") {
            simple_page pages[3];
            time_t now = time(NULL);
            
            // Configurar páginas con diferentes tiempos
            pages[0].page_number = 0;
            pages[0].last_access = now - 30; // Más antigua
            pages[0].present = true;
            
            pages[1].page_number = 1;
            pages[1].last_access = now - 10; // Más reciente
            pages[1].present = true;
            
            pages[2].page_number = 2;
            pages[2].last_access = now - 20; // Intermedia
            pages[2].present = true;
            
            simple_page* victima = lru_seleccionar_victima_simple(pages, 3);
            should_ptr(victima) not be null;
            should_int(victima->page_number) be equal to(0); // La más antigua
        } end
        
        it("maneja páginas con mismo tiempo de acceso") {
            simple_page pages[3];
            time_t same_time = time(NULL);
            
            for (int i = 0; i < 3; i++) {
                pages[i].page_number = i;
                pages[i].last_access = same_time;
                pages[i].present = true;
            }
            
            simple_page* victima = lru_seleccionar_victima_simple(pages, 3);
            should_ptr(victima) not be null; // Debe seleccionar alguna
        } end
        
        it("ignora páginas no presentes") {
            simple_page pages[2];
            time_t now = time(NULL);
            
            pages[0].page_number = 0;
            pages[0].last_access = now - 100; // Muy antigua pero no presente
            pages[0].present = false;
            
            pages[1].page_number = 1;
            pages[1].last_access = now - 10;
            pages[1].present = true;
            
            simple_page* victima = lru_seleccionar_victima_simple(pages, 2);
            should_ptr(victima) not be null;
            should_int(victima->page_number) be equal to(1); // La única presente
        } end
        
    } end
    
    describe("Tests de algoritmo CLOCK simplificado") {
        
        it("selecciona página con clock_bit = 0") {
            simple_page pages[2];
            int clock_pointer = 0;
            
            pages[0].page_number = 0;
            pages[0].clock_bit = 1;
            pages[0].present = true;
            
            pages[1].page_number = 1;
            pages[1].clock_bit = 0; // Candidata inmediata
            pages[1].present = true;
            
            simple_page* victima = clock_seleccionar_victima_simple(pages, 2, &clock_pointer);
            should_ptr(victima) not be null;
            should_int(victima->page_number) be equal to(1);
        } end
        
        it("da segunda oportunidad correctamente") {
            simple_page pages[2];
            int clock_pointer = 0;
            
            pages[0].page_number = 0;
            pages[0].clock_bit = 1;
            pages[0].present = true;
            
            pages[1].page_number = 1;
            pages[1].clock_bit = 1;
            pages[1].present = true;
            
            simple_page* victima = clock_seleccionar_victima_simple(pages, 2, &clock_pointer);
            should_ptr(victima) not be null;
            
            // Verificar que se dio segunda oportunidad
            should_int(pages[0].clock_bit) be equal to(0);
        } end
        
        it("avanza el puntero correctamente") {
            simple_page pages[1];
            int clock_pointer = 0;
            
            pages[0].page_number = 0;
            pages[0].clock_bit = 0;
            pages[0].present = true;
            
            int pointer_inicial = clock_pointer;
            clock_seleccionar_victima_simple(pages, 1, &clock_pointer);
            
            should_bool(clock_pointer > pointer_inicial) be equal to(true);
        } end
        
    } end
    
    describe("Tests de integración y casos complejos") {
        
        it("maneja arrays vacíos sin romper") {
            simple_page* victima_lru = lru_seleccionar_victima_simple(NULL, 0);
            should_ptr(victima_lru) be null;
            
            int clock_pointer = 0;
            simple_page* victima_clock = clock_seleccionar_victima_simple(NULL, 0, &clock_pointer);
            should_ptr(victima_clock) be null;
        } end
        
        it("verifica consistencia en escenarios controlados") {
            // Crear un escenario donde sabemos qué página debería ser seleccionada
            simple_page pages[3];
            time_t now = time(NULL);
            
            // Configurar para que página 2 sea la más antigua en LRU
            pages[0].page_number = 0;
            pages[0].last_access = now - 10;
            pages[0].present = true;
            
            pages[1].page_number = 1;
            pages[1].last_access = now - 5;
            pages[1].present = true;
            
            pages[2].page_number = 2;
            pages[2].last_access = now - 50; // La más antigua
            pages[2].present = true;
            
            simple_page* victima_lru = lru_seleccionar_victima_simple(pages, 3);
            should_ptr(victima_lru) not be null;
            should_int(victima_lru->page_number) be equal to(2);
        } end
        
        it("maneja casos límite de direcciones") {
            // Probar con direcciones en límites de página
            int boundary_addresses[] = {0, 63, 64, 127, 128, 191, 192};
            int expected_pages[] = {0, 0, 1, 1, 2, 2, 3};
            int expected_offsets[] = {0, 63, 0, 63, 0, 63, 0};
            
            for (int i = 0; i < 7; i++) {
                int page = calcular_numero_pagina(boundary_addresses[i]);
                int offset = calcular_offset_pagina(boundary_addresses[i]);
                
                should_int(page) be equal to(expected_pages[i]);
                should_int(offset) be equal to(expected_offsets[i]);
                
                // Verificar que la dirección física es correcta
                int direccion_fisica = calcular_direccion_fisica(page, offset);
                should_int(direccion_fisica) be equal to(boundary_addresses[i]);
            }
        } end
        
    } end
}