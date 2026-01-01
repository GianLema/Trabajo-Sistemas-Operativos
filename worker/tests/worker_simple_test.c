#include <cspecs/cspec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int BLOCK_SIZE = 64;

char* crear_file_tag_key(char* file_name, char* tag) {
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

int calcular_direccion_fisica(int frame_number, int offset) {
    return (frame_number * BLOCK_SIZE) + offset;
}

typedef struct {
    char* file_name;
    char* tag;
} t_file_tag;

t_file_tag crear_file_tag(char* file_name, char* tag) {
    t_file_tag ft;
    ft.file_name = strdup(file_name);
    ft.tag = strdup(tag);
    return ft;
}

context (test_worker_utils) {
    describe("crear_file_tag_key") {
        it("concatena correctamente file_name y tag con ':'") {
            char* key = crear_file_tag_key("archivo.txt", "tag1");
            should_string(key) be equal to("archivo.txt:tag1");
            free(key);
        } end
        
        it("maneja strings vacíos") {
            char* key = crear_file_tag_key("", "");
            should_string(key) be equal to(":");
            free(key);
        } end
        
        it("maneja un archivo sin tag") {
            char* key = crear_file_tag_key("archivo.txt", "");
            should_string(key) be equal to("archivo.txt:");
            free(key);
        } end
    } end
    
    describe("calcular_numero_pagina") {
        it("calcula correctamente la página para address 0") {
            int page = calcular_numero_pagina(0);
            should_int(page) be equal to(0);
        } end
        
        it("calcula correctamente páginas dentro del primer bloque") {
            int page1 = calcular_numero_pagina(32);
            int page2 = calcular_numero_pagina(63);
            should_int(page1) be equal to(0);
            should_int(page2) be equal to(0);
        } end
        
        it("calcula correctamente el cambio de página") {
            int page1 = calcular_numero_pagina(63);  
            int page2 = calcular_numero_pagina(64);  
            int page3 = calcular_numero_pagina(128); 
            
            should_int(page1) be equal to(0);
            should_int(page2) be equal to(1);
            should_int(page3) be equal to(2);
        } end
        
        it("maneja addresses grandes") {
            int page = calcular_numero_pagina(1024);
            should_int(page) be equal to(16);
        } end
    } end
    
    describe("calcular_offset_pagina") {
        it("calcula offset 0 para el inicio de página") {
            int offset1 = calcular_offset_pagina(0);
            int offset2 = calcular_offset_pagina(64);
            int offset3 = calcular_offset_pagina(128);
            
            should_int(offset1) be equal to(0);
            should_int(offset2) be equal to(0);
            should_int(offset3) be equal to(0);
        } end
        
        it("calcula offsets intermedios correctamente") {
            int offset1 = calcular_offset_pagina(32);
            int offset2 = calcular_offset_pagina(96);
            
            should_int(offset1) be equal to(32);
            should_int(offset2) be equal to(32);
        } end
        
        it("calcula el último offset de una página") {
            int offset1 = calcular_offset_pagina(63);
            int offset2 = calcular_offset_pagina(127);
            
            should_int(offset1) be equal to(63);
            should_int(offset2) be equal to(63);
        } end
    } end
    
    describe("calcular_direccion_fisica") {
        it("calcula dirección física correctamente") {
            int direccion1 = calcular_direccion_fisica(0, 0);
            int direccion2 = calcular_direccion_fisica(0, 32);
            int direccion3 = calcular_direccion_fisica(1, 0);
            int direccion4 = calcular_direccion_fisica(2, 16);
            
            should_int(direccion1) be equal to(0);
            should_int(direccion2) be equal to(32);
            should_int(direccion3) be equal to(64);
            should_int(direccion4) be equal to(144);
        } end
        
        it("maneja marcos y offsets máximos") {
            int direccion = calcular_direccion_fisica(10, 63);
            should_int(direccion) be equal to(703);
        } end
    } end
    
    describe("crear_file_tag") {
        it("crea correctamente un file_tag") {
            t_file_tag ft = crear_file_tag("test.txt", "v1");
            
            should_string(ft.file_name) be equal to("test.txt");
            should_string(ft.tag) be equal to("v1");
            
            free(ft.file_name);
            free(ft.tag);
        } end
        
        it("crea copias independientes de los strings") {
            char original_file[] = "archivo.txt";
            char original_tag[] = "tag1";
            
            t_file_tag ft = crear_file_tag(original_file, original_tag);
            
            strcpy(original_file, "modificado");
            strcpy(original_tag, "modificado");
            
            should_string(ft.file_name) be equal to("archivo.txt");
            should_string(ft.tag) be equal to("tag1");
            
            free(ft.file_name);
            free(ft.tag);
        } end
    } end
}