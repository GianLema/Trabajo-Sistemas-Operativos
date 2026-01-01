#!/bin/bash

# Script de diagnóstico para verificar el funcionamiento del Worker

BASE_DIR="/home/tobiasrf22/tp-2025-2c-BaaaaaastA-"

# Colores
BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

echo -e "${BLUE}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║      DIAGNÓSTICO DEL WORKER - ANÁLISIS DE MEMORIA           ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ ! -f "$BASE_DIR/worker/bin/worker.log" ]; then
    echo -e "${RED}Error: No se encuentra worker.log${NC}"
    echo "Ejecuta primero algunas queries para generar logs."
    exit 1
fi

LOG_FILE="$BASE_DIR/worker/bin/worker.log"

echo -e "${CYAN}Analizando:${NC} $LOG_FILE"
echo ""

# 1. Verificar direcciones físicas
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}1. VERIFICACIÓN DE DIRECCIONES FÍSICAS${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"

TOTAL_DIRECCIONES=$(grep -c "Dirección Física:" "$LOG_FILE" 2>/dev/null || echo "0")
DIRECCIONES_INVALIDAS=$(grep -c "Dirección Física: -1" "$LOG_FILE" 2>/dev/null || echo "0")
DIRECCIONES_VALIDAS=$((TOTAL_DIRECCIONES - DIRECCIONES_INVALIDAS))

echo -e "${CYAN}Total de accesos con dirección física:${NC} $TOTAL_DIRECCIONES"
echo -e "${GREEN}Direcciones válidas (>= 0):${NC} $DIRECCIONES_VALIDAS"
echo -e "${RED}Direcciones inválidas (-1):${NC} $DIRECCIONES_INVALIDAS"

if [ $DIRECCIONES_INVALIDAS -gt 0 ]; then
    echo ""
    echo -e "${RED}⚠ PROBLEMA DETECTADO: Hay direcciones físicas inválidas${NC}"
    echo -e "${YELLOW}Mostrando ejemplos de direcciones inválidas:${NC}"
    grep "Dirección Física: -1" "$LOG_FILE" | head -5
    echo ""
fi

# 2. Verificar asignación de marcos
echo ""
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}2. ASIGNACIÓN DE MARCOS${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"

echo -e "${CYAN}Asignaciones de marcos:${NC}"
grep "Se asigna el Marco:" "$LOG_FILE" | while read line; do
    # Extraer número de marco
    MARCO=$(echo "$line" | grep -oP 'Marco: \K\d+' | head -1)
    PAGINA=$(echo "$line" | grep -oP 'Página: \K\d+')
    
    if [ "$MARCO" = "-1" ]; then
        echo -e "${RED}  ✗ Marco -1 asignado a página $PAGINA (INVÁLIDO)${NC}"
    else
        echo -e "${GREEN}  ✓ Marco $MARCO asignado a página $PAGINA${NC}"
    fi
done | head -20

# Contar marcos inválidos
MARCOS_INVALIDOS=$(grep "Se asigna el Marco: -1" "$LOG_FILE" 2>/dev/null | wc -l)
if [ $MARCOS_INVALIDOS -gt 0 ]; then
    echo ""
    echo -e "${RED}⚠ PROBLEMA: Se asignaron $MARCOS_INVALIDOS marcos con valor -1${NC}"
fi

# 3. Verificar números de página
echo ""
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}3. DISTRIBUCIÓN DE PÁGINAS ACCEDIDAS${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"

echo -e "${CYAN}Páginas únicas accedidas:${NC}"
grep -E "Memoria (Miss|Hit|Add)" "$LOG_FILE" | grep -oP 'Pagina: \K\d+' | sort -n | uniq -c | while read count pagina; do
    echo "  Página $pagina: $count accesos"
done | head -15

# Verificar si solo se accede a la página 0
PAGINAS_UNICAS=$(grep -E "Memoria (Miss|Hit|Add)" "$LOG_FILE" | grep -oP 'Pagina: \K\d+' | sort -n | uniq | wc -l)
SOLO_PAGINA_0=$(grep -E "Memoria (Miss|Hit|Add)" "$LOG_FILE" | grep -oP 'Pagina: \K\d+' | sort -n | uniq)

if [ "$SOLO_PAGINA_0" = "0" ] && [ $PAGINAS_UNICAS -eq 1 ]; then
    echo ""
    echo -e "${YELLOW}⚠ ADVERTENCIA: Solo se está accediendo a la página 0${NC}"
    echo -e "${YELLOW}   Esto podría indicar un problema en el cálculo de números de página${NC}"
fi

# 4. Verificar reemplazos de página
echo ""
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}4. REEMPLAZOS DE PÁGINA (LRU)${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"

TOTAL_REEMPLAZOS=$(grep -c "Se reemplaza la página" "$LOG_FILE" 2>/dev/null || echo "0")
echo -e "${CYAN}Total de reemplazos de página:${NC} $TOTAL_REEMPLAZOS"

if [ $TOTAL_REEMPLAZOS -gt 0 ]; then
    echo ""
    echo -e "${CYAN}Detalles de reemplazos:${NC}"
    grep "Se reemplaza la página" "$LOG_FILE" | nl
    echo ""
    echo -e "${CYAN}Marcos liberados y reutilizados:${NC}"
    grep "Se libera el Marco:" "$LOG_FILE"
    echo ""
    echo -e "${CYAN}Verificación de reutilización:${NC}"
    grep "marco.*reutilizado" "$LOG_FILE"
else
    echo -e "${YELLOW}No se han realizado reemplazos de página todavía.${NC}"
    echo -e "${YELLOW}Esto es normal si la memoria no se ha llenado.${NC}"
fi

# 5. Calcular estadísticas de accesos
echo ""
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}5. ESTADÍSTICAS DE ACCESOS A MEMORIA${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"

MEMORIA_MISS=$(grep -c "Memoria Miss" "$LOG_FILE" 2>/dev/null || echo "0")
MEMORIA_HIT=$(grep -c "Memoria Hit" "$LOG_FILE" 2>/dev/null || echo "0")
MEMORIA_ADD=$(grep -c "Memoria Add" "$LOG_FILE" 2>/dev/null || echo "0")
WRITES=$(grep -c "Acción: ESCRIBIR" "$LOG_FILE" 2>/dev/null || echo "0")
READS=$(grep -c "Acción: LEER" "$LOG_FILE" 2>/dev/null || echo "0")

echo -e "${CYAN}Memoria Miss:${NC} $MEMORIA_MISS"
echo -e "${CYAN}Memoria Hit:${NC} $MEMORIA_HIT"
echo -e "${CYAN}Páginas cargadas (Add):${NC} $MEMORIA_ADD"
echo -e "${CYAN}Operaciones de escritura:${NC} $WRITES"
echo -e "${CYAN}Operaciones de lectura:${NC} $READS"

if [ $MEMORIA_MISS -gt 0 ] || [ $MEMORIA_HIT -gt 0 ]; then
    TOTAL_ACCESOS=$((MEMORIA_MISS + MEMORIA_HIT))
    HIT_RATE=$(awk "BEGIN {printf \"%.2f\", ($MEMORIA_HIT * 100.0) / $TOTAL_ACCESOS}")
    echo -e "${CYAN}Tasa de Hit:${NC} ${HIT_RATE}%"
fi

# 6. Verificar offsets
echo ""
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}6. DISTRIBUCIÓN DE OFFSETS${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"

echo -e "${CYAN}Offsets únicos detectados:${NC}"
grep -E "Dirección Física:" "$LOG_FILE" | grep -oP 'Física: \K-?\d+' | \
    awk '{if($1 >= 0) print $1 % 4096}' | sort -n | uniq -c | head -10

# 7. Resumen y diagnóstico
echo ""
echo -e "${BLUE}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                    DIAGNÓSTICO FINAL                         ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

PROBLEMAS=0

if [ $DIRECCIONES_INVALIDAS -gt 0 ]; then
    echo -e "${RED}✗ PROBLEMA 1: Direcciones físicas inválidas detectadas${NC}"
    echo -e "  Causa probable: frame_number = -1 al calcular dirección física"
    echo -e "  Solución: Verificar asignación de marcos en obtener_pagina()"
    PROBLEMAS=$((PROBLEMAS + 1))
    echo ""
fi

if [ $MARCOS_INVALIDOS -gt 0 ]; then
    echo -e "${RED}✗ PROBLEMA 2: Marcos con valor -1 asignados${NC}"
    echo -e "  Causa probable: asignar_marco_libre() retorna -1"
    echo -e "  Solución: Verificar frame_table y used_pages"
    PROBLEMAS=$((PROBLEMAS + 1))
    echo ""
fi

if [ "$SOLO_PAGINA_0" = "0" ] && [ $PAGINAS_UNICAS -eq 1 ] && [ $TOTAL_ACCESOS -gt 5 ]; then
    echo -e "${YELLOW}⚠ ADVERTENCIA: Solo se accede a la página 0${NC}"
    echo -e "  Posible causa: Cálculo de page_number siempre retorna 0"
    echo -e "  Verificar: calcular_numero_pagina(address)"
    echo ""
fi

if [ $TOTAL_REEMPLAZOS -eq 0 ] && [ $MEMORIA_ADD -gt 10 ]; then
    echo -e "${YELLOW}⚠ INFO: No se han realizado reemplazos${NC}"
    echo -e "  Esto es normal si la memoria es suficiente para todas las páginas"
    echo ""
fi

if [ $PROBLEMAS -eq 0 ]; then
    echo -e "${GREEN}✓✓✓ NO SE DETECTARON PROBLEMAS CRÍTICOS ✓✓✓${NC}"
    echo ""
    echo -e "${GREEN}El Worker está funcionando correctamente:${NC}"
    echo -e "  - Direcciones físicas válidas"
    echo -e "  - Marcos asignados correctamente"
    echo -e "  - Sistema de memoria operativo"
else
    echo -e "${RED}Se detectaron $PROBLEMAS problema(s) que requieren atención${NC}"
fi

echo ""
echo -e "${CYAN}Para más detalles, revisa:${NC} $LOG_FILE"
