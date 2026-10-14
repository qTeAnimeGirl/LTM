#if defined(__APPLE__) && !defined(__FREESTANDING__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>

#define BENCH_PRINT(str) printf("%s", str)
#define BENCH_PRINT_FMT(fmt, ...) printf(fmt, ##__VA_ARGS__)

#else

#include "include/ltm_test.h"

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif

#define BENCH_PRINT(str) ltm_print(str)

static void print_uint(uint64_t val) {
    char buf[32];
    int i = 0;
    if (val == 0) {
        ltm_print("0");
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\0';
    ltm_print(buf);
}

#endif

static inline uint64_t bench_timer_now(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static inline uint64_t bench_timer_freq(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

/* Workload 1: Sieve of Eratosthenes (10,000,000 range) */
#define SIEVE_LIMIT 10000000
static uint64_t bench_sieve(uint8_t *sieve) {
    for (size_t i = 0; i < SIEVE_LIMIT; i++) sieve[i] = 1;
    sieve[0] = sieve[1] = 0;

    for (size_t p = 2; p * p < SIEVE_LIMIT; p++) {
        if (sieve[p]) {
            for (size_t i = p * p; i < SIEVE_LIMIT; i += p) {
                sieve[i] = 0;
            }
        }
    }

    uint64_t count = 0;
    for (size_t i = 0; i < SIEVE_LIMIT; i++) {
        if (sieve[i]) count++;
    }
    return count;
}

/* Workload 2: SHA-256 Engine */
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x) (ROR(x, 2) ^ ROR(x, 13) ^ ROR(x, 22))
#define SIGMA1(x) (ROR(x, 6) ^ ROR(x, 11) ^ ROR(x, 25))
#define GAMMA0(x) (ROR(x, 7) ^ ROR(x, 18) ^ ((x) >> 3))
#define GAMMA1(x) (ROR(x, 17) ^ ROR(x, 19) ^ ((x) >> 10))

static void sha256_transform(uint32_t state[8], const uint32_t block[16]) {
    uint32_t W[64];
    for (int t = 0; t < 16; t++) W[t] = block[t];
    for (int t = 16; t < 64; t++) W[t] = GAMMA1(W[t - 2]) + W[t - 7] + GAMMA0(W[t - 15]) + W[t - 16];

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int t = 0; t < 64; t++) {
        uint32_t T1 = h + SIGMA1(e) + CH(e, f, g) + K[t] + W[t];
        uint32_t T2 = SIGMA0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static uint32_t bench_sha256(size_t num_blocks) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint32_t block[16];
    for (int i = 0; i < 16; i++) block[i] = (uint32_t)(i * 0x9e3779b9);

    for (size_t b = 0; b < num_blocks; b++) {
        block[0] = (uint32_t)b;
        sha256_transform(state, block);
    }
    return state[0] ^ state[7];
}

/* Workload 3: Matrix Multiplication (GEMM 300x300 Double Precision) */
#define MATRIX_N 300
static double bench_gemm(double *A, double *B, double *C) {
    for (int i = 0; i < MATRIX_N; i++) {
        for (int j = 0; j < MATRIX_N; j++) {
            A[i * MATRIX_N + j] = (double)(i + j) * 0.001;
            B[i * MATRIX_N + j] = (double)(i - j) * 0.001;
            C[i * MATRIX_N + j] = 0.0;
        }
    }

    for (int i = 0; i < MATRIX_N; i++) {
        for (int k = 0; k < MATRIX_N; k++) {
            double aik = A[i * MATRIX_N + k];
            for (int j = 0; j < MATRIX_N; j++) {
                C[i * MATRIX_N + j] += aik * B[k * MATRIX_N + j];
            }
        }
    }

    double sum = 0.0;
    for (int i = 0; i < MATRIX_N * MATRIX_N; i++) {
        sum += C[i];
    }
    return sum;
}

/* Workload 4: Recursive Fibonacci (F(40)) */
static uint64_t fib(uint64_t n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

/* Workload 5: Memory Bandwidth (64MB buffer x 4 passes = 256MB) */
#define MEM_SIZE (64 * 1024 * 1024)
static uint64_t bench_mem(uint64_t *buf) {
    size_t count = MEM_SIZE / sizeof(uint64_t);
    for (size_t i = 0; i < count; i++) {
        buf[i] = i ^ 0x5a5a5a5a5a5a5a5aULL;
    }
    for (size_t i = 0; i < count; i++) {
        buf[i] = buf[i] + 1;
    }
    for (size_t i = 0; i < count; i++) {
        buf[i] = buf[i] ^ 0xa5a5a5a5a5a5a5a5ULL;
    }
    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += buf[i];
    }
    return sum;
}

static void run_all_benchmarks(const char *environment_tag) {
    uint64_t freq = bench_timer_freq();
    if (freq == 0) freq = 24000000;

    BENCH_PRINT("\n======================================================\n");
    BENCH_PRINT("      LTM CPU Performance Benchmark Suite           \n");
    BENCH_PRINT("  Apple Silicon Native vs Linux ARM64 Translation     \n");
    BENCH_PRINT("======================================================\n");
    BENCH_PRINT("Environment: ");
    BENCH_PRINT(environment_tag);
    BENCH_PRINT("\nHardware Timer: 24 MHz cntvct_el0 hardware cycle counter\n");
    BENCH_PRINT("------------------------------------------------------\n");

    /* Allocate memory buffers */
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    uint8_t *sieve_buf = malloc(SIEVE_LIMIT);
    double *mat_A = malloc(MATRIX_N * MATRIX_N * sizeof(double));
    double *mat_B = malloc(MATRIX_N * MATRIX_N * sizeof(double));
    double *mat_C = malloc(MATRIX_N * MATRIX_N * sizeof(double));
    uint64_t *mem_buf = malloc(MEM_SIZE);
#else
    uint8_t *sieve_buf = (uint8_t *)sys_mmap(NULL, SIEVE_LIMIT, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    double *mat_A = (double *)sys_mmap(NULL, MATRIX_N * MATRIX_N * sizeof(double), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    double *mat_B = (double *)sys_mmap(NULL, MATRIX_N * MATRIX_N * sizeof(double), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    double *mat_C = (double *)sys_mmap(NULL, MATRIX_N * MATRIX_N * sizeof(double), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    uint64_t *mem_buf = (uint64_t *)sys_mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
#endif

    /* 1. Sieve */
    BENCH_PRINT("[1/5] Running Sieve of Eratosthenes (10M integers)... ");
    uint64_t t0 = bench_timer_now();
    uint64_t primes = bench_sieve(sieve_buf);
    uint64_t t1 = bench_timer_now();
    uint64_t ms_sieve = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (primes found: %llu)\n", ms_sieve, primes);
#else
    print_uint(ms_sieve);
    BENCH_PRINT(" ms (primes found: ");
    print_uint(primes);
    BENCH_PRINT(")\n");
#endif

    /* 2. SHA-256 */
    BENCH_PRINT("[2/5] Running SHA-256 Cryptographic Hash (500k blocks)... ");
    t0 = bench_timer_now();
    uint32_t sha_res = bench_sha256(500000);
    t1 = bench_timer_now();
    uint64_t ms_sha = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (hash: 0x%08x)\n", ms_sha, sha_res);
#else
    print_uint(ms_sha);
    BENCH_PRINT(" ms (hash check: ");
    print_uint(sha_res);
    BENCH_PRINT(")\n");
#endif

    /* 3. GEMM */
    BENCH_PRINT("[3/5] Running Matrix Multiply GEMM (300x300 double)... ");
    t0 = bench_timer_now();
    double gemm_sum = bench_gemm(mat_A, mat_B, mat_C);
    t1 = bench_timer_now();
    uint64_t ms_gemm = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (checksum: %.2f)\n", ms_gemm, gemm_sum);
#else
    print_uint(ms_gemm);
    BENCH_PRINT(" ms (54M FLOPs completed)\n");
    (void)gemm_sum;
#endif

    /* 4. Recursive Fibonacci */
    BENCH_PRINT("[4/5] Running Recursive Fibonacci F(40) (331M calls)... ");
    t0 = bench_timer_now();
    uint64_t fib_res = fib(40);
    t1 = bench_timer_now();
    uint64_t ms_fib = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (result: %llu)\n", ms_fib, fib_res);
#else
    print_uint(ms_fib);
    BENCH_PRINT(" ms (result: ");
    print_uint(fib_res);
    BENCH_PRINT(")\n");
#endif

    /* 5. Memory Bandwidth */
    BENCH_PRINT("[5/5] Running Memory Stream Bandwidth (256MB throughput)... ");
    t0 = bench_timer_now();
    uint64_t mem_res = bench_mem(mem_buf);
    t1 = bench_timer_now();
    uint64_t ms_mem = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (check: 0x%llx)\n", ms_mem, mem_res);
#else
    print_uint(ms_mem);
    BENCH_PRINT(" ms (throughput: ");
    print_uint(ms_mem > 0 ? (256 * 1000 / ms_mem) : 0);
    BENCH_PRINT(" MB/s)\n");
    (void)mem_res;
#endif

    BENCH_PRINT("------------------------------------------------------\n");
    uint64_t total_ms = ms_sieve + ms_sha + ms_gemm + ms_fib + ms_mem;
    BENCH_PRINT("Total Benchmark Execution Time: ");
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms\n", total_ms);
    BENCH_PRINT_FMT("BENCH_RESULT:%s:%llu:%llu:%llu:%llu:%llu:%llu\n",
                    environment_tag, ms_sieve, ms_sha, ms_gemm, ms_fib, ms_mem, total_ms);
#else
    print_uint(total_ms);
    BENCH_PRINT(" ms\n");
    BENCH_PRINT("BENCH_RESULT:");
    BENCH_PRINT(environment_tag);
    BENCH_PRINT(":");
    print_uint(ms_sieve); BENCH_PRINT(":");
    print_uint(ms_sha); BENCH_PRINT(":");
    print_uint(ms_gemm); BENCH_PRINT(":");
    print_uint(ms_fib); BENCH_PRINT(":");
    print_uint(ms_mem); BENCH_PRINT(":");
    print_uint(total_ms); BENCH_PRINT("\n");
#endif
    BENCH_PRINT("======================================================\n\n");
}

#if defined(__APPLE__) && !defined(__FREESTANDING__)
int main(int argc, char **argv) {
    const char *tag = "Darwin-Native-MachO";
    if (argc > 1) tag = argv[1];
    run_all_benchmarks(tag);
    return 0;
}
#else
int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    run_all_benchmarks("LTM-Linux-ARM64-ELF");
    return 0;
}

LTM_TEST_ENTRYPOINT
#endif
