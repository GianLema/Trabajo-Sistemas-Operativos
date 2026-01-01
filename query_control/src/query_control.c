#include <query_control.h>

int fd_master = -1;


void conectarse_a_master(char* path_query, int prioridad){
    log_info(log_qc,"Conectandose al Master");
    fd_master = crear_conexion(IP_MASTER,PUERTO_MASTER);
    t_buffer* buffer_conectar_a_master = crear_buffer();
    char* msj = "HOLIS MASTER, SOY QUERY CONTROL";
    cargar_string_al_buff(buffer_conectar_a_master,msj);    
    cargar_string_al_buff(buffer_conectar_a_master,path_query);    
    cargar_int_al_buff(buffer_conectar_a_master,prioridad);
    t_paquete* paquete_conectar_a_master = crear_super_paquete(HANDSHAKE_MASTER_QUERY_CONTROL, buffer_conectar_a_master);
    enviar_paquete(paquete_conectar_a_master, fd_master);
    eliminar_paquete(paquete_conectar_a_master);
    bool key = true;
    while (key){
        op_code cod_op = recibir_operacion(fd_master);
        log_info(log_qc, "Codigo de operacion recibido de master: %d",cod_op);
        switch (cod_op) {
        // Si tu Master re-envía lecturas con este opcode, ajustá el nombre
        case QC_MSG_READ: {
            t_buffer* buffer_msg_read = recibir_buffer(fd_master);
            if (!buffer_msg_read) { cerrar_ordenado("Error recibiendo lectura"); }
            char* contenido = extraer_string_buffer(buffer_msg_read);
            log_info(log_qc, "## Lectura realizada: %s", contenido); // obligatorio
            free(contenido);
            free(buffer_msg_read);
        } break;

        case QC_MSG_FIN: {
            t_buffer* buffer_msg_fin = recibir_buffer(fd_master);
            if (!buffer_msg_fin) { cerrar_ordenado("Error recibiendo fin"); }
            char* motivo = extraer_string_buffer(buffer_msg_fin);
            free(buffer_msg_fin);
            cerrar_ordenado(motivo); // log + close
        } break;

        case HANDSHAKE_MASTER_QUERY_CONTROL:
            t_buffer* buffer_hand_master_qc = recibir_buffer(fd_master);
            char*  mensaje_recibido = extraer_string_buffer(buffer_hand_master_qc);
            log_info(log_qc,"Recibi mensaje del Master con %s", mensaje_recibido);
            free(buffer_hand_master_qc);
            free(mensaje_recibido);
            break;
        case -1:
            log_error(log_qc, "Cerrando conexion con el master %d",fd_master);
            key=false;
            break;
        default:
            log_warning(log_qc,"Respuesta desconocida del master");
            break;
        }
    }
}

void iniciar_config_query_control(char* ruta_config){
    config_qc = config_create(ruta_config);
    if(config_qc  == NULL){
        printf("No se pudo encontrar la config\n");
        exit(EXIT_FAILURE);
    }
    IP_MASTER = config_get_string_value(config_qc, "IP_MASTER");
    PUERTO_MASTER = config_get_string_value(config_qc, "PUERTO_MASTER");
    LOG_LEVEL = config_get_int_value(config_qc, "LOG_LEVEL");
}

void inicializar_query_control(char* config){
    log_qc = iniciar_logger("query_control.log","QUERY_CONTROL", LOG_LEVEL_INFO);
    iniciar_config_query_control(config);
}

void cerrar_ordenado(const char* motivo) {
    if (motivo) log_info(log_qc, "## Query Finalizada - %s", motivo); // log obligatorio
    if (fd_master != -1) { close(fd_master); fd_master = -1; }
    if (config_qc) { config_destroy(config_qc); config_qc = NULL; }
    if (log_qc) { log_destroy(log_qc); log_qc = NULL; }
    exit(0);
}
void sig_handler(int sig) { (void)sig; cerrar_ordenado("Interrumpida por usuario"); }

int main(int argc, char* argv[]) {
    if(argc < 4) {
        printf("Uso: %s <archivo_config> <query_file> <prioridad>\n", argv[0]);
        printf("Ejemplo: %s query_ctrl.config query_test_lru.txt 5\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    inicializar_query_control(argv[1]);
    log_info(log_qc,"Iniciado Query Control");
    conectarse_a_master(argv[2], atoi(argv[3]));
    
    return 0;
}