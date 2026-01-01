#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Uso: $0 ./archivo.config NUEVA_IP"
    exit 1
fi

ARCHIVO="$1"
NUEVA_IP="$2"

if [ ! -f "$ARCHIVO" ]; then
    echo "Error: No existe el archivo $ARCHIVO"
    exit 1
fi

# Reemplaza línea tipo: IP_MASTER=192.168.1.1
sed -i "s/^IP_MASTER=.*/IP_MASTER=$NUEVA_IP/" "$ARCHIVO"

echo "Archivo actualizado correctamente:"
grep "^IP_MASTER" "$ARCHIVO"
