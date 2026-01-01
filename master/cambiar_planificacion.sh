#!/bin/bash

# Validar parámetros
if [ "$#" -ne 3 ]; then
    echo "Uso: $0 ./archivo.config ALGORITMO TIEMPO_AGING"
    echo "Ejemplo: $0 ./archivo.config FIFO 10"
    exit 1
fi

ARCHIVO="$1"
ALGORITMO="$2"
TIEMPO_AGING="$3"

# Verificar que existe el archivo
if [ ! -f "$ARCHIVO" ]; then
    echo "Error: No existe el archivo $ARCHIVO"
    exit 1
fi

# Reemplazar valores
sed -i "s/^ALGORITMO_PLANIFICACION=.*/ALGORITMO_PLANIFICACION=$ALGORITMO/" "$ARCHIVO"
sed -i "s/^TIEMPO_AGING=.*/TIEMPO_AGING=$TIEMPO_AGING/" "$ARCHIVO"

echo "Archivo actualizado correctamente:"
grep -E "^(ALGORITMO_PLANIFICACION|TIEMPO_AGING)=" "$ARCHIVO"
