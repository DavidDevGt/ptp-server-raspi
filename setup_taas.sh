#!/bin/bash
set -e

echo "========================================="
echo "   TaaS Enterprise Deployment System     "
echo "========================================="

echo "[+] Compilando Driver y Nodo..."
make clean
make

if lsmod | grep -q "taas_driver"; then
    echo "[+] Reiniciando Driver (versión anterior detectada)..."
    sudo systemctl stop taas 2>/dev/null || true
    sudo modprobe -r taas_driver || (echo "[!] Error: No se pudo remover el driver. ¿Está en uso?" && exit 1)
fi

echo "[+] Registrando persistencia..."
make install

echo "[+] Cargando nuevo Driver 64-bit..."
sudo modprobe taas_driver

sleep 1
if [ -e /dev/taas_timer ]; then
    echo "[OK] Dispositivo /dev/taas_timer creado con éxito."
else
    echo "[!] ERROR: El dispositivo no se creó. Revisa dmesg."
    exit 1
fi

echo "[+] Reiniciando servicio TaaS..."
sudo systemctl daemon-reload
sudo systemctl enable taas
sudo systemctl restart taas

echo "========================================="
echo "   DESPLIEGUE COMPLETADO EXITOSAMENTE    "
echo "========================================="
sudo systemctl status taas --no-pager
