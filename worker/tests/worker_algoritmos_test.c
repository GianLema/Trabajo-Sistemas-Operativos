#include <cspecs/cspec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

int BLOCK_SIZE = 64;
int TAM_MEMORIA = 256; // 4 paginas
int current_query_id = 1;
char* ALGORITMO_REEMPLAZO = "LRU";

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
    time_t last_access;
    int clock_bit;
    int frame_number;
} t_page;

typedef struct {
    t_page** pages;
    int size;
    int capacity;
} t_page_list;

typedef struct {
    void* memory_space;
    int total_pages;
    int used_pages;
    t_page_list* physical_pages;
    int clock_pointer;
    pthread_mutex_t memory_mutex;
    bool* frame_table;
} t_internal_memory;

t_internal_memory* internal_memory = NULL;

t_page_list* crear_lista_paginas() {
    t_page_list* lista = malloc(sizeof(t_page_list));
    lista->capacity = 10;
    lista->size = 0;
    lista->pages = malloc(sizeof(t_page*) * lista->capacity);
    return lista;
}

void agregar_pagina(t_page_list* lista, t_page* page) {
    if (lista->size >= lista->capacity) {
        lista->capacity *= 2;
        lista->pages = realloc(lista->pages, sizeof(t_page*) * lista->capacity);
    }
    lista->pages[lista->size++] = page;
}

void liberar_lista_paginas(t_page_list* lista) {
    if (lista) {
        free(lista->pages);
        free(lista);
    }
}

t_page* crear_pagina_test(char* file_name, char* tag, int page_number) {
    t_page* page = malloc(sizeof(t_page));
    page->file_tag.file_name = strdup(file_name);
    page->file_tag.tag = strdup(tag);
    page->page_number = page_number;
    page->modified = false;
    page->present = true;
    page->content = malloc(BLOCK_SIZE);
    page->last_access = time(NULL);
    page->clock_bit = 1;
    page->frame_number = -1;
    return page;
}

void liberar_pagina_test(t_page* page) {
    if (page) {
        if (page->file_tag.file_name) free(page->file_tag.file_name);
        if (page->file_tag.tag) free(page->file_tag.tag);
        if (page->content) free(page->content);
        free(page);
    }
}

t_page* algoritmo_lru_seleccionar_victima(t_page_list* pages) {
    if (!pages || pages->size == 0) return NULL;
    
    t_page* victima = NULL;
    time_t oldest_time = time(NULL) + 1; // Inicializar con tiempo futuro
    
    for (int i = 0; i < pages->size; i++) {
        t_page* page = pages->pages[i];
        if (page->present && page->last_access < oldest_time) {
            oldest_time = page->last_access;
            victima = page;
        }
    }
    
    return victima;
}

void algoritmo_lru_actualizar(t_page* page) {
    if (page) {
        page->last_access = time(NULL);
    }
}


t_page* algoritmo_clock_seleccionar_victima(t_page_list* pages, int* clock_pointer) {
    if (!pages || pages->size == 0) return NULL;
    
    int intentos = 0;
    int total_pages = pages->size;
    
    while (intentos < total_pages * 2) {
        if (*clock_pointer >= total_pages) {
            *clock_pointer = 0;
        }
        
        t_page* page = pages->pages[*clock_pointer];
        
        if (page->present) {
            if (page->clock_bit == 0) {
                (*clock_pointer)++;
                return page;
            } else {
                page->clock_bit = 0; 
            }
        }
        
        (*clock_pointer)++;
        intentos++;
    }
    
    // Si no encuentra ninguna, selecciona la primera disponible
    for (int i = 0; i < total_pages; i++) {
        t_page* page = pages->pages[i];
        if (page->present) {
            return page;
        }
    }
    
    return NULL;
}

// Mock
char* crear_file_tag_key(char* file_name, char* tag) {
    if (!file_name || !tag) return NULL;
    int len = strlen(file_name) + strlen(tag) + 2;
    char* key = malloc(len);
    sprintf(key, "%s:%s", file_name, tag);
    return key;
}

int calcular_numero_pagina(int address) {
    return address / BLOCK_SIZE;
}

int calcular_offset_pagina(int address) {
    return address % BLOCK_SIZE;
}

context (test_worker_algoritmos) {
    
    describe("Algoritmo LRU - Tests unitarios") {
        
        it("selecciona la pagina menos recientemente usada") {
            t_page_list* pages = crear_lista_paginas();
            
            // Crear páginas con diferentes tiempos de acceso
            time_t now = time(NULL);
            
            t_page* page1 = crear_pagina_test("file1", "tag1", 0);
            page1->last_access = now - 100; // Más antigua
            
            t_page* page2 = crear_pagina_test("file2", "tag2", 0);
            page2->last_access = now - 50;  // Intermedia
            
            t_page* page3 = crear_pagina_test("file3", "tag3", 0);
            page3->last_access = now - 10;  // Más reciente
            
            agregar_pagina(pages, page1);
            agregar_pagina(pages, page2);
            agregar_pagina(pages, page3);
            
            t_page* victima = algoritmo_lru_seleccionar_victima(pages);
            should_ptr(victima) be equal to(page1);
            
            // Limpiar
            liberar_pagina_test(page1);
            liberar_pagina_test(page2);
            liberar_pagina_test(page3);
            liberar_lista_paginas(pages);
        } end
        
        it("actualiza correctamente el tiempo de acceso") {
            t_page* page = crear_pagina_test("test", "v1", 0);
            time_t antes = page->last_access;
            
            sleep(1); // Esperar un segundo
            
            algoritmo_lru_actualizar(page);
            
            should_bool(page->last_access > antes) be equal to(true);
            
            liberar_pagina_test(page);
        } end
        
        it("maneja lista vacia sin romper") {
            t_page_list* pages = crear_lista_paginas();
            
            t_page* victima = algoritmo_lru_seleccionar_victima(pages);
            should_ptr(victima) be null;
            
            liberar_lista_paginas(pages);
        } end
        
        it("ignora paginas no presentes") {
            t_page_list* pages = crear_lista_paginas();
            
            t_page* page1 = crear_pagina_test("file1", "tag1", 0);
            page1->present = false; // No presente
            page1->last_access = time(NULL) - 1000; // Muy antigua pero no presente
            
            t_page* page2 = crear_pagina_test("file2", "tag2", 0);
            page2->present = true;  // Presente
            page2->last_access = time(NULL) - 50;
            
            agregar_pagina(pages, page1);
            agregar_pagina(pages, page2);
            
            t_page* victima = algoritmo_lru_seleccionar_victima(pages);
            should_ptr(victima) be equal to(page2); // Debe seleccionar la presente
            
            liberar_pagina_test(page1);
            liberar_pagina_test(page2);
            liberar_lista_paginas(pages);
        } end
        
    } end
    
    describe("Algoritmo CLOCK-M - Tests unitarios") {
        
        it("selecciona pagina con clock_bit = 0") {
            t_page_list* pages = crear_lista_paginas();
            int clock_pointer = 0;
            
            t_page* page1 = crear_pagina_test("file1", "tag1", 0);
            page1->clock_bit = 1; // Segunda oportunidad
            
            t_page* page2 = crear_pagina_test("file2", "tag2", 0);
            page2->clock_bit = 0; // Candidata inmediata
            
            agregar_pagina(pages, page1);
            agregar_pagina(pages, page2);
            
            t_page* victima = algoritmo_clock_seleccionar_victima(pages, &clock_pointer);
            should_ptr(victima) be equal to(page2);
            
            liberar_pagina_test(page1);
            liberar_pagina_test(page2);
            liberar_lista_paginas(pages);
        } end
        
        it("da segunda oportunidad correctamente") {
            t_page_list* pages = crear_lista_paginas();
            int clock_pointer = 0;
            
            t_page* page1 = crear_pagina_test("file1", "tag1", 0);
            page1->clock_bit = 1;
            
            t_page* page2 = crear_pagina_test("file2", "tag2", 0);
            page2->clock_bit = 1; 
            
            agregar_pagina(pages, page1);
            agregar_pagina(pages, page2);
            
            // Primera llamada debería dar segunda oportunidad y seleccionar page1
            t_page* victima = algoritmo_clock_seleccionar_victima(pages, &clock_pointer);
            should_ptr(victima) be equal to(page1);
            should_int(page1->clock_bit) be equal to(0); // Debería haber cambiado
            
            liberar_pagina_test(page1);
            liberar_pagina_test(page2);
            liberar_lista_paginas(pages);
        } end
        
        it("avanza el puntero correctamente") {
            t_page_list* pages = crear_lista_paginas();
            int clock_pointer = 0;
            
            t_page* page1 = crear_pagina_test("file1", "tag1", 0);
            page1->clock_bit = 0;
            
            agregar_pagina(pages, page1);
            
            int pointer_inicial = clock_pointer;
            algoritmo_clock_seleccionar_victima(pages, &clock_pointer);
            
            should_bool(clock_pointer > pointer_inicial) be equal to(true);
            
            liberar_pagina_test(page1);
            liberar_lista_paginas(pages);
        } end
        
        it("maneja puntero que excede el tamaño de la lista") {
            t_page_list* pages = crear_lista_paginas();
            int clock_pointer = 10; // Más grande que el tamaño de la lista
            
            t_page* page1 = crear_pagina_test("file1", "tag1", 0);
            page1->clock_bit = 0;
            
            agregar_pagina(pages, page1);
            
            t_page* victima = algoritmo_clock_seleccionar_victima(pages, &clock_pointer);
            should_ptr(victima) be equal to(page1); // Debería funcionar igual
            
            liberar_pagina_test(page1);
            liberar_lista_paginas(pages);
        } end
        
    } end
    
    describe("Tests de integracion - Escenarios reales") {
        
        it("simula acceso secuencial a paginas") {
            t_page_list* pages = crear_lista_paginas();
            
            for (int i = 0; i < 4; i++) {
                t_page* page = crear_pagina_test("archivo.txt", "v1", i);
                page->last_access = time(NULL) - (4 - i); // Acceso secuencial
                agregar_pagina(pages, page);
            }
            
            // LRU deberia seleccionar la página 0 (mas antigua)
            t_page* victima = algoritmo_lru_seleccionar_victima(pages);
            should_int(victima->page_number) be equal to(0);
            
            // Simular acceso a la pagina 0 (actualizarla)
            algoritmo_lru_actualizar(victima);
            
            // Ahora LRU deberia seleccionar la pagina 1
            t_page* nueva_victima = algoritmo_lru_seleccionar_victima(pages);
            should_int(nueva_victima->page_number) be equal to(1);
            
            // Limpiar
            for (int i = 0; i < pages->size; i++) {
                liberar_pagina_test(pages->pages[i]);
            }
            liberar_lista_paginas(pages);
        } end
        
        it("simula patron de acceso aleatorio") {
            t_page_list* pages = crear_lista_paginas();
            time_t base_time = time(NULL);
            
            // Crear paginas con patrón de acceso aleatorio
            int access_order[] = {3, 1, 4, 2, 0}; 
            
            for (int i = 0; i < 5; i++) {
                t_page* page = crear_pagina_test("random.txt", "v1", i);
                page->last_access = base_time - access_order[i] * 10;
                agregar_pagina(pages, page);
            }
            
            t_page* victima = algoritmo_lru_seleccionar_victima(pages);
            should_int(victima->page_number) be equal to(2); 
            
            // Limpiar
            for (int i = 0; i < pages->size; i++) {
                liberar_pagina_test(pages->pages[i]);
            }
            liberar_lista_paginas(pages);
        } end
        
        it("simula comportamiento bajo carga de múltiples archivos") {
            t_page_list* pages = crear_lista_paginas();
            time_t now = time(NULL);
            
            char* archivos[] = {"app1.txt", "app2.txt", "app3.txt"};
            char* tags[] = {"v1", "v2", "v1"};
            
            for (int archivo = 0; archivo < 3; archivo++) {
                for (int pagina = 0; pagina < 2; pagina++) {
                    t_page* page = crear_pagina_test(archivos[archivo], tags[archivo], pagina);
                    page->last_access = now - (archivo * 2 + pagina) * 5;
                    agregar_pagina(pages, page);
                }
            }
            
            t_page* victima = algoritmo_lru_seleccionar_victima(pages);
            should_string(victima->file_tag.file_name) be equal to("app3.txt");
            should_int(victima->page_number) be equal to(1); // La más antigua
            
            // Limpiar
            for (int i = 0; i < pages->size; i++) {
                liberar_pagina_test(pages->pages[i]);
            }
            liberar_lista_paginas(pages);
        } end
        
        it("verifica consistencia entre algoritmos con mismos datos") {
            t_page_list* pages_lru = crear_lista_paginas();
            t_page_list* pages_clock = crear_lista_paginas();
            int clock_pointer = 0;
            
            for (int i = 0; i < 3; i++) {
                t_page* page_lru = crear_pagina_test("test.txt", "v1", i);
                t_page* page_clock = crear_pagina_test("test.txt", "v1", i);
                
                page_lru->last_access = time(NULL) - (3 - i) * 10;
                page_clock->clock_bit = (i == 0) ? 0 : 1; 
                
                agregar_pagina(pages_lru, page_lru);
                agregar_pagina(pages_clock, page_clock);
            }
            
            t_page* victima_lru = algoritmo_lru_seleccionar_victima(pages_lru);
            t_page* victima_clock = algoritmo_clock_seleccionar_victima(pages_clock, &clock_pointer);
            
            should_int(victima_lru->page_number) be equal to(2); // Más antigua en LRU
            should_int(victima_clock->page_number) be equal to(0); // clock_bit = 0 en CLOCK
            
            // Limpiar
            for (int i = 0; i < pages_lru->size; i++) {
                liberar_pagina_test(pages_lru->pages[i]);
                liberar_pagina_test(pages_clock->pages[i]);
            }
            liberar_lista_paginas(pages_lru);
            liberar_lista_paginas(pages_clock);
        } end
        
    } end
    
    describe("Tests de robustez y casos límite") {
        
        it("maneja correctamente páginas con tiempos de acceso idénticos") {
            t_page_list* pages = crear_lista_paginas();
            time_t mismo_tiempo = time(NULL);
            
            for (int i = 0; i < 3; i++) {
                t_page* page = crear_pagina_test("igual.txt", "v1", i);
                page->last_access = mismo_tiempo;
                agregar_pagina(pages, page);
            }
            
            t_page* victima = algoritmo_lru_seleccionar_victima(pages);
            should_ptr(victima) not be null; 
            
            // Limpiar
            for (int i = 0; i < pages->size; i++) {
                liberar_pagina_test(pages->pages[i]);
            }
            liberar_lista_paginas(pages);
        } end
        
        it("maneja páginas todas con clock_bit = 1") {
            t_page_list* pages = crear_lista_paginas();
            int clock_pointer = 0;
            
            for (int i = 0; i < 3; i++) {
                t_page* page = crear_pagina_test("clock1.txt", "v1", i);
                page->clock_bit = 1;
                agregar_pagina(pages, page);
            }
            
            t_page* victima = algoritmo_clock_seleccionar_victima(pages, &clock_pointer);
            should_ptr(victima) not be null; // Debe seleccionar alguna después de dar segunda oportunidad
            
            bool alguna_con_cero = false;
            for (int i = 0; i < pages->size; i++) {
                if (pages->pages[i]->clock_bit == 0) {
                    alguna_con_cero = true;
                    break;
                }
            }
            should_bool(alguna_con_cero) be equal to(true);
            
            // Limpiar
            for (int i = 0; i < pages->size; i++) {
                liberar_pagina_test(pages->pages[i]);
            }
            liberar_lista_paginas(pages);
        } end
        
        it("verifica que los algoritmos no modifican páginas no seleccionadas incorrectamente") {
            t_page_list* pages = crear_lista_paginas();
            time_t now = time(NULL);
            
            t_page* page1 = crear_pagina_test("file1", "tag1", 0);
            page1->last_access = now - 100; // Será víctima
            page1->modified = false;
            
            t_page* page2 = crear_pagina_test("file2", "tag2", 0);  
            page2->last_access = now - 10;  // No será víctima
            page2->modified = false;
            
            agregar_pagina(pages, page1);
            agregar_pagina(pages, page2);
            
            algoritmo_lru_seleccionar_victima(pages);
            
            // page2 no deberia haber sido modificada
            should_bool(page2->modified) be equal to(false);
            should_int(page2->last_access) be equal to(now - 10);
            
            liberar_pagina_test(page1);
            liberar_pagina_test(page2);
            liberar_lista_paginas(pages);
        } end
        
    } end
}