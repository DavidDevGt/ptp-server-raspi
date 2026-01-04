# TaaS — Time as a Service

![platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Zero%202%20W-red)
![kernel](https://img.shields.io/badge/kernel-6.12.x--v7-green)
![realtime](https://img.shields.io/badge/realtime-SCHED_FIFO_99-critical)
![isolation](https://img.shields.io/badge/isolation-isolcpus--3-orange)
![network](https://img.shields.io/badge/network-Low--Latency--WiFi-blue)
![deployment](https://img.shields.io/badge/deployment-GitOps--Hooks-brightgreen)

**Deterministic hardware time access, Cryptographic Notarization, and Low-Latency Networking for BCM2837.**

---

## Overview

**TaaS (Time as a Service)** is a vertically optimized timing architecture designed to deliver **predictable, microsecond-accurate time** over a network. 

By bypassing standard kernel abstractions and tuning the hardware at the firmware level, TaaS creates a dedicated execution slice for time-critical operations. It exposes the **BCM2837 64-bit System Timer** directly to user space via a custom MMIO-based kernel driver, ensuring that the act of "reading time" is free from syscall overhead and scheduler interference.

---

## Vertical System Optimization (The SRE Stack)

TaaS is not just code; it is a **hardened runtime environment**. The system is tuned from the bootloader to the application layer:

### 1. Hard Core Isolation & Kernel Silence
The OS is restricted to Cores 0-2, leaving **Core 3** as a dedicated silicon slice for TaaS. This is achieved via advanced boot parameters in `cmdline.txt`:
*   `isolcpus=3 rcu_nocbs=3 nohz_full=3`: Completely removes Core 3 from the general scheduler.
*   `irqaffinity=0,1,2`: Pins all hardware interrupts to other cores.
*   `rcu_nocb_poll`: Offloads RCU processing to ensure zero kernel-induced jitter on the timing core.
*   `workqueue.cpumask=7`: Restricts kernel worker threads to non-isolated cores.

### 2. Radio & Network Layer Tuning
To achieve low-latency over WiFi (WLAN), the radio power management is disabled to prevent "sleep-induced latency spikes":
*   `iwconfig wlan0 power off`: Forces the WiFi chip into high-performance constant-awake mode.
*   `cfg80211.ieee80211_regdom=GT`: Regulatory domain hardcoded for consistent radio behavior.

### 3. Firmware & Thermal Stability
*   `force_turbo=1`: Disables dynamic frequency scaling (DVFS) to prevent clock instability and timing drift.
*   `arm_boost=1`: Maximizes throughput for cryptographic operations.
*   `disable-bt`: Bluetooth is disabled at the overlay level to reduce interrupt load and radio interference.

---

## Infrastructure as Code (Embedded GitOps)

TaaS utilizes a **Git-driven deployment workflow**. Infrastructure updates are managed via server-side Git hooks:
*   **Post-Receive Automation**: Pushing code to the node triggers an automatic build, LKM installation, and network optimization.
*   **Certification**: Every deployment ends with a real-time jitter certification test (`measure_jitter.py`) running under a real-time scheduler.

---

## System Architecture

```text
    FIRMWARE / BOOT LAYER          KERNEL LAYER              USERSPACE LAYER
┌──────────────────────────┐   ┌──────────────────┐      ┌──────────────────────┐
│ BCM2837 SoC              │   │ Custom LKM       │      │ TaaS RT-Daemon       │
│ - Core 3 Isolated        │   │ - Non-cached Pgs │ mmap │ - SCHED_FIFO 99      │
│ - IRQ Affinity pinned    ├──►│ - Atomic Logic   ├─────►│ - mlockall Residency │
│ - WiFi Power Mgmt: OFF   │   │ - /dev/taas_timer│      │ - Ed25519 Signer     │
└──────────────────────────┘   └──────────────────┘      └──────────┬───────────┘
                                                                    │
                                              UDP [Port 1588] ◄─────┘
                                              - Signed TSA Certificates
                                              - Raw PTP-like timestamps
```

---

## Technical Deep-Dive: Atomicity

Reading a 64-bit timestamp on a 32-bit architecture (ARMv7) risks data corruption if a rollover occurs between reading the Low and High registers. TaaS implements a lock-less verification loop:

```c
/* Guaranteed consistent 64-bit read on 32-bit bus */
do {
    h1 = *st_high;
    l  = *st_low;
    h2 = *st_high;
} while (h1 != h2);
timestamp = ((u64)h1 << 32) | l;
```

---

## Usage

### 1. Raw PTP-style Timestamp (UDP)
```bash
echo -n "ping" | nc -u -w 1 127.0.0.1 1588 | hexdump -C
```

### 2. Trusted Timestamping (TSA Notarization)
Send a 32-byte SHA256 hash to receive a signed 104-byte certificate:
```bash
sha256sum document.pdf | cut -d' ' -f1 | xxd -r -p | nc -u -w 1 [NODE_IP] 1588 > cert.tsr
```

---

## License
**GPLv2**

---

## Philosophy
> "In real-time systems, **abstraction is latency**. The kernel should protect hardware access — not stand in its way."

---

**Author:** David ([@DavidDevGt](https://github.com/DavidDevGt))
