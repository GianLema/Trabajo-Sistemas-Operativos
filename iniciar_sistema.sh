#!/bin/bash
# Script para iniciar todos los módulos del sistema

echo "======================================"
echo "  INICIANDO SISTEMA DE PRUEBA"
echo "======================================"

# Colores para output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Directorio base
BASE_DIR="/home/tobiasrf22/tp-2025-2c-BaaaaaastA-"

# Detener procesos existentes primero
echo -e "${YELLOW}[0/4] Deteniendo procesos existentes...${NC}"
pkill -9 -f "storage.*storage.*\.config" 2>/dev/null
pkill -9 -f "master.*master.*\.config" 2>/dev/null
pkill -9 -f "worker.*worker.*\.config" 2>/dev/null
rm -f $BASE_DIR/.system_pids
sleep 1
echo -e "${GREEN}✓ Procesos anteriores detenidos${NC}"

echo -e "${YELLOW}[1/4] Iniciando STORAGE...${NC}"
cd $BASE_DIR/storage/bin
./storage ../storage_stable.config &
STORAGE_PID=$!
echo -e "${GREEN}✓ Storage iniciado (PID: $STORAGE_PID)${NC}"
sleep 2

echo -e "${YELLOW}[2/4] Iniciando MASTER...${NC}"
cd $BASE_DIR/master/bin
./master ../master_stable.config &
MASTER_PID=$!
echo -e "${GREEN}✓ Master iniciado (PID: $MASTER_PID)${NC}"
sleep 2

echo -e "${YELLOW}[3/4] Iniciando WORKERS...${NC}"
cd $BASE_DIR/worker/bin
./worker ../worker1_stable.config 1 &
WORKER_PID_1=$!
echo -e "${GREEN}✓ Worker 1 iniciado (PID: $WORKER_PID_1)${NC}"
sleep 1

./worker ../worker2_stable.config 2 &
WORKER_PID_2=$!
echo -e "${GREEN}✓ Worker 2 iniciado (PID: $WORKER_PID_2)${NC}"
sleep 1

./worker ../worker3_stable.config 3 &
WORKER_PID_3=$!
echo -e "${GREEN}✓ Worker 3 iniciado (PID: $WORKER_PID_3)${NC}"
sleep 2

echo -e "${YELLOW}[4/4] Sistema listo para recibir queries${NC}"
echo ""
echo "======================================"
echo "  SISTEMA INICIADO"
echo "======================================"
echo "Storage PID: $STORAGE_PID"
echo "Master PID:  $MASTER_PID"
echo "Worker 1 PID: $WORKER_PID_1"
echo "Worker 2 PID: $WORKER_PID_2"
echo "Worker 3 PID: $WORKER_PID_3"
echo ""
echo "Para enviar queries, ejecutar:"
echo "  cd $BASE_DIR/query_control/bin"
echo "  ./query_control ../../query_ctrl.config <archivo_query> <prioridad>"
echo ""
echo "Para detener todo, ejecutar:"
echo "  kill $STORAGE_PID $MASTER_PID $WORKER_PID_1 $WORKER_PID_2 $WORKER_PID_3"
echo ""
echo "Presiona Ctrl+C para detener todos los módulos..."
echo ""

# Guardar PIDs en archivo para otros scripts
echo "$STORAGE_PID $MASTER_PID $WORKER_PID_1 $WORKER_PID_2 $WORKER_PID_3" > $BASE_DIR/.system_pids

# Función para limpiar al salir
cleanup() {
    echo ""
    echo "Deteniendo todos los módulos..."
    kill $STORAGE_PID $MASTER_PID $WORKER_PID_1 $WORKER_PID_2 $WORKER_PID_3 2>/dev/null
    rm -f $BASE_DIR/.system_pids
    echo "✓ Todos los módulos detenidos"
    exit 0
}

# Capturar Ctrl+C
trap cleanup SIGINT SIGTERM

# Mantener el script corriendo y verificar que los procesos sigan vivos
while true; do
    # Verificar si algún proceso crítico murió
    if ! kill -0 $STORAGE_PID 2>/dev/null; then
        echo -e "\n${RED}ERROR: Storage se detuvo inesperadamente${NC}"
        cleanup
    fi
    if ! kill -0 $MASTER_PID 2>/dev/null; then
        echo -e "\n${RED}ERROR: Master se detuvo inesperadamente${NC}"
        cleanup
    fi
    sleep 5
done