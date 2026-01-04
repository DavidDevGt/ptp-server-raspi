# TaaS — Time as a Service

![platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Zero%202%20W-red)
![kernel](https://img.shields.io/badge/kernel-6.12.x--v7-green)
![license](https://img.shields.io/badge/license-GPLv2-blue)
![realtime](https://img.shields.io/badge/realtime-SCHED_FIFO_99-critical)
![memory](https://img.shields.io/badge/memory-mlockall-orange)
![crypto](https://img.shields.io/badge/crypto-Ed25519-blue)

**Deterministic hardware time access and PTP node for Raspberry Pi (BCM2837).**

---

## Overview

**TaaS (Time as a Service)** is a low-level timing architecture designed for **deterministic, low-latency time access** on Linux-based embedded systems.

By bypassing standard syscall-based clocks (`clock_gettime`, hrtimers, vDSO), TaaS exposes the **BCM2837 64-bit System Timer** directly to user space via a minimal kernel driver and an `mmap()`-based MMIO interface.

The ultimate objective is not abstraction, but **predictability**.

This project addresses key real-time challenges:
* **Scheduler latency:** Traditional time-keeping is subject to OS scheduling.
* **Syscall overhead:** Context switches are not free.
* **Direct Access:** Time-critical systems should read time directly from hardware, not request it from the kernel.

---

## Design Goals

* **Deterministic Reads:** Zero scheduler involvement during the hot path.
* **Zero-Copy MMIO:** Direct register access via memory mapping.
* **Minimal Footprint:** No unnecessary kernel surface area or timekeeping reinvention.
* **Predictable Execution:** Clear failure modes and no background kernel threads.

*Note: If your use case requires portability or POSIX compliance, this architecture is not the intended solution.*

---

## System Architecture

TaaS maps the hardware timer directly into user space. The kernel's role is strictly limited to **validation, mapping, and protecting hardware access.**

```text
┌──────────────────────────────┐
│      BCM2837 SoC Hardware    │
│   64-bit System Timer (ST)   │
└───────────────┬──────────────┘
                │ MMIO
┌───────────────▼──────────────┐
│      Kernel Module (taas)    │
│  - Non-cached page mapping   │
│  - Atomic 64-bit read logic  │
│  - miscdevice (/dev/taas)    │
└───────────────┬──────────────┘
                │ mmap()
┌───────────────▼──────────────┐
│       User Space Node        │
│  - SCHED_FIFO (prio 99)      │
│  - mlockall(MCL_CURRENT|FUT) │
│  - UDP time responder        │
│  - Ed25519 Signing Engine    │ 
└──────────────────────────────┘
```

**Efficiency by design:** No kernel threads, no ioctl overhead, and zero polling inside kernel space.

---

## Kernel Driver (`taas_driver.c`)

The kernel module is a lightweight LKM that performs exactly **three tasks**:

1. Maps the System Timer registers using MMIO.
2. Ensures strictly non-cached access (`pgprot_noncached`) to avoid stale data.
3. Exposes a read-only memory region via the `mmap()` interface.

### Atomicity
Since the BCM2837 exposes the 64-bit timer via two 32-bit registers (Low/High), the driver implements a lock-less verification loop to guarantee a **consistent 64-bit read** without the overhead of spinlocks or IRQ hooks.

---

## User Space Node (`taas_node.c`)

The user-space daemon is a high-performance C implementation focused on determinism.

**Key Properties:**
* **Real-time Priority:** Runs under `SCHED_FIFO` (priority 99).
* **Memory Residency:** Pages are locked using `mlockall()` to prevent swapping and page faults.
* **Zero-Alloc Hot-Path:** No dynamic memory allocation after initialization.
* **Hardware Native:** Reads time directly from the mapped registers, bypassing libc time functions.
* **Integrated Ed25519 TSA:** Provides signed "Proof of Existence" certificates for cryptographic notarization.

---

## Installation

### Requirements
* **Hardware:** Raspberry Pi Zero 2 W (BCM2837).
* **OS:** Raspberry Pi OS (Debian 13 or newer).
* **Dependencies:** Kernel headers and OpenSSL development libraries (`libssl-dev`).

```bash
sudo apt install raspberrypi-kernel-headers libssl-dev
```

### Setup

1. **Generate Node Identity (Ed25519):**
```bash
openssl genpkey -algorithm ed25519 -out private_key.pem
```

2. **Deploy System:**
```bash
chmod +x setup_taas.sh
sudo ./setup_taas.sh
```

### Real-time Tuning
To ensure maximum predictability, it is highly recommended to isolate the CPU core. Append the following to `/boot/cmdline.txt`:

```text
isolcpus=3 rcu_nocbs=3 nohz_full=3
```
*This dedicates Core 3 exclusively to the TaaS node by removing scheduler ticks and RCU callbacks.*

---

## Validation & Usage

### 1. Basic UDP Timer (Raw PTP)
Query the node for the raw 64-bit hardware timer value:
```bash
echo -n "ping" | nc -u -w 1 127.0.0.1 1588 | hexdump -C
```

### 2. Trusted Timestamping (TSA Certificate)
Send a 32-byte SHA256 hash to receive a signed 104-byte notarization certificate:
```bash
# Example: Notarizing a file
sha256sum document.txt | cut -d' ' -f1 | xxd -r -p | nc -u -w 1 127.0.0.1 1588 > cert.tsr
```

---

## Compatibility Matrix

| Component | Status |
| --- | --- |
| Hardware | Raspberry Pi Zero 2 W ✅ |
| SoC | BCM2837 ✅ |
| Architecture | armv7l (32-bit) ✅ |
| Kernel | Linux 6.12.x-rpi-v7 ✅ |
| Time Source | System Timer (µs resolution) ✅ |
| Crypto | Ed25519 Digital Signature ✅ |

---

## Limitations

* **Hardware Locked:** Non-portable; tied to BCM2837 memory layout (Physical `0x3F003000`).
* **Raw Time:** No clock discipline (NTP-style slewing) or leap second handling.
* **Manual Sync:** Not synchronized with external time sources by default.

*These limitations are intentional design choices to prioritize performance over general-purpose flexibility.*

---

## License

**GPLv2**
Kernel-side code adheres to Linux kernel licensing conventions.

---

## Philosophy

> In real-time systems, **abstraction is latency**. 
> The kernel should protect hardware access — not stand in its way.

---

**Author:** David ([@DavidDevGt](https://github.com/DavidDevGt))
