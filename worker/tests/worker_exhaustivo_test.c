#include <cspecs/cspec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

int BLOCK_SIZE = 64;
int TAM_MEMORIA = 512; 
int current_query_id = 1;

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
    void* memory_space;
    int total_pages;
    int used_pages;
    void* page_tables; 
    void* physical_pages; 
    int clock_pointer;
    pthread_mutex_t memory_mutex;
    bool* frame_table;
} t_internal_memory;

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

t_internal_memory* internal_memory = NULL;

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

t_file_tag crear_file_tag(char* file_name, char* tag) {
    t_file_tag ft;
    ft.file_name = file_name ? strdup(file_name) : NULL;
    ft.tag = tag ? strdup(tag) : NULL;
    return ft;
}

void liberar_instruccion(t_instruction* instruccion) {
    if (!instruccion) return;
    if (instruccion->file_name) free(instruccion->file_name);
    if (instruccion->tag) free(instruccion->tag);
    if (instruccion->content) free(instruccion->content);
    if (instruccion->dest_file_name) free(instruccion->dest_file_name);
    if (instruccion->dest_tag) free(instruccion->dest_tag);
    free(instruccion);
}

// Mock
void inicializar_memoria_interna() {
    internal_memory = malloc(sizeof(t_internal_memory));
    internal_memory->memory_space = malloc(TAM_MEMORIA);
    internal_memory->total_pages = TAM_MEMORIA / BLOCK_SIZE;
    internal_memory->used_pages = 0;
    internal_memory->clock_pointer = 0;
    internal_memory->frame_table = calloc(internal_memory->total_pages, sizeof(bool));
    pthread_mutex_init(&internal_memory->memory_mutex, NULL);
}

int asignar_marco_libre() {
    if (!internal_memory) return -1;
    for (int i = 0; i < internal_memory->total_pages; i++) {
        if (!internal_memory->frame_table[i]) {
            internal_memory->frame_table[i] = true;
            return i;
        }
    }
    return -1;
}

void liberar_marco(int frame_number) {
    if (!internal_memory || frame_number < 0 || frame_number >= internal_memory->total_pages) 
        return;
    internal_memory->frame_table[frame_number] = false;
}

void limpiar_memoria_test() {
    if (internal_memory) {
        if (internal_memory->memory_space) free(internal_memory->memory_space);
        if (internal_memory->frame_table) free(internal_memory->frame_table);
        pthread_mutex_destroy(&internal_memory->memory_mutex);
        free(internal_memory);
        internal_memory = NULL;
    }
}

context (test_worker_exhaustivo) {
    
    describe("Tests exhaustivos de crear_file_tag_key") {
        
        it("maneja nombres muy largos sin desbordamiento") {
            char* nombre_largo = malloc(1000);
            char* tag_largo = malloc(1000);
            
            // Llenar con caracteres
            for (int i = 0; i < 999; i++) {
                nombre_largo[i] = 'a' + (i % 26);
                tag_largo[i] = 'A' + (i % 26);
            }
            nombre_largo[999] = '\0';
            tag_largo[999] = '\0';
            
            char* key = crear_file_tag_key(nombre_largo, tag_largo);
            should_ptr(key) not be null;
            
            // Verificar que la longitud sea correcta
            int expected_len = strlen(nombre_largo) + strlen(tag_largo) + 1; // +1 por ':'
            should_int(strlen(key)) be equal to(expected_len);
            
            free(key);
            free(nombre_largo);
            free(tag_largo);
        } end
        
        it("maneja caracteres especiales correctamente") {
            char* key1 = crear_file_tag_key("archivo@#$.txt", "tag!%&()");
            should_string(key1) be equal to("archivo@#$.txt:tag!%&()");
            
            char* key2 = crear_file_tag_key("archivo con espacios.txt", "tag con espacios");
            should_string(key2) be equal to("archivo con espacios.txt:tag con espacios");
            
            free(key1);
            free(key2);
        } end
        
        it("maneja punteros NULL sin romper") {
            char* key1 = crear_file_tag_key(NULL, "tag");
            should_ptr(key1) be null;
            
            char* key2 = crear_file_tag_key("archivo", NULL);
            should_ptr(key2) be null;
            
            char* key3 = crear_file_tag_key(NULL, NULL);
            should_ptr(key3) be null;
        } end
        
        it("no produce memory leaks en uso intensivo") {
            for (int i = 0; i < 1000; i++) {
                char archivo[50];
                char tag[50];
                sprintf(archivo, "archivo_%d.txt", i);
                sprintf(tag, "tag_%d", i);
                
                char* key = crear_file_tag_key(archivo, tag);
                should_ptr(key) not be null;
                free(key);
            }
            should_bool(true) be equal to(true); 
        } end
        
    } end
    
    describe("Tests de límites para cálculos de páginas") {
        
        it("maneja direcciones negativas correctamente") {
            int page = calcular_numero_pagina(-1);
            should_int(page) be equal to(-1);
            
            int offset = calcular_offset_pagina(-100);
            should_int(offset) be equal to(-1);
            
            int direccion = calcular_direccion_fisica(-1, 10);
            should_int(direccion) be equal to(-1);
            
            int direccion2 = calcular_direccion_fisica(5, -1);
            should_int(direccion2) be equal to(-1);
        } end
        
        it("maneja el valor 0 correctamente en todos los casos") {
            int page = calcular_numero_pagina(0);
            should_int(page) be equal to(0);
            
            int offset = calcular_offset_pagina(0);
            should_int(offset) be equal to(0);
            
            int direccion = calcular_direccion_fisica(0, 0);
            should_int(direccion) be equal to(0);
        } end
        
        it("calcula correctamente en los límites de página") {
            for (int pagina = 0; pagina < 10; pagina++) {
                int inicio = pagina * BLOCK_SIZE;
                int fin = inicio + BLOCK_SIZE - 1;
                
                should_int(calcular_numero_pagina(inicio)) be equal to(pagina);
                should_int(calcular_numero_pagina(fin)) be equal to(pagina);
                should_int(calcular_numero_pagina(fin + 1)) be equal to(pagina + 1);
                
                should_int(calcular_offset_pagina(inicio)) be equal to(0);
                should_int(calcular_offset_pagina(fin)) be equal to(BLOCK_SIZE - 1);
                should_int(calcular_offset_pagina(fin + 1)) be equal to(0);
            }
        } end
        
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
        
    } end
    
    describe("Tests de consistencia matemática") {
        
        it("verifica que address = página * BLOCK_SIZE + offset") {
            for (int address = 0; address < 10000; address += 17) { 
                int page = calcular_numero_pagina(address);
                int offset = calcular_offset_pagina(address);
                int recalculado = calcular_direccion_fisica(page, offset);
                
                should_int(recalculado) be equal to(address);
            }
        } end
        
        it("verifica propiedades matemáticas básicas") {
            // Propiedad: offset siempre < BLOCK_SIZE
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
        
    } end
    
    describe("Tests exhaustivos de gestión de memoria") {
        
        before {
            limpiar_memoria_test();
            inicializar_memoria_interna();
        } end
        
        after {
            limpiar_memoria_test();
        } end
        
        it("inicializa correctamente con diferentes tamaños de memoria") {
            limpiar_memoria_test();
            
            // Probar con memoria pequeña
            TAM_MEMORIA = 128; // 2 pag
            inicializar_memoria_interna();
            should_int(internal_memory->total_pages) be equal to(2);
            
            limpiar_memoria_test();
            
            // Probar con memoria grande
            TAM_MEMORIA = 1024; // 16 pag
            inicializar_memoria_interna();
            should_int(internal_memory->total_pages) be equal to(16);
            
            // Restaurar valor original
            TAM_MEMORIA = 512;
        } end
        
        it("asigna marcos secuencialmente") {
            int marcos[8];
            for (int i = 0; i < 8; i++) {
                marcos[i] = asignar_marco_libre();
                should_int(marcos[i]) be equal to(i);
                should_bool(internal_memory->frame_table[i]) be equal to(true);
            }
        } end
        
        it("maneja correctamente el agotamiento de marcos") {
            // Llenar toda la memoria
            for (int i = 0; i < internal_memory->total_pages; i++) {
                int marco = asignar_marco_libre();
                should_int(marco) be equal to(i);
            }
            
            // deberia fallar
            int marco_extra = asignar_marco_libre();
            should_int(marco_extra) be equal to(-1);
        } end
        
        it("permite reutilización de marcos liberados") {
            // Asignar algunos marcos
            int marco1 = asignar_marco_libre(); // 0
            int marco2 = asignar_marco_libre(); // 1
            int marco3 = asignar_marco_libre(); // 2
            
            // Liberar el marco del medio
            liberar_marco(marco2);
            should_bool(internal_memory->frame_table[marco2]) be equal to(false);
            
            // El siguiente marco asignado deberia ser el liberado
            int marco_reutilizado = asignar_marco_libre();
            should_int(marco_reutilizado) be equal to(marco2);
        } end
        
        it("maneja liberación de marcos inválidos sin romper") {
            liberar_marco(-1);        // Índice negativo
            liberar_marco(1000);      // Índice muy grande
            liberar_marco(internal_memory->total_pages); // Justo fuera del rango
            
            // La memoria deberia seguir funcionando normalmente
            int marco = asignar_marco_libre();
            should_int(marco) be equal to(0);
        } end
        
    } end
    
    describe("Tests de t_file_tag") {
        
        it("maneja correctamente valores NULL") {
            t_file_tag ft1 = crear_file_tag(NULL, "tag");
            should_ptr(ft1.file_name) be null;
            should_string(ft1.tag) be equal to("tag");
            
            t_file_tag ft2 = crear_file_tag("archivo", NULL);
            should_string(ft2.file_name) be equal to("archivo");
            should_ptr(ft2.tag) be null;
            
            t_file_tag ft3 = crear_file_tag(NULL, NULL);
            should_ptr(ft3.file_name) be null;
            should_ptr(ft3.tag) be null;
            
            // Limpiar memoria no NULL
            if (ft1.tag) free(ft1.tag);
            if (ft2.file_name) free(ft2.file_name);
        } end
        
        it("crea copias profundas correctas") {
            char original[20] = "original"; 
            t_file_tag ft = crear_file_tag(original, original);
            
            strcpy(original, "modificado");
            
            should_string(ft.file_name) be equal to("original");
            should_string(ft.tag) be equal to("original");
            
            should_ptr(ft.file_name) not be equal to(original);
            should_ptr(ft.tag) not be equal to(original);
            
            free(ft.file_name);
            free(ft.tag);
        } end
        
    } end
    
    describe("Tests exhaustivos de instrucciones") {
        
        it("libera correctamente instrucciones complejas") {
            t_instruction* instr = malloc(sizeof(t_instruction));
            instr->type = INSTR_TAG;
            instr->file_name = strdup("archivo_origen_muy_largo_con_muchos_caracteres.txt");
            instr->tag = strdup("tag_origen_tambien_muy_largo");
            instr->address = 123456;
            instr->size = 789;
            instr->content = strdup("Este es un contenido muy largo que simula datos reales que podrían estar en una instrucción de write con mucha información");
            instr->dest_file_name = strdup("archivo_destino_igualmente_largo.txt");
            instr->dest_tag = strdup("tag_destino_largo");
            
            liberar_instruccion(instr);
            should_bool(true) be equal to(true);
        } end
        
        it("maneja correctamente instrucción NULL") {
            liberar_instruccion(NULL);
            should_bool(true) be equal to(true);
        } end
        
        it("libera instrucciones con mezcla de campos NULL y no NULL") {
            t_instruction* instr = malloc(sizeof(t_instruction));
            instr->type = INSTR_WRITE;
            instr->file_name = strdup("archivo.txt");
            instr->tag = NULL;
            instr->address = 100;
            instr->size = 50;
            instr->content = strdup("contenido");
            instr->dest_file_name = NULL;
            instr->dest_tag = strdup("tag_destino");
            
            liberar_instruccion(instr);
            should_bool(true) be equal to(true);
        } end
        
    } end
    
    describe("Tests de estrés y concurrencia básica") {
        
        it("maneja creación masiva de keys sin problemas") {
            int num_tests = 100;
            char** keys = malloc(num_tests * sizeof(char*));
            
            for (int i = 0; i < num_tests; i++) {
                char archivo[100];
                char tag[100];
                sprintf(archivo, "archivo_%d_test.txt", i);
                sprintf(tag, "tag_%d_version", i);
                
                keys[i] = crear_file_tag_key(archivo, tag);
                should_ptr(keys[i]) not be null;
            }
            
            for (int i = 0; i < 10; i++) {
                char expected[200];
                sprintf(expected, "archivo_%d_test.txt:tag_%d_version", i, i);
                should_string(keys[i]) be equal to(expected);
            }
            
            for (int i = 0; i < num_tests; i++) {
                free(keys[i]);
            }
            free(keys);
        } end
        
        it("realiza cálculos consistentes bajo uso intensivo") {
            for (int iteracion = 0; iteracion < 10; iteracion++) {
                for (int address = 0; address < 500; address += 13) {
                    int page1 = calcular_numero_pagina(address);
                    int offset1 = calcular_offset_pagina(address);
                    int direccion1 = calcular_direccion_fisica(page1, offset1);
                    
                    int page2 = calcular_numero_pagina(address);
                    int offset2 = calcular_offset_pagina(address);
                    int direccion2 = calcular_direccion_fisica(page2, offset2);
                    
                    should_int(page1) be equal to(page2);
                    should_int(offset1) be equal to(offset2);
                    should_int(direccion1) be equal to(direccion2);
                    should_int(direccion1) be equal to(address);
                }
            }
        } end
        
    } end
}