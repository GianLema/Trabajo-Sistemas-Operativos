#include "../src/master.h"
#include <cspecs/cspec.h> // ⚠️ debe ir siempre al final de los includes

// -----------------------------------------------------------------------------
// Tests de integración del módulo MASTER
// -----------------------------------------------------------------------------

context (test_master_integracion) {

    describe("Creación de estructuras básicas") {

        it("crea correctamente un worker libre") {
            // --- Arrange ---
            int fake_socket_w = 42;

            // --- Act ---
            t_worker* w = crear_estructura_worker(1, fake_socket_w);

            // --- Assert ---
            should_ptr(w) not be null;
            should_int(w->id) be equal to(1);
            should_int(w->socket_worker) be equal to(42);
            should_bool(w->libre) be equal to(true);

            // --- Cleanup ---
            free(w);
        } end


        it("crea correctamente una query en estado READY") {
            // --- Arrange ---
            int fake_socket_qc = 99;
            char* path = "/tmp/query1.sql";

            // --- Act ---
            t_query* q = crear_estructura_query(path, 5, fake_socket_qc);

            // --- Assert ---
            should_ptr(q) not be null;
            should_string(q->path) be equal to(path);
            should_int(q->prioridad) be equal to(5);
            should_int(q->socket_qc) be equal to(99);
            should_int(q->estado) be equal to(READY);

            // --- Cleanup ---
            free(q);
        } end

    } end


    describe("Gestión de colas del planificador") {

        it("inserta correctamente una query en la cola READY") {
            // --- Arrange ---
            cola_ready = queue_create();
            sem_init(&sem_query_ready, 0, 0);
            pthread_mutex_init(&mutex_cola_ready, NULL);

            t_query* q = crear_estructura_query("/tmp/q2.sql", 3, 88);

            // --- Act ---
            pthread_mutex_lock(&mutex_cola_ready);
            queue_push(cola_ready, q);
            pthread_mutex_unlock(&mutex_cola_ready);

            // --- Assert ---
            should_int(queue_size(cola_ready)) be equal to(1);
            t_query* q_peek = queue_peek(cola_ready);
            should_string(q_peek->path) be equal to("/tmp/q2.sql");

            // --- Cleanup ---
            queue_destroy_and_destroy_elements(cola_ready, free);
            sem_destroy(&sem_query_ready);
            pthread_mutex_destroy(&mutex_cola_ready);
        } end


        it("permite mover una query de READY a EXEC correctamente") {
            // --- Arrange ---
            cola_ready = queue_create();
            cola_exec  = queue_create();

            t_query* q = crear_estructura_query("/tmp/q3.sql", 2, 77);
            queue_push(cola_ready, q);

            // --- Act ---
            pthread_mutex_init(&mutex_cola_ready, NULL);
            pthread_mutex_init(&mutex_cola_exec, NULL);

            pthread_mutex_lock(&mutex_cola_ready);
            t_query* q_exec = queue_pop(cola_ready);
            pthread_mutex_unlock(&mutex_cola_ready);

            q_exec->estado = EXEC;

            pthread_mutex_lock(&mutex_cola_exec);
            queue_push(cola_exec, q_exec);
            pthread_mutex_unlock(&mutex_cola_exec);

            // --- Assert ---
            should_int(queue_size(cola_ready)) be equal to(0);
            should_int(queue_size(cola_exec)) be equal to(1);

            t_query* q_test = queue_peek(cola_exec);
            should_int(q_test->estado) be equal to(EXEC);

            // --- Cleanup ---
            queue_destroy_and_destroy_elements(cola_ready, free);
            queue_destroy_and_destroy_elements(cola_exec, free);
            pthread_mutex_destroy(&mutex_cola_ready);
            pthread_mutex_destroy(&mutex_cola_exec);
        } end

    } end


    describe("Asignación de worker y query") {

        it("marca correctamente un worker como ocupado") {
            // --- Arrange ---
            t_worker* w = crear_estructura_worker(2, 50);
            t_query* q = crear_estructura_query("/tmp/q4.sql", 1, 33);

            // --- Act ---
            w->libre = false;
            w->id_query = q->id;
            q->estado = EXEC;

            // --- Assert ---
            should_bool(w->libre) be equal to(false);
            should_int(w->id_query) be equal to(q->id);
            should_int(q->estado) be equal to(EXEC);

            // --- Cleanup ---
            free(w);
            free(q);
        } end

    } end

    describe("Planificador FIFO - integración básica") {

        it("asigna correctamente las queries en orden FIFO a un worker") {
            // --- Arrange ---
            // Inicializo estructuras base
            lista_workers = list_create();
            lista_querys = list_create();
            cola_ready = queue_create();
            cola_exec  = queue_create();
            cola_workers_libres = queue_create();

            pthread_mutex_init(&mutex_lista_workers, NULL);
            pthread_mutex_init(&mutex_lista_querys, NULL);
            pthread_mutex_init(&mutex_cola_ready, NULL);
            pthread_mutex_init(&mutex_cola_exec, NULL);
            pthread_mutex_init(&mutex_workers_libres, NULL);
            sem_init(&sem_query_ready, 0, 0);
            sem_init(&sem_worker_disponible, 0, 0);

            // Creamos un solo worker libre
            t_worker* w1 = crear_estructura_worker(1, 100);
            queue_push(cola_workers_libres, w1);
            sem_post(&sem_worker_disponible);

            // Creamos dos queries
            t_query* q1 = crear_estructura_query("/tmp/q_fifo_1.sql", 1, 10);
            t_query* q2 = crear_estructura_query("/tmp/q_fifo_2.sql", 1, 11);

            queue_push(cola_ready, q1);
            queue_push(cola_ready, q2);
            sem_post(&sem_query_ready);
            sem_post(&sem_query_ready);

            // --- Act ---
            // Simulamos el comportamiento FIFO del planificador
            t_query* q_ready = queue_pop(cola_ready);
            t_worker* w_libre = queue_pop(cola_workers_libres);

            asignar_query_a_worker(q_ready, w_libre);

            // --- Assert ---
            should_int(q_ready->estado) be equal to(EXEC);
            should_bool(w_libre->libre) be equal to(false);
            should_int(w_libre->id_query) be equal to(q_ready->id);

            // La cola READY debería tener una sola query pendiente (q2)
            should_int(queue_size(cola_ready)) be equal to(1);
            t_query* q_restante = queue_peek(cola_ready);
            should_string(q_restante->path) be equal to("/tmp/q_fifo_2.sql");

            // --- Cleanup ---
            queue_destroy_and_destroy_elements(cola_ready, free);
            queue_destroy_and_destroy_elements(cola_exec, free);
            queue_destroy_and_destroy_elements(cola_workers_libres, free);
            list_destroy(lista_workers);
            list_destroy(lista_querys);

            pthread_mutex_destroy(&mutex_lista_workers);
            pthread_mutex_destroy(&mutex_lista_querys);
            pthread_mutex_destroy(&mutex_cola_ready);
            pthread_mutex_destroy(&mutex_cola_exec);
            pthread_mutex_destroy(&mutex_workers_libres);
            sem_destroy(&sem_query_ready);
            sem_destroy(&sem_worker_disponible);
        } end
    } end

    describe("Planificador FIFO - finalización de query") {

        it("mueve la query de EXEC a EXIT y libera el worker") {
            // --- Inicialización de listas globales ---
            lista_workers = list_create();
            lista_querys  = list_create();

            // --- Arrange ---
            cola_exec = queue_create();
            cola_exit = queue_create();
            cola_workers_libres = queue_create();

            pthread_mutex_init(&mutex_cola_exec, NULL);
            pthread_mutex_init(&mutex_cola_exit, NULL);
            pthread_mutex_init(&mutex_workers_libres, NULL);

            sem_init(&sem_worker_disponible, 0, 0);

            t_worker* w = crear_estructura_worker(1, 999);
            t_query* q = crear_estructura_query("/tmp/final.sql", 3, 44);

            // Simulamos que la query ya estaba en ejecución
            w->id_query = q->id;
            w->libre = false;
            q->estado = EXEC;
            queue_push(cola_exec, q);

            // --- Act ---
            // Simular que el worker terminó la query
            pthread_mutex_lock(&mutex_cola_exec);
            t_query* q_exit = queue_pop(cola_exec);
            pthread_mutex_unlock(&mutex_cola_exec);

            q_exit->estado = EXIT;
            w->libre = true;
            w->id_query = -1;

            pthread_mutex_lock(&mutex_cola_exit);
            queue_push(cola_exit, q_exit);
            pthread_mutex_unlock(&mutex_cola_exit);

            pthread_mutex_lock(&mutex_workers_libres);
            queue_push(cola_workers_libres, w);
            pthread_mutex_unlock(&mutex_workers_libres);

            // --- Assert ---
            should_int(queue_size(cola_exec)) be equal to(0);
            should_int(queue_size(cola_exit)) be equal to(1);
            should_int(queue_size(cola_workers_libres)) be equal to(1);

            t_query* q_test = queue_peek(cola_exit);
            should_int(q_test->estado) be equal to(EXIT);

            t_worker* w_test = queue_peek(cola_workers_libres);
            should_bool(w_test->libre) be equal to(true);
            should_int(w_test->id_query) be equal to(-1);

            // --- Cleanup ---
            queue_destroy_and_destroy_elements(cola_exec, free);
            queue_destroy_and_destroy_elements(cola_exit, free);
            queue_destroy_and_destroy_elements(cola_workers_libres, free);

            pthread_mutex_destroy(&mutex_cola_exec);
            pthread_mutex_destroy(&mutex_cola_exit);
            pthread_mutex_destroy(&mutex_workers_libres);
            sem_destroy(&sem_worker_disponible);

            list_destroy(lista_workers);
            list_destroy(lista_querys);

        } end
    } end
}

