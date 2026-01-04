/*
 * TaaS Node - Userspace PTP Daemon
 * SPDX-License-Identifier: GPL-2.0
 *
 * TaaS User-Space Time Node
 *
 * This process maps the BCM2837 system timer directly and serves
 * high-precision UTC timestamps over UDP.
 *
 * Architecture:
 * - Boot-Time Anchoring: Syncs Hardware Timer to Kernel UTC once at startup.
 * - Runtime: Extrapolates time using only hardware ticks (No syscalls).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h> 

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#define PTP_PORT 1588
#define TIMER_DEVICE "/dev/taas_timer"
#define MAP_SIZE 4096
#define KEY_FILE "private_key.pem"

/* * BCM2837 System Timer runs at 1MHz.
 * 1 Tick = 1 Microsecond = 1000 Nanoseconds.
 */
#define NSEC_PER_TICK 1000

struct __attribute__((packed)) taas_certificate {
    uint8_t  client_hash[32];
    uint64_t utc_timestamp_ns; /* Changed from raw ticks to UTC Nanoseconds */
    uint8_t  signature[64];
};

/* Structure to hold the Boot-Time Anchor */
struct time_anchor {
    uint64_t base_utc_ns;
    uint64_t base_hw_ticks;
};

static int timer_fd = -1;
static void *map_base = NULL;
static EVP_PKEY *pkey = NULL;
static EVP_MD_CTX *md_ctx = NULL;
static struct time_anchor anchor;

/* Pointers to memory mapped registers */
static volatile uint32_t *st_low = NULL;
static volatile uint32_t *st_high = NULL;

/*
 * shutdown_node - async-signal-safe cleanup handler
 */
void shutdown_node(int sig)
{
    if (map_base != MAP_FAILED && map_base != NULL)
        munmap(map_base, MAP_SIZE);

    if (timer_fd >= 0)
        close(timer_fd);

    if (md_ctx)
        EVP_MD_CTX_free(md_ctx);

    if (pkey)
        EVP_PKEY_free(pkey);

    const char msg[] = "\n[taas] stopping daemon\n";
    write(STDOUT_FILENO, msg, sizeof(msg)-1);
    _exit(EXIT_SUCCESS);
}

/*
 * get_hardware_ticks - Atomic read of 64-bit BCM2837 timer
 * Uses optimistic concurrency control (lock-free).
 */
static inline uint64_t get_hardware_ticks(void)
{
    uint32_t h1, l, h2;
    do {
        h1 = *st_high;
        l  = *st_low;
        h2 = *st_high;
    } while (h1 != h2);
    return ((uint64_t)h1 << 32) | l;
}

/*
 * calibrate_time_anchor - Establishes the relationship between 
 * Hardware Ticks and Real World Time (UTC).
 * This is called ONCE at startup to avoid syscalls later.
 */
void calibrate_time_anchor(void)
{
    struct timespec ts_kernel;
    
    /* * Critical Section: We want these two reads to be as close as possible.
     * Since we are isolated on Core 3 with interrupts pinned away, 
     * this is highly deterministic.
     */
    clock_gettime(CLOCK_REALTIME, &ts_kernel);
    uint64_t ticks_now = get_hardware_ticks();

    anchor.base_utc_ns = ((uint64_t)ts_kernel.tv_sec * 1000000000ULL) + ts_kernel.tv_nsec;
    anchor.base_hw_ticks = ticks_now;

    printf("[TaaS] Anchor Established:\n");
    printf("       UTC Base: %llu ns\n", anchor.base_utc_ns);
    printf("       HW Base:  %llu ticks\n", anchor.base_hw_ticks);
}

int main(void)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) < 0) {
        perror("taas warning: isolcpus is active?");
    }

    struct sched_param sp = { .sched_priority = 99 };
    struct sockaddr_in servaddr, cliaddr;
    int sockfd;

    signal(SIGINT, shutdown_node);
    signal(SIGTERM, shutdown_node);

    OpenSSL_add_all_algorithms();

    md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        fprintf(stderr, "Fatal: Failed to allocate crypto context\n");
        return EXIT_FAILURE;
    }

    FILE *fp = fopen(KEY_FILE, "r");
    if (fp) {
        pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
        fclose(fp);
    } else {
        fprintf(stderr, "Fatal: Key file not found. Generate Ed25519 key first.\n");
    }

    /* Lock memory to prevent paging latency */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
        perror("taas: warning: mlockall failed");

    /* Elevate to Real-Time FIFO */
    if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
        perror("taas: warning: sched_setscheduler failed");

    /* Open Timer Driver */
    timer_fd = open(TIMER_DEVICE, O_RDWR | O_SYNC);
    if (timer_fd < 0) {
        perror("taas: open timer device");
        return EXIT_FAILURE;
    }

    /* MMIO Map */
    map_base = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, timer_fd, 0);
    if (map_base == MAP_FAILED) {
        perror("taas: mmap failed");
        close(timer_fd);
        return EXIT_FAILURE;
    }

    /* BCM2837 Offsets */
    st_low  = (volatile uint32_t *)((char *)map_base + 0x04);
    st_high = (volatile uint32_t *)((char *)map_base + 0x08);

    /* --- CALIBRATION PHASE --- */
    /* This connects the abstract hardware ticks to real world time */
    calibrate_time_anchor();

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("taas: socket creation");
        return EXIT_FAILURE;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PTP_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("taas: bind failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    struct taas_certificate cert;
    uint8_t data_to_sign[40];
    uint8_t buffer[64];
    socklen_t len = sizeof(cliaddr);
    size_t req_len;

    printf("[TaaS] Unified Ed25519 Node Ready. Serving UTC Nanoseconds.\n");

    /*
     * Main event loop:
     * - Wait for UDP trigger
     * - Perform atomic hardware read
     * - Extrapolate UTC time from Anchor
     * - Send UTC timestamp back
     */
    while (1) {
        ssize_t rec = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                               (struct sockaddr *)&cliaddr, &len);

        if (rec > 0) {
            
            /* 1. Get Hardware Ticks (Atomic) */
            uint64_t current_hw = get_hardware_ticks();

            /* 2. Calculate Delta from Anchor */
            uint64_t delta_ticks = current_hw - anchor.base_hw_ticks;

            /* 3. Convert to Nanoseconds (1MHz timer -> x1000) */
            uint64_t delta_ns = delta_ticks * NSEC_PER_TICK;

            /* 4. Calculate Absolute UTC Time */
            uint64_t current_utc_ns = anchor.base_utc_ns + delta_ns;

            if (rec == 32 && pkey) {
                /* TSA MODE (Certificate with UTC) */
                memcpy(cert.client_hash, buffer, 32);
                cert.utc_timestamp_ns = current_utc_ns;

                memcpy(data_to_sign, cert.client_hash, 32);
                memcpy(data_to_sign + 32, &cert.utc_timestamp_ns, 8);

                if (EVP_DigestSignInit(md_ctx, NULL, NULL, NULL, pkey) == 1) {
                    req_len = 64;
                    EVP_DigestSign(md_ctx, cert.signature, &req_len, data_to_sign, 40);
                }

                sendto(sockfd, &cert, sizeof(cert), 0, (struct sockaddr *)&cliaddr, len);
            } else {
                /* RAW MODE (Just the UTC uint64) */
                sendto(sockfd, &current_utc_ns, sizeof(current_utc_ns), 0, (struct sockaddr *)&cliaddr, len);
            }
        }
    }

    return 0;
}
