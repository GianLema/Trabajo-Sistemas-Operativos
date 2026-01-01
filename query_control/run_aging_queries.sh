#!/bin/bash

CONFIG="query_ctrl.config"

NOMBRES=(
    "AGING_1"
    "AGING_2"
    "AGING_3"
    "AGING_4"
)

PRIORIDADES=(20 20 20 20)   # Si después querés cambiar prioridad por tipo, acá lo podés modificar

for i in $(seq 1 25); do   # 25 rondas -> 100 queries
    echo "============ Ciclo $i ============"

    for idx in {0..3}; do  # Ejecuta 4 queries intercaladas por ciclo
        NOMBRE="${NOMBRES[$idx]}"
        PRIORIDAD="${PRIORIDADES[$idx]}"

        echo " ➤ Lanzando ./bin/query_control $CONFIG $NOMBRE $PRIORIDAD"
        ./bin/query_control "$CONFIG" "$NOMBRE" "$PRIORIDAD" &
        sleep 0.1   # pequeña pausa opcional para escalonar carga
    done

    echo "Ciclo $i completo (4 queries lanzadas)"
done

echo "=================================================="
echo "✔ Se lanzaron 100 queries en total"
echo "✔ 25 rondas, 4 queries por ronda (AGING_1–4)"
echo "✔ Todas con prioridad 20 intercaladas"
echo "=================================================="