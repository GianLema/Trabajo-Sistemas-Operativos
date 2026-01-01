#!/bin/bash

echo "== Configurador del TP (Deploy en 3 PCs) =="

echo ""
echo "Seleccione el rol de ESTA PC:"
echo "1) MASTER + QUERY CONTROL"
echo "2) STORAGE"
echo "3) WORKER"
read -p "Opción: " ROL

# ------------------ PUERTOS MANUALES ------------------
read -p "Ingrese el PUERTO de MASTER: " PORT_MASTER
read -p "Ingrese el PUERTO de STORAGE: " PORT_STORAGE

# ------------------ FUNCION EDITAR CAMPOS (SIN CREAR) ------------------
editar_existente() {
    local archivo="$1"
    local campo="$2"
    local valor="$3"

    # SOLO edita si existe la línea
    if grep -q "^$campo=" "$archivo"; then
        sed -i "s/^$campo=.*/$campo=$valor/" "$archivo"
    fi
}

# ======================================================
# ====================== ROL MASTER ====================
# ======================================================
if [[ $ROL == 1 ]]; then

    read -p "Ingrese la IP de ESTA PC (MASTER): " IP_MASTER
    read -p "Ingrese la IP del STORAGE: " IP_STORAGE

    echo "→ Configurando TODOS los master/*.config ..."
    for cfg in master/*.config; do
        [[ -f "$cfg" ]] || continue
        editar_existente "$cfg" "PUERTO_ESCUCHA" "$PORT_MASTER"
    done

    echo "→ Configurando TODOS los query_control/*.config ..."
    for cfg in query_control/*.config; do
        [[ -f "$cfg" ]] || continue
        editar_existente "$cfg" "IP_MASTER" "$IP_MASTER"
        editar_existente "$cfg" "PUERTO_MASTER" "$PORT_MASTER"
    done

    echo ""
    echo "MASTER + QUERY CONTROL configurados."

# ======================================================
# ===================== ROL STORAGE ====================
# ======================================================
elif [[ $ROL == 2 ]]; then

    read -p "Ingrese la IP de ESTA PC (STORAGE): " IP_STORAGE

    echo "→ Configurando TODOS los storage/*.config ..."
    for cfg in storage/*.config; do
        [[ -f "$cfg" ]] || continue
        editar_existente "$cfg" "PUERTO_ESCUCHA" "$PORT_STORAGE"
    done

    echo ""
    echo "STORAGE configurado."

# ======================================================
# ====================== ROL WORKER ====================
# ======================================================
elif [[ $ROL == 3 ]]; then

    read -p "Ingrese la IP del MASTER: " IP_MASTER
    read -p "Ingrese la IP del STORAGE: " IP_STORAGE

    echo "→ Configurando TODOS los worker/*.config ..."
    for cfg in worker/*.config; do
        [[ -f "$cfg" ]] || continue

        editar_existente "$cfg" "IP_MASTER" "$IP_MASTER"
        editar_existente "$cfg" "PUERTO_MASTER" "$PORT_MASTER"

        editar_existente "$cfg" "IP_STORAGE" "$IP_STORAGE"
        editar_existente "$cfg" "PUERTO_STORAGE" "$PORT_STORAGE"
    done

    echo ""
    echo "WORKERS configurados."

else
    echo "Opción inválida."
    exit 1
fi

echo ""
echo "============================================"
echo "Configuración finalizada para ROL $ROL"
echo "MASTER escucha en ....... $PORT_MASTER"
echo "STORAGE escucha en ...... $PORT_STORAGE"
echo "============================================"
