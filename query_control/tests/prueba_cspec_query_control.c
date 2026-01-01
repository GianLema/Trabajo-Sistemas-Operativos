#include "../src/query_control.h"
#include <cspecs/cspec.h>

// -----------------------------------------------------------------------------
// MOCKS Y VARIABLES GLOBALES PARA TEST
// -----------------------------------------------------------------------------

bool mock_enviar_paquete_llamado = false;
bool mock_recibir_buffer_llamado = false;
bool mock_recibir_operacion_llamado = false;

int mock_socket_fake = 999;

// Hacemos visible el fd_master del módulo
// extern int fd_master;

// -----------------------------------------------------------------------------
// MOCKS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// MOCKS
// -----------------------------------------------------------------------------

// En lugar de redefinir las funciones reales, definimos versiones mock
// con nombres distintos para evitar conflicto en el linker.

void mock_enviar_paquete(t_paquete* paquete, int socket) {
    mock_enviar_paquete_llamado = true;
    should_int(socket) be equal to(mock_socket_fake);
    should_int(paquete->codigo_operacion) be equal to(HANDSHAKE_MASTER_QUERY_CONTROL);
}

int mock_recibir_operacion(int socket) {
    mock_recibir_operacion_llamado = true;
    static int step = 0;
    step++;
    switch (step) {
        case 1: return HANDSHAKE_MASTER_QUERY_CONTROL;
        case 2: return QC_MSG_READ;
        case 3: return QC_MSG_FIN;
        default: return -1;
    }
}

t_buffer* mock_recibir_buffer(int socket) {
    mock_recibir_buffer_llamado = true;
    t_buffer* buffer = crear_buffer();
    cargar_string_al_buff(buffer, "Archivo.sql");
    cargar_string_al_buff(buffer, "tag");
    cargar_string_al_buff(buffer, "contenido_prueba");
    return buffer;
}


// -----------------------------------------------------------------------------
// TESTS
// -----------------------------------------------------------------------------

context (test_query_control) {

    describe("Inicialización de Query Control") {

        it("carga correctamente la configuración") {
            log_qc = iniciar_logger("qc_test.log", "QC_TEST", LOG_LEVEL_INFO);
            iniciar_config_query_control("./tests/config_qc_test.cfg");

            should_ptr(config_qc) not be null;
            should_string(IP_MASTER) be equal to("127.0.0.1");
            should_string(PUERTO_MASTER) be equal to("8000");

            config_destroy(config_qc);
            log_destroy(log_qc);
        } end

        it("inicializa correctamente logger y config") {
            inicializar_query_control("./tests/config_qc_test.cfg");
            should_ptr(log_qc) not be null;
            should_ptr(config_qc) not be null;
            log_destroy(log_qc);
            config_destroy(config_qc);
        } end
    } end


    describe("Handshake con Master") {

        it("envía correctamente el handshake inicial") {
            log_qc = iniciar_logger("qc_test.log", "QC_TEST", LOG_LEVEL_INFO);
            IP_MASTER = "127.0.0.1";
            PUERTO_MASTER = "8000";
            fd_master = mock_socket_fake;

            t_buffer* buffer = crear_buffer();
            cargar_string_al_buff(buffer, "HOLIS MASTER, SOY QUERY CONTROL");
            cargar_string_al_buff(buffer, "/tmp/q_test.sql");
            cargar_int_al_buff(buffer, 5);

            t_paquete* paquete = crear_super_paquete(HANDSHAKE_MASTER_QUERY_CONTROL, buffer);
            mock_enviar_paquete(paquete, fd_master);
            eliminar_paquete(paquete);
            should_bool(mock_enviar_paquete_llamado) be equal to(true);
            log_destroy(log_qc);
        } end
    } end


    describe("Manejo de mensajes del Master") {

        it("procesa correctamente un mensaje QC_MSG_READ") {
            log_qc = iniciar_logger("qc_test.log", "QC_TEST", LOG_LEVEL_INFO);
            IP_MASTER = "127.0.0.1";
            PUERTO_MASTER = "8000";
            fd_master = mock_socket_fake;

            t_buffer* buffer_msg_read = crear_buffer();
            cargar_string_al_buff(buffer_msg_read, "ArchivoA.sql");
            cargar_string_al_buff(buffer_msg_read, "TAGA");
            cargar_string_al_buff(buffer_msg_read, "ContenidoSimulado");

            char* file = extraer_string_buffer(buffer_msg_read);
            char* tag = extraer_string_buffer(buffer_msg_read);
            char* cont = extraer_string_buffer(buffer_msg_read);

            should_string(file) be equal to("ArchivoA.sql");
            should_string(tag) be equal to("TAGA");
            should_string(cont) be equal to("ContenidoSimulado");

            free(file);
            free(tag);
            free(cont);
            free(buffer_msg_read);
            log_destroy(log_qc);
        } end


        it("procesa correctamente un mensaje QC_MSG_FIN") {
            log_qc = iniciar_logger("qc_test.log", "QC_TEST", LOG_LEVEL_INFO);
            fd_master = mock_socket_fake;

            t_buffer* buffer_msg_fin = crear_buffer();
            cargar_string_al_buff(buffer_msg_fin, "Fin de ejecución normal");

            char* motivo = extraer_string_buffer(buffer_msg_fin);
            should_string(motivo) be equal to("Fin de ejecución normal");

            free(motivo);
            free(buffer_msg_fin);
            log_destroy(log_qc);
        } end
    } end
}
