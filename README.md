![platform](https://img.shields.io/badge/platform-Raspberry%20Pi%203-blue)
![kernel](https://img.shields.io/badge/kernel-64--bit-green)
![license](https://img.shields.io/badge/license-GPLv2-red)
![realtime](https://img.shields.io/badge/realtime-SCHED_FIFO-critical)

# TaaS — Time as a Service

**Enterprise-Grade Hardware Timestamping & PTP Node for Raspberry Pi**

---

## 📌 Visión General

**TaaS (Time as a Service)** es una solución **enterprise-level** de sincronización de tiempo de **alta precisión**, diseñada para entornos embebidos, industriales y de infraestructura crítica.

El sistema expone el **System Timer de 64 bits del SoC BCM2837** directamente al espacio de usuario mediante un **driver Linux kernel** optimizado, y lo integra con un **nodo PTP (Precision Time Protocol)** de baja latencia que opera en tiempo real.

> 🎯 Objetivo principal: **timestamping determinista, estable y de ultra baja latencia**, eliminando jitter introducido por el kernel y llamadas syscall tradicionales.

---

## 🧩 Arquitectura del Sistema

```
┌────────────────────────────┐
│      Hardware (BCM2837)     │
│  System Timer 64-bit (ST)   │
└───────────────┬────────────┘
                │ MMIO
┌───────────────▼────────────┐
│   Kernel Module (taas)      │
│   - ioremap ST registers   │
│   - /dev/taas_timer        │
│   - mmap + read() API      │
└───────────────┬────────────┘
                │ mmap (zero-copy)
┌───────────────▼────────────┐
│   User Space Node           │
│   - SCHED_FIFO RT          │
│   - UDP PTP (port 1588)    │
│   - 64-bit timestamp reply │
└────────────────────────────┘
```

---

## 🚀 Componentes

### 1️⃣ Kernel Driver — `taas_driver`

**Tipo:** Linux Kernel Module
**Dispositivo:** `/dev/taas_timer`

#### Funcionalidades clave

* Acceso directo al **System Timer 64-bit**
* Lectura atómica High/Low para consistencia temporal
* Soporte **MMAP no-cacheado** (latencia mínima)
* Registro como `miscdevice` con permisos controlados

#### Interfaces

| Interfaz | Descripción                            |
| -------- | -------------------------------------- |
| `read()` | Retorna timestamp 64-bit               |
| `mmap()` | Mapea registros del timer directamente |

---

### 2️⃣ Nodo PTP — `taas_node`

**Tipo:** User-space real-time daemon
**Protocolo:** UDP (PTP-like)
**Puerto:** `1588`

#### Características

* Prioridad **SCHED_FIFO (RT, prio 99)**
* Zero-copy timestamping vía `mmap`
* Respuesta determinista a triggers de red
* Limpieza segura ante señales (`SIGINT`, `SIGTERM`)

---

## 🛠️ Compilación

### Requisitos

* Linux kernel headers
* GCC
* Raspberry Pi (BCM2837)
* Privilegios de superusuario

```bash
make
```

Esto compila:

* `taas_driver.ko`
* `taas_node`

---

## 📦 Instalación Enterprise

Se recomienda usar el **script oficial de despliegue**:

```bash
chmod +x setup_taas.sh
./setup_taas.sh
```

### El script realiza:

✔ Limpieza y recompilación
✔ Eliminación segura de versiones anteriores
✔ Instalación persistente del driver
✔ Configuración automática de reglas UDEV
✔ Carga del módulo kernel
✔ Verificación de `/dev/taas_timer`
✔ Reinicio y habilitación del servicio `taas`

---

## 🔐 Seguridad y Permisos

* Dispositivo expuesto vía UDEV:

  ```
  KERNEL=="taas_timer", MODE="0666"
  ```
* Acceso directo a MMIO → **uso exclusivo en sistemas confiables**
* Diseñado para **entornos controlados / industriales**

---

## 📡 Flujo de Operación

1. Cliente envía trigger UDP
2. Nodo TaaS:

   * Lee timer sin syscall
   * Ensambla timestamp 64-bit
3. Respuesta inmediata al cliente
4. Latencia extremadamente baja y predecible

---

## 📈 Casos de Uso

* Precision Time Protocol (PTP)
* Sincronización de nodos industriales
* Timestamping financiero / trading
* Audio / Video profesional
* Edge computing
* Sistemas de control en tiempo real
* Instrumentación y medición de latencia

---

## ⚙️ Compatibilidad

| Plataforma               | Estado                         |
| ------------------------ | ------------------------------ |
| Raspberry Pi 3 (BCM2837) | ✅ Soportado                    |
| Kernel 64-bit            | ✅ Requerido                    |
| Raspberry Pi 4           | ⚠ Requiere ajuste de base MMIO |

---

## 🧪 Verificación

```bash
ls -l /dev/taas_timer
dmesg | grep TaaS
systemctl status taas
```

---

## 📜 Licencia

Este proyecto se distribuye bajo licencia **GPL v2**, compatible con módulos kernel Linux.

---

## 🧠 Filosofía de Diseño

> **El tiempo no se pide al sistema.
> El tiempo se toma del hardware.**

TaaS elimina capas innecesarias, syscall jitter y abstracciones de alto nivel para entregar **tiempo puro, determinista y verificable**.

---

