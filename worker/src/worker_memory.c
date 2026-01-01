#include "./worker.h"

// ========================== FUNCIONES DE MEMORIA INTERNA ==========================

t_page* obtener_pagina(char* file_name, char* tag, int page_number) {
    pthread_mutex_lock(&internal_memory->memory_mutex);
    
    char* key = crear_file_tag_key(file_name, tag);
    t_page_table* page_table = dictionary_get(internal_memory->page_tables, key);
    
    if(!page_table) {
        // Crear nueva tabla de paginas
        page_table = malloc(sizeof(t_page_table));
        page_table->pages = list_create();
        pthread_mutex_init(&page_table->mutex, NULL);
        dictionary_put(internal_memory->page_tables, key, page_table);
    }
    
    // Buscar la pagina en la tabla
    t_page* page = NULL;
    for(int i = 0; i < list_size(page_table->pages); i++) {
        t_page* p = list_get(page_table->pages, i);
        if(p->page_number == page_number && p->present) {
            page = p;
            // Actualizar bit de uso para CLOCK-M en caso de hit
            if(strcmp(ALGORITMO_REEMPLAZO, "CLOCK-M") == 0) {
                page->clock_bit = 1;
            }
            page->last_access = time(NULL); // También para LRU
            break;
        }
    }
    
    if(!page) {
        // Log obligatorio: Memoria Miss
        log_info(log_worker, "Query %d: - Memoria Miss - File: %s - Tag: %s - Pagina: %d", 
                 current_query_id, file_name, tag, page_number);
        
        // Página no esta en memoria, cargar desde storage
        page = cargar_pagina_desde_storage(file_name, tag, page_number);
        if(page) {
            // Verificar si hay espacio en memoria
            if(internal_memory->used_pages >= internal_memory->total_pages) {
                // Seleccionar victima y reemplazar
                t_page* victima = seleccionar_victima();
                if(victima) {
                    log_info(log_worker, "## Query %d: Se reemplaza la página %s:%s/%d por la %s:%s/%d", 
                             current_query_id, 
                             victima->file_tag.file_name, victima->file_tag.tag, victima->page_number,
                             file_name, tag, page_number);
                    
                    if(victima->modified) {
                        escribir_pagina_en_storage(victima);
                    }
                    
                    log_info(log_worker, "Query %d: Se libera el Marco: %d perteneciente al - File: %s - Tag: %s", 
                             current_query_id, victima->frame_number, victima->file_tag.file_name, victima->file_tag.tag);
                    
                    // Guardar el frame_number de la victima antes de liberarla
                    int frame_reusado = victima->frame_number;
                    
                    // CORRECCION: Remover victima de SU tabla de paginas (no de la tabla de la pagina entrante)
                    char* key_victima = crear_file_tag_key(victima->file_tag.file_name, victima->file_tag.tag);
                    t_page_table* pt_victima = dictionary_get(internal_memory->page_tables, key_victima);
                    if(pt_victima) {
                        list_remove_element(pt_victima->pages, victima);
                    }
                    free(key_victima);
                    
                    // Remover de physical pages (esto estaba bien)
                    // IMPORTANTE: Necesitamos encontrar el índice de la victima para ajustar clock_pointer
                    int indice_victima = -1;
                    for(int idx = 0; idx < list_size(internal_memory->physical_pages); idx++) {
                        if(list_get(internal_memory->physical_pages, idx) == victima) {
                            indice_victima = idx;
                            break;
                        }
                    }
                    
                    bool removed_from_physical = list_remove_element(internal_memory->physical_pages, victima);
                    
                    // Ajustar clock_pointer si se removio un elemento antes o en la posicion actual
                    if(strcmp(ALGORITMO_REEMPLAZO, "CLOCK-M") == 0 && indice_victima >= 0) {
                        if(indice_victima < internal_memory->clock_pointer) {
                            // Si removimos un elemento antes del puntero, decrementar
                            internal_memory->clock_pointer--;
                        } else if(indice_victima == internal_memory->clock_pointer) {
                            // Si removimos el elemento actual, el puntero ya apunta al siguiente
                            // No hacer nada, pero asegurar que no este fuera de rango
                            if(internal_memory->clock_pointer >= list_size(internal_memory->physical_pages)) {
                                internal_memory->clock_pointer = 0;
                            }
                        }
                    }
                    
                    // Liberar el marco en la frame_table ANTES de liberar la victima
                    liberar_marco(frame_reusado);
                    
                    // LIBERAR MEMORIA DE LA VICTIMA para evitar memory leak
                    free(victima->file_tag.file_name);
                    free(victima->file_tag.tag);
                    free(victima->content);
                    free(victima);
                    
                    // Ahora asignar el marco reutilizado a la nueva página
                    page->frame_number = frame_reusado;
                    
                    // Marcar el marco como ocupado nuevamente
                    internal_memory->frame_table[frame_reusado] = true;
                    
                    // NO incrementamos used_pages porque estamos reemplazando (1 sale, 1 entra)
                    
                    log_info(log_worker, "Query %d: Victima removida (tabla: SI, fisica: %d), marco %d reutilizado", 
                             current_query_id, removed_from_physical, frame_reusado);
                } else {
                    // Asignar nuevo marco
                    page->frame_number = asignar_marco_libre();
                    internal_memory->used_pages++;
                }
            } else {
                // Asignar marco libre
                page->frame_number = asignar_marco_libre();
                internal_memory->used_pages++;
            }
            
            page->present = true;
            list_add(page_table->pages, page);
            list_add(internal_memory->physical_pages, page);
            
            // Log obligatorio: Bloque ingresado en Memoria y Asignar Marco
            log_info(log_worker, "Query %d: - Memoria Add - File: %s - Tag: %s - Pagina: %d - Marco: %d", 
                     current_query_id, file_name, tag, page_number, page->frame_number);
            log_info(log_worker, "Query %d: Se asigna el Marco: %d a la Página: %d perteneciente al - File: %s - Tag: %s", 
                     current_query_id, page->frame_number, page_number, file_name, tag);
        }
    }
    
    free(key);
    pthread_mutex_unlock(&internal_memory->memory_mutex);
    return page;
}

t_page* cargar_pagina_desde_storage(char* file_name, char* tag, int page_number) {
    log_info(log_worker, "Cargando página %d del file %s:%s desde storage", page_number, file_name, tag);
    
    void* data = recibir_read_block_storage(file_name, tag, page_number);
    if(!data) {
        log_warning(log_worker, "No se pudo cargar página desde storage");
        return NULL;
    }
    
    t_page* page = malloc(sizeof(t_page));
    page->file_tag = crear_file_tag(file_name, tag);
    page->page_number = page_number;
    page->modified = false;
    page->present = false;
    page->content = malloc(BLOCK_SIZE);
    memcpy(page->content, data, BLOCK_SIZE);
    page->last_access = time(NULL);
    page->clock_bit = 1;
    page->frame_number = -1; // Se asigna en obtener_pagina
    
    free(data);
    return page;
}

bool escribir_pagina_en_storage(t_page* page) {
    log_info(log_worker, "Escribiendo página %d del file %s:%s en storage", 
             page->page_number, page->file_tag.file_name, page->file_tag.tag);
    
    return enviar_write_block_storage(page->file_tag.file_name, page->file_tag.tag, 
                                      page->page_number, page->content);
}

t_page* seleccionar_victima() {
    if(strcmp(ALGORITMO_REEMPLAZO, "LRU") == 0) {
        return algoritmo_lru_seleccionar_victima();
    } else if(strcmp(ALGORITMO_REEMPLAZO, "CLOCK-M") == 0) {
        return algoritmo_clock_seleccionar_victima();
    }
    return NULL;
}

void algoritmo_lru_actualizar(t_page* page) {
    page->last_access = time(NULL);
    // Para CLOCK-M, tambien setear el bit de uso
    if(strcmp(ALGORITMO_REEMPLAZO, "CLOCK-M") == 0) {
        page->clock_bit = 1;
    }
}

t_page* algoritmo_lru_seleccionar_victima() {
    t_page* victima = NULL;
    time_t oldest_time = 0;
    bool first_page = true;
    
    for(int i = 0; i < list_size(internal_memory->physical_pages); i++) {
        t_page* page = list_get(internal_memory->physical_pages, i);
        if(page->present) {
            if(first_page || page->last_access < oldest_time) {
                oldest_time = page->last_access;
                victima = page;
                first_page = false;
            }
        }
    }
    
    if(victima) {
        log_info(log_worker, "LRU: Seleccionada víctima página %d del file %s:%s", 
                 victima->page_number, victima->file_tag.file_name, victima->file_tag.tag);
    }
    
    return victima;
}

t_page* algoritmo_clock_seleccionar_victima() {
    int total_pages = list_size(internal_memory->physical_pages);
    if(total_pages == 0) {
        return NULL;
    }
    
    t_page* victima = NULL;
    int inicio_busqueda = internal_memory->clock_pointer;
    
    // PASO 1: Buscar página con (use=0, modified=0)
    log_info(log_worker, "CLOCK-M Paso 1: Buscando página (use=0, modified=0)");
    for(int i = 0; i < total_pages; i++) {
        if(internal_memory->clock_pointer >= total_pages) {
            internal_memory->clock_pointer = 0;
        }
        
        t_page* page = list_get(internal_memory->physical_pages, internal_memory->clock_pointer);
        
        if(page->present && page->clock_bit == 0 && !page->modified) {
            victima = page;
            log_info(log_worker, "CLOCK-M: Encontrada víctima óptima (0,0) - File: %s:%s - Página: %d", 
                     page->file_tag.file_name, page->file_tag.tag, page->page_number);
            internal_memory->clock_pointer++;
            return victima;
        }
        
        internal_memory->clock_pointer++;
    }
    
    // PASO 2: Buscar página con (use=0, modified=1), limpiando bits de uso
    log_info(log_worker, "CLOCK-M Paso 2: Buscando página (use=0, modified=1) y limpiando bits de uso");
    for(int i = 0; i < total_pages; i++) {
        if(internal_memory->clock_pointer >= total_pages) {
            internal_memory->clock_pointer = 0;
        }
        
        t_page* page = list_get(internal_memory->physical_pages, internal_memory->clock_pointer);
        
        if(page->present) {
            if(page->clock_bit == 0 && page->modified) {
                victima = page;
                log_info(log_worker, "CLOCK-M: Encontrada víctima (0,1) - File: %s:%s - Página: %d (requiere escritura)", 
                         page->file_tag.file_name, page->file_tag.tag, page->page_number);
                internal_memory->clock_pointer++;
                return victima;
            } else if(page->clock_bit == 1) {
                page->clock_bit = 0; // Dar segunda oportunidad
            }
        }
        
        internal_memory->clock_pointer++;
    }
    
    // PASO 3: Repetir Paso 1 - Buscar página con (use=0, modified=0)
    log_info(log_worker, "CLOCK-M Paso 3: Segunda búsqueda de página (use=0, modified=0)");
    for(int i = 0; i < total_pages; i++) {
        if(internal_memory->clock_pointer >= total_pages) {
            internal_memory->clock_pointer = 0;
        }
        
        t_page* page = list_get(internal_memory->physical_pages, internal_memory->clock_pointer);
        
        if(page->present && page->clock_bit == 0 && !page->modified) {
            victima = page;
            log_info(log_worker, "CLOCK-M: Encontrada víctima (0,0) en paso 3 - File: %s:%s - Página: %d", 
                     page->file_tag.file_name, page->file_tag.tag, page->page_number);
            internal_memory->clock_pointer++;
            return victima;
        }
        
        internal_memory->clock_pointer++;
    }
    
    // PASO 4: Repetir Paso 2 - Buscar página con (use=0, modified=1)
    log_info(log_worker, "CLOCK-M Paso 4: Segunda búsqueda de página (use=0, modified=1)");
    for(int i = 0; i < total_pages; i++) {
        if(internal_memory->clock_pointer >= total_pages) {
            internal_memory->clock_pointer = 0;
        }
        
        t_page* page = list_get(internal_memory->physical_pages, internal_memory->clock_pointer);
        
        if(page->present && page->clock_bit == 0 && page->modified) {
            victima = page;
            log_info(log_worker, "CLOCK-M: Encontrada víctima (0,1) en paso 4 - File: %s:%s - Página: %d", 
                     page->file_tag.file_name, page->file_tag.tag, page->page_number);
            internal_memory->clock_pointer++;
            return victima;
        }
        
        internal_memory->clock_pointer++;
    }
    
    // Si después de 4 pasos no encuentra víctima, tomar la primera disponible
    log_warning(log_worker, "CLOCK-M: No se encontró víctima en 4 pasos, seleccionando primera página disponible");
    for(int i = 0; i < total_pages; i++) {
        t_page* page = list_get(internal_memory->physical_pages, i);
        if(page->present) {
            return page;
        }
    }
    
    return NULL;
}

void simular_retardo_memoria() {
    usleep(RETARDO_MEMORIA * 1000);
}

void flush_todas_las_paginas_modificadas() {
    pthread_mutex_lock(&internal_memory->memory_mutex);
    
    log_info(log_worker, "Query %d: Iniciando FLUSH de todas las páginas modificadas", current_query_id);
    
    int paginas_escritas = 0;
    for(int i = 0; i < list_size(internal_memory->physical_pages); i++) {
        t_page* page = list_get(internal_memory->physical_pages, i);
        if(page->modified && page->present) {
            log_info(log_worker, "Query %d: FLUSH - File: %s - Tag: %s - Página: %d", 
                     current_query_id, page->file_tag.file_name, page->file_tag.tag, page->page_number);
            
            if(escribir_pagina_en_storage(page)) {
                page->modified = false;
                paginas_escritas++;
            } else {
                log_error(log_worker, "Query %d: Error al escribir página %d de %s:%s en Storage", 
                         current_query_id, page->page_number, page->file_tag.file_name, page->file_tag.tag);
            }
        }
    }
    
    log_info(log_worker, "Query %d: FLUSH completado. %d páginas escritas en Storage", 
             current_query_id, paginas_escritas);
    
    pthread_mutex_unlock(&internal_memory->memory_mutex);
}

// ========================== COMUNICACIÓN CON STORAGE ==========================

bool enviar_create_storage(char* file_name, char* tag) {
    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer, current_query_id);
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    
    t_paquete* paquete = crear_super_paquete(CREACION_FILE, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    // Recibir confirmacion del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == CREACION_FILE) {
        t_buffer* buf_resp = recibir_buffer(fd_storage);
        char* mensaje = extraer_string_buffer(buf_resp);
        
        bool exito = (strcmp(mensaje, "OK") == 0);
        if(exito) {
            log_info(log_worker, "CREATE confirmado por storage para %s:%s", file_name, tag);
        } else {
            log_error(log_worker, "Error en CREATE para %s:%s: %s", file_name, tag, mensaje);
        }
        free(mensaje);
        free(buf_resp); // Liberar buffer al final
        return exito;
    } else {
        log_error(log_worker, "Error en CREATE para %s:%s", file_name, tag);
        return false;
    }
}

bool enviar_truncate_storage(char* file_name, char* tag, int size) {
    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer, current_query_id);
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    cargar_int_al_buff(buffer, size);
    
    t_paquete* paquete = crear_super_paquete(TRUNCADO_ARCHIVO, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    // Recibir respuesta del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == TRUNCADO_ARCHIVO) {
        t_buffer* buf_resp = recibir_buffer(fd_storage);
        char* mensaje = extraer_string_buffer(buf_resp);
        
        bool exito = (strcmp(mensaje, "OK") == 0);
        if(exito) {
            log_info(log_worker, "TRUNCATE confirmado por storage para %s:%s tamaño %d", file_name, tag, size);
        } else {
            log_warning(log_worker, "TRUNCATE falló en storage: %s", mensaje);
        }
        free(mensaje);
        free(buf_resp); // Liberar buffer al final
        return exito;
    }
    return false;
}

bool enviar_tag_storage(char* src_file, char* src_tag, char* dest_file, char* dest_tag) {
    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer, current_query_id);
    cargar_string_al_buff(buffer, src_file);
    cargar_string_al_buff(buffer, src_tag);
    cargar_string_al_buff(buffer, dest_file);
    cargar_string_al_buff(buffer, dest_tag);
    
    t_paquete* paquete = crear_super_paquete(TAG_FILE, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    // Recibir respuesta del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == TAG_FILE) {
        t_buffer* buf_resp = recibir_buffer(fd_storage);
        char* mensaje = extraer_string_buffer(buf_resp);
        
        bool exito = (strcmp(mensaje, "OK") == 0);
        if(exito) {
            log_info(log_worker, "TAG confirmado por storage: %s:%s -> %s:%s", src_file, src_tag, dest_file, dest_tag);
        } else {
            log_warning(log_worker, "TAG falló en storage: %s", mensaje);
        }
        free(mensaje);
        free(buf_resp); // Liberar buffer al final
        return exito;
    }
    return false;
}

bool enviar_commit_storage(char* file_name, char* tag) {
    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer, current_query_id);
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    
    
    log_info(log_worker, "Enviando COMMIT a storage para %s:%s", file_name, tag);
    t_paquete* paquete = crear_super_paquete(COMMIT_TAG, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);

    log_info(log_worker, "Enviado COMMIT a storage para %s:%s", file_name, tag);
    
    // Recibir respuesta del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == COMMIT_TAG) {
        t_buffer* buf_resp = recibir_buffer(fd_storage);
        char* mensaje = extraer_string_buffer(buf_resp);
        
        bool exito = (strcmp(mensaje, "OK") == 0 || strcmp(mensaje, "NO") == 0);
        if(strcmp(mensaje, "OK") == 0) {
            log_info(log_worker, "COMMIT confirmado por storage para %s:%s", file_name, tag);
        } else if(strcmp(mensaje, "NO") == 0) {
            log_info(log_worker, "COMMIT: Tag %s:%s ya estaba commited (sin cambios)", file_name, tag);
        } else {
            log_error(log_worker, "COMMIT falló en storage: %s", mensaje);
        }
        free(mensaje);
        free(buf_resp); // Liberar buffer al final
        return exito;
    } else {
        log_error(log_worker, "COMMIT falló en storage");
        return false;
    }
}

bool enviar_delete_storage(char* file_name, char* tag) {
    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer, current_query_id);
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    
    log_info(log_worker, "Enviando DELETE a storage para %s:%s", file_name, tag);
    t_paquete* paquete = crear_super_paquete(ELIMINAR_TAG, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    // Recibir respuesta del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == ELIMINAR_TAG) {
        t_buffer* buf_resp = recibir_buffer(fd_storage);
        char* mensaje = extraer_string_buffer(buf_resp);
        
        bool exito = (strcmp(mensaje, "OK") == 0);
        if(exito) {
            log_info(log_worker, "DELETE confirmado por storage para %s:%s", file_name, tag);
        } else {
            log_error(log_worker, "DELETE falló en storage: %s", mensaje);
        }
        free(mensaje);
        free(buf_resp); // Liberar buffer al final
        return exito;
    } else {
        log_error(log_worker, "DELETE falló en storage: respuesta inválida");
        return false;
    }
}

bool enviar_write_block_storage(char* file_name, char* tag, int block_number, void* data) {
    //log_info(log_worker, "data q mano a escribir: %s", data);
    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer, current_query_id);
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    cargar_int_al_buff(buffer, block_number);
    // Cargar los datos del bloque (BLOCK_SIZE bytes) usando agregar_al_buffer
    agregar_al_buffer(buffer, data, BLOCK_SIZE);
    
    t_paquete* paquete = crear_super_paquete(ESCRITURA_BLOQUE, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    log_info(log_worker, "WRITE_BLOCK enviado a storage para %s:%s bloque %d", file_name, tag, block_number);
    
    // Esperar respuesta del storage
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == ESCRITURA_BLOQUE) {
        t_buffer* buffer_respuesta = recibir_buffer(fd_storage);
        char* mensaje = extraer_string_buffer(buffer_respuesta);
        
        bool exito = (strcmp(mensaje, "OK") == 0);
        if(exito) {
            log_info(log_worker, "WRITE_BLOCK confirmado por storage para %s:%s bloque %d", file_name, tag, block_number);
        } else {
            log_error(log_worker, "WRITE_BLOCK falló: %s", mensaje);
        }
        
        free(mensaje);
        free(buffer_respuesta); // IMPORTANTE: Liberar buffer DESPUES de mensaje
        return exito;
    } else {
        log_error(log_worker, "Error al recibir respuesta de WRITE_BLOCK");
        return false;
    }
}

void* recibir_read_block_storage(char* file_name, char* tag, int block_number) {
    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer, current_query_id);
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    cargar_int_al_buff(buffer, block_number);
    
    t_paquete* paquete = crear_super_paquete(LECTURA_BLOQUE, buffer);
    enviar_paquete(paquete, fd_storage);
    eliminar_paquete(paquete);
    
    // Recibir respuesta del storage (ahora está implementado)
    op_code respuesta = recibir_operacion(fd_storage);
    if(respuesta == LECTURA_BLOQUE) {
        t_buffer* buffer_respuesta = recibir_buffer(fd_storage);
        char* mensaje = extraer_string_buffer(buffer_respuesta);
        
        // Verificar si es un mensaje de error
        if (strcmp(mensaje, "File inexistente") == 0 || 
            strcmp(mensaje, "Tag inexistente") == 0 ||
            strcmp(mensaje, "Bloque logico no asignado / Lectura fuera de limite") == 0) {
            log_error(log_worker, "Query %d: Error en READ_BLOCK para %s:%s bloque %d: %s", 
                     current_query_id, file_name, tag, block_number, mensaje);
            free(mensaje);
            free(buffer_respuesta); // Liberar al final
            return NULL;
        }
        
        // Es una lectura exitosa, mensaje contiene los datos del bloque
        void* data = malloc(BLOCK_SIZE);
        memcpy(data, mensaje, BLOCK_SIZE);
        
        free(mensaje);
        free(buffer_respuesta); // Liberar al final
        log_info(log_worker, "READ_BLOCK recibido desde storage para %s:%s bloque %d", file_name, tag, block_number);
        return data;
    } else {
        log_error(log_worker, "Error en READ_BLOCK para %s:%s bloque %d", file_name, tag, block_number);
        return NULL;
    }
}

// ========================== COMUNICACION CON MASTER ==========================

void enviar_resultado_read_master(char* file_name, char* tag, void* data, int size) {
    // Convertir data a string para enviar al Master
    char* contenido = malloc(size + 1);
    memcpy(contenido, data, size);
    contenido[size] = '\0';
    
    t_buffer* buffer = crear_buffer();
    cargar_int_al_buff(buffer, current_query_id);
    cargar_string_al_buff(buffer, file_name);
    cargar_string_al_buff(buffer, tag);
    cargar_string_al_buff(buffer, contenido);
    
    t_paquete* paquete = crear_super_paquete(WORKER_ENVIA_LECTURA, buffer);
    enviar_paquete(paquete, fd_master);
    eliminar_paquete(paquete);
    
    free(contenido);
    log_info(log_worker, "Resultado de READ enviado al Master para %s:%s (%d bytes)", file_name, tag, size);
}


char* crear_file_tag_key(char* file_name, char* tag) {
    int len = strlen(file_name) + strlen(tag) + 2; // +1 para ':' y +1 para '\0'
    char* key = malloc(len);
    sprintf(key, "%s:%s", file_name, tag);
    return key;
}

t_file_tag crear_file_tag(char* file_name, char* tag) {
    t_file_tag ft;
    ft.file_name = strdup(file_name);
    ft.tag = strdup(tag);
    return ft;
}

int calcular_numero_pagina(int address) {
    if (address < 0) return -1;
    return address / BLOCK_SIZE;
}

int calcular_offset_pagina(int address) {
    if (address < 0) return -1;
    return address % BLOCK_SIZE;
}

void liberar_instruccion(t_instruction* instruccion) {
    if (!instruccion) return;
    if(instruccion->file_name) free(instruccion->file_name);
    if(instruccion->tag) free(instruccion->tag);
    if(instruccion->content) free(instruccion->content);
    if(instruccion->dest_file_name) free(instruccion->dest_file_name);
    if(instruccion->dest_tag) free(instruccion->dest_tag);
    free(instruccion);
}

void procesar_query_hilo(void* args_ptr) {
    typedef struct {
        int id;
        char* path;
    } query_args;
    
    query_args* args = (query_args*)args_ptr;
    procesar_query(args->id, args->path);
    free(args->path);
    free(args);
}

// ========================== MANEJO DE MARCOS ==========================

int asignar_marco_libre() {
    for(int i = 0; i < internal_memory->total_pages; i++) {
        if(!internal_memory->frame_table[i]) {
            internal_memory->frame_table[i] = true; // Marcar como ocupado
            return i;
        }
    }
    return -1; // No hay marcos libres
}

void liberar_marco(int frame_number) {
    if(frame_number >= 0 && frame_number < internal_memory->total_pages) {
        internal_memory->frame_table[frame_number] = false; // Marcar como libre
    }
}

int calcular_direccion_fisica(int frame_number, int offset) {
    if (frame_number < 0 || offset < 0) return -1;
    return (frame_number * BLOCK_SIZE) + offset;
}