#if defined(__APPLE__) && !defined(__FREESTANDING__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
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

#define NUM_BENCH_THREADS 8
#define STACK_SIZE (1024 * 1024)

/* 24 MHz ARM64 Virtual Counter */
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

/* ========================================================================= */
/* WORKLOAD 1: Parallel Sieve of Eratosthenes (80,000,000 total integers)     */
/* ========================================================================= */
#define SIEVE_PER_THREAD 10000000
static uint64_t run_sieve_worker(uint8_t *sieve, size_t limit) {
    for (size_t i = 0; i < limit; i++) sieve[i] = 1;
    sieve[0] = sieve[1] = 0;

    for (size_t p = 2; p * p < limit; p++) {
        if (sieve[p]) {
            for (size_t i = p * p; i < limit; i += p) {
                sieve[i] = 0;
            }
        }
    }

    uint64_t count = 0;
    for (size_t i = 0; i < limit; i++) {
        if (sieve[i]) count++;
    }
    return count;
}

/* ========================================================================= */
/* WORKLOAD 2: Parallel SHA-256 Engine (4,000,000 blocks / 256MB)            */
/* ========================================================================= */
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

static uint32_t run_sha256_worker(size_t num_blocks, uint32_t salt) {
    uint32_t state[8] = {
        0x6a09e667 ^ salt, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint32_t block[16];
    for (int i = 0; i < 16; i++) block[i] = (uint32_t)(i * 0x9e3779b9 + salt);

    for (size_t b = 0; b < num_blocks; b++) {
        block[0] = (uint32_t)(b + salt);
        sha256_transform(state, block);
    }
    return state[0] ^ state[7];
}

/* ========================================================================= */
/* WORKLOAD 3: Parallel Matrix Multiply GEMM (8 x 300x300 Double GEMM)       */
/* ========================================================================= */
#define MATRIX_N 300
static double run_gemm_worker(double *A, double *B, double *C, int id) {
    for (int i = 0; i < MATRIX_N; i++) {
        for (int j = 0; j < MATRIX_N; j++) {
            A[i * MATRIX_N + j] = (double)(i + j + id) * 0.001;
            B[i * MATRIX_N + j] = (double)(i - j + id) * 0.001;
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

/* ========================================================================= */
/* WORKLOAD 4: Parallel Recursive Fibonacci (8 concurrent F(40) = 2.65B ops) */
/* ========================================================================= */
static uint64_t fib(uint64_t n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

/* ========================================================================= */
/* WORKLOAD 5: Parallel Memory Streaming (8 x 64MB = 512MB RAM throughput)  */
/* ========================================================================= */
#define MEM_PER_THREAD (64 * 1024 * 1024)
static uint64_t run_mem_worker(uint64_t *buf, int id) {
    size_t count = MEM_PER_THREAD / sizeof(uint64_t);
    uint64_t pat = 0x5a5a5a5a5a5a5a5aULL ^ (uint64_t)id;
    for (size_t i = 0; i < count; i++) {
        buf[i] = i ^ pat;
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

/* ========================================================================= */
/* WORKLOAD 6: Scaling Workload (Integer Arithmetic & Bit Mixing)            */
/* ========================================================================= */
static uint64_t run_math_worker(uint64_t iterations, uint64_t seed) {
    uint64_t acc = seed;
    for (uint64_t i = 0; i < iterations; i++) {
        acc ^= (acc << 13);
        acc ^= (acc >> 7);
        acc ^= (acc << 17);
        acc += 0x9e3779b97f4a7c15ULL;
    }
    return acc;
}

/* ========================================================================= */
/* Thread Management & Synchronization Abstraction                           */
/* ========================================================================= */
typedef enum {
    BENCH_WORK_SIEVE,
    BENCH_WORK_SHA,
    BENCH_WORK_GEMM,
    BENCH_WORK_FIB,
    BENCH_WORK_MEM,
    BENCH_WORK_MATH
} bench_work_type_t;

struct bench_thread_arg {
    int thread_id;
    bench_work_type_t work_type;
    uint8_t *sieve_buf;
    double *gemm_A;
    double *gemm_B;
    double *gemm_C;
    uint64_t *mem_buf;
    uint64_t math_iters;
    uint64_t result_u64;
    double result_double;
};

#if defined(__APPLE__) && !defined(__FREESTANDING__)

static void *darwin_thread_worker(void *arg) {
    struct bench_thread_arg *ta = (struct bench_thread_arg *)arg;
    switch (ta->work_type) {
        case BENCH_WORK_SIEVE:
            ta->result_u64 = run_sieve_worker(ta->sieve_buf, SIEVE_PER_THREAD);
            break;
        case BENCH_WORK_SHA:
            ta->result_u64 = run_sha256_worker(500000, (uint32_t)ta->thread_id * 100);
            break;
        case BENCH_WORK_GEMM:
            ta->result_double = run_gemm_worker(ta->gemm_A, ta->gemm_B, ta->gemm_C, ta->thread_id);
            break;
        case BENCH_WORK_FIB:
            ta->result_u64 = fib(40);
            break;
        case BENCH_WORK_MEM:
            ta->result_u64 = run_mem_worker(ta->mem_buf, ta->thread_id);
            break;
        case BENCH_WORK_MATH:
            ta->result_u64 = run_math_worker(ta->math_iters, 0x12345678ULL + ta->thread_id);
            break;
    }
    return NULL;
}

static void run_parallel_threads(struct bench_thread_arg *args, int num_threads) {
    pthread_t threads[NUM_BENCH_THREADS];
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, darwin_thread_worker, &args[i]);
    }
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
}

#else

#define CLONE_VM             0x00000100
#define CLONE_FS             0x00000200
#define CLONE_FILES          0x00000400
#define CLONE_SIGHAND        0x00000800
#define CLONE_THREAD         0x00010000
#define CLONE_SYSVSEM        0x00040000

#define FUTEX_WAIT           0
#define FUTEX_WAKE           1

static volatile int g_start_gate = 0;
static volatile int g_completed_count = 0;

static inline long ltm_clone(int (*fn)(void *), void *stack, int flags, void *arg, int *ptid, void *tls, int *ctid) {
    register long x0 __asm__("x0") = (long)fn;
    register long x1 __asm__("x1") = (long)stack;
    register long x2 __asm__("x2") = (long)flags;
    register long x3 __asm__("x3") = (long)arg;
    register long x4 __asm__("x4") = (long)ptid;
    register long x5 __asm__("x5") = (long)tls;
    register long x6 __asm__("x6") = (long)ctid;

    __asm__ volatile (
        "sub x1, x1, #16\n\t"
        "stp x0, x3, [x1]\n\t"
        "mov x0, x2\n\t"
        "mov x2, x4\n\t"
        "mov x3, x5\n\t"
        "mov x4, x6\n\t"
        "mov x8, #220\n\t"      /* SYS_clone */
        "svc #0\n\t"
        "cbnz x0, 1f\n\t"
        "ldp x1, x0, [sp], #16\n\t"
        "blr x1\n\t"
        "mov x8, #93\n\t"       /* SYS_exit */
        "svc #0\n\t"
        "1:\n\t"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6)
        :
        : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "memory"
    );

    return x0;
}

static int linux_thread_worker(void *arg) {
    struct bench_thread_arg *ta = (struct bench_thread_arg *)arg;

    /* Wait for start gate */
    while (g_start_gate == 0) {
        sys_futex((int *)&g_start_gate, FUTEX_WAIT, 0, NULL, NULL, 0);
    }

    switch (ta->work_type) {
        case BENCH_WORK_SIEVE:
            ta->result_u64 = run_sieve_worker(ta->sieve_buf, SIEVE_PER_THREAD);
            break;
        case BENCH_WORK_SHA:
            ta->result_u64 = run_sha256_worker(500000, (uint32_t)ta->thread_id * 100);
            break;
        case BENCH_WORK_GEMM:
            ta->result_double = run_gemm_worker(ta->gemm_A, ta->gemm_B, ta->gemm_C, ta->thread_id);
            break;
        case BENCH_WORK_FIB:
            ta->result_u64 = fib(40);
            break;
        case BENCH_WORK_MEM:
            ta->result_u64 = run_mem_worker(ta->mem_buf, ta->thread_id);
            break;
        case BENCH_WORK_MATH:
            ta->result_u64 = run_math_worker(ta->math_iters, 0x12345678ULL + ta->thread_id);
            break;
    }

    /* Signal completion */
    int done = __atomic_add_fetch(&g_completed_count, 1, __ATOMIC_SEQ_CST);
    sys_futex((int *)&g_completed_count, FUTEX_WAKE, 1, NULL, NULL, 0);
    (void)done;

    return 0;
}

static void *g_thread_stacks[NUM_BENCH_THREADS];

static void run_parallel_threads(struct bench_thread_arg *args, int num_threads) {
    g_start_gate = 0;
    g_completed_count = 0;

    int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM;

    for (int i = 0; i < num_threads; i++) {
        if (!g_thread_stacks[i]) {
            g_thread_stacks[i] = (void *)sys_mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        }
        void *top_of_stack = (void *)((uintptr_t)g_thread_stacks[i] + STACK_SIZE);
        ltm_clone(linux_thread_worker, top_of_stack, flags, &args[i], NULL, NULL, NULL);
    }

    /* Release all threads simultaneously */
    g_start_gate = 1;
    sys_futex((int *)&g_start_gate, FUTEX_WAKE, num_threads, NULL, NULL, 0);

    /* Wait for all threads to complete */
    while (g_completed_count < num_threads) {
        sys_futex((int *)&g_completed_count, FUTEX_WAIT, g_completed_count, NULL, NULL, 0);
    }
}

#endif

/* ========================================================================= */
/* BENCHMARK SUITE DRIVER                                                    */
/* ========================================================================= */
static void run_multithread_suite(const char *environment_tag) {
    uint64_t freq = bench_timer_freq();
    if (freq == 0) freq = 24000000;

    BENCH_PRINT("\n==============================================================\n");
    BENCH_PRINT("      LTM 8-CORE CPU MULTI-THREAD BENCHMARK SUITE             \n");
    BENCH_PRINT("   Apple Silicon Native Darwin vs Linux ARM64 Translation     \n");
    BENCH_PRINT("==============================================================\n");
    BENCH_PRINT("Environment       : ");
    BENCH_PRINT(environment_tag);
    BENCH_PRINT("\nTotal CPU Threads : 8 Cores (4 Performance + 4 Efficiency)\n");
    BENCH_PRINT("Hardware Timer    : 24 MHz cntvct_el0 high-precision cycle counter\n");
    BENCH_PRINT("--------------------------------------------------------------\n");

    /* Allocate buffers for all 8 threads */
    struct bench_thread_arg args[NUM_BENCH_THREADS];
    memset(args, 0, sizeof(args));

    for (int i = 0; i < NUM_BENCH_THREADS; i++) {
        args[i].thread_id = i;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
        args[i].sieve_buf = (uint8_t *)malloc(SIEVE_PER_THREAD);
        args[i].gemm_A = (double *)malloc(MATRIX_N * MATRIX_N * sizeof(double));
        args[i].gemm_B = (double *)malloc(MATRIX_N * MATRIX_N * sizeof(double));
        args[i].gemm_C = (double *)malloc(MATRIX_N * MATRIX_N * sizeof(double));
        args[i].mem_buf = (uint64_t *)malloc(MEM_PER_THREAD);
#else
        args[i].sieve_buf = (uint8_t *)sys_mmap(NULL, SIEVE_PER_THREAD, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        args[i].gemm_A = (double *)sys_mmap(NULL, MATRIX_N * MATRIX_N * sizeof(double), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        args[i].gemm_B = (double *)sys_mmap(NULL, MATRIX_N * MATRIX_N * sizeof(double), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        args[i].gemm_C = (double *)sys_mmap(NULL, MATRIX_N * MATRIX_N * sizeof(double), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        args[i].mem_buf = (uint64_t *)sys_mmap(NULL, MEM_PER_THREAD, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
#endif
    }

    /* ------------------------------------------------------------- */
    /* Workload 1: Parallel Sieve of Eratosthenes (80M integers)     */
    /* ------------------------------------------------------------- */
    BENCH_PRINT("[1/6] Parallel Prime Sieve (80M integers across 8 cores)... ");
    for (int i = 0; i < NUM_BENCH_THREADS; i++) args[i].work_type = BENCH_WORK_SIEVE;
    uint64_t t0 = bench_timer_now();
    run_parallel_threads(args, NUM_BENCH_THREADS);
    uint64_t t1 = bench_timer_now();
    uint64_t ms_sieve = (t1 - t0) * 1000 / freq;
    uint64_t total_primes = 0;
    for (int i = 0; i < NUM_BENCH_THREADS; i++) total_primes += args[i].result_u64;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (total primes: %llu)\n", ms_sieve, total_primes);
#else
    print_uint(ms_sieve);
    BENCH_PRINT(" ms (total primes: ");
    print_uint(total_primes);
    BENCH_PRINT(")\n");
#endif

    /* ------------------------------------------------------------- */
    /* Workload 2: Parallel SHA-256 Hashing (4,000,000 blocks / 256MB)*/
    /* ------------------------------------------------------------- */
    BENCH_PRINT("[2/6] Parallel SHA-256 Hashing (4M blocks / 256MB across 8 cores)... ");
    for (int i = 0; i < NUM_BENCH_THREADS; i++) args[i].work_type = BENCH_WORK_SHA;
    t0 = bench_timer_now();
    run_parallel_threads(args, NUM_BENCH_THREADS);
    t1 = bench_timer_now();
    uint64_t ms_sha = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (all 8 hashes computed)\n", ms_sha);
#else
    print_uint(ms_sha);
    BENCH_PRINT(" ms (all 8 hashes computed)\n");
#endif

    /* ------------------------------------------------------------- */
    /* Workload 3: Parallel Matrix Multiplication (8 x 300x300 GEMM) */
    /* ------------------------------------------------------------- */
    BENCH_PRINT("[3/6] Parallel Matrix GEMM (432M FLOPs across 8 cores)... ");
    for (int i = 0; i < NUM_BENCH_THREADS; i++) args[i].work_type = BENCH_WORK_GEMM;
    t0 = bench_timer_now();
    run_parallel_threads(args, NUM_BENCH_THREADS);
    t1 = bench_timer_now();
    uint64_t ms_gemm = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (432 MFLOPs completed)\n", ms_gemm);
#else
    print_uint(ms_gemm);
    BENCH_PRINT(" ms (432 MFLOPs completed)\n");
#endif

    /* ------------------------------------------------------------- */
    /* Workload 4: Parallel Recursive Fibonacci (8 x F(40) = 2.65B calls)*/
    /* ------------------------------------------------------------- */
    BENCH_PRINT("[4/6] Parallel Recursive Fibonacci (2.65B calls across 8 cores)... ");
    for (int i = 0; i < NUM_BENCH_THREADS; i++) args[i].work_type = BENCH_WORK_FIB;
    t0 = bench_timer_now();
    run_parallel_threads(args, NUM_BENCH_THREADS);
    t1 = bench_timer_now();
    uint64_t ms_fib = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (all 8 threads completed)\n", ms_fib);
#else
    print_uint(ms_fib);
    BENCH_PRINT(" ms (all 8 threads completed)\n");
#endif

    /* ------------------------------------------------------------- */
    /* Workload 5: Parallel Memory Streaming (512MB RAM bandwidth)   */
    /* ------------------------------------------------------------- */
    BENCH_PRINT("[5/6] Parallel Memory Bandwidth (512MB RAM across 8 cores)... ");
    for (int i = 0; i < NUM_BENCH_THREADS; i++) args[i].work_type = BENCH_WORK_MEM;
    t0 = bench_timer_now();
    run_parallel_threads(args, NUM_BENCH_THREADS);
    t1 = bench_timer_now();
    uint64_t ms_mem = (t1 - t0) * 1000 / freq;
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms (aggregate throughput: %llu MB/s)\n", ms_mem, ms_mem > 0 ? (512 * 1000 / ms_mem) : 0);
#else
    print_uint(ms_mem);
    BENCH_PRINT(" ms (aggregate throughput: ");
    print_uint(ms_mem > 0 ? (512 * 1000 / ms_mem) : 0);
    BENCH_PRINT(" MB/s)\n");
#endif

    /* ------------------------------------------------------------- */
    /* Workload 6: 1-Core vs 8-Core Scaling Benchmark                */
    /* ------------------------------------------------------------- */
    BENCH_PRINT("[6/6] Multi-Core Scaling (Single-Core vs 8-Core Parallelism)... \n");

    /* Single core: 500M iterations on 1 thread */
    args[0].work_type = BENCH_WORK_MATH;
    args[0].math_iters = 500000000ULL;
    t0 = bench_timer_now();
    run_parallel_threads(args, 1);
    t1 = bench_timer_now();
    uint64_t ms_single = (t1 - t0) * 1000 / freq;

    /* 8 cores: 500M iterations per thread x 8 threads = 4,000M operations */
    for (int i = 0; i < NUM_BENCH_THREADS; i++) {
        args[i].work_type = BENCH_WORK_MATH;
        args[i].math_iters = 500000000ULL;
    }
    t0 = bench_timer_now();
    run_parallel_threads(args, NUM_BENCH_THREADS);
    t1 = bench_timer_now();
    uint64_t ms_multi = (t1 - t0) * 1000 / freq;

    /* Speedup calculation: 4000M ops would take ms_single * 8 on 1 core */
    uint64_t estimated_seq_ms = ms_single * 8;
    uint64_t speedup_x100 = ms_multi > 0 ? (estimated_seq_ms * 100 / ms_multi) : 0;

#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("      -> Single-Core Baseline (500M ops on 1 core)   : %llu ms\n", ms_single);
    BENCH_PRINT_FMT("      -> 8-Core Parallel Work  (4,000M ops on 8 cores): %llu ms\n", ms_multi);
    BENCH_PRINT_FMT("      -> Multi-Core Speedup Factor                   : %llu.%02llux (4P + 4E)\n",
                    speedup_x100 / 100, speedup_x100 % 100);
#else
    BENCH_PRINT("      -> Single-Core Baseline (500M ops on 1 core)   : ");
    print_uint(ms_single);
    BENCH_PRINT(" ms\n");
    BENCH_PRINT("      -> 8-Core Parallel Work  (4,000M ops on 8 cores): ");
    print_uint(ms_multi);
    BENCH_PRINT(" ms\n");
    BENCH_PRINT("      -> Multi-Core Speedup Factor                   : ");
    print_uint(speedup_x100 / 100);
    BENCH_PRINT(".");
    if (speedup_x100 % 100 < 10) BENCH_PRINT("0");
    print_uint(speedup_x100 % 100);
    BENCH_PRINT("x (4P + 4E)\n");
#endif

    BENCH_PRINT("--------------------------------------------------------------\n");
    uint64_t total_suite_ms = ms_sieve + ms_sha + ms_gemm + ms_fib + ms_mem + ms_multi;
    BENCH_PRINT("Total 8-Core Parallel Execution Time: ");
#if defined(__APPLE__) && !defined(__FREESTANDING__)
    BENCH_PRINT_FMT("%llu ms\n", total_suite_ms);
    BENCH_PRINT_FMT("BENCH_RESULT_MT:%s:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu\n",
                    environment_tag, ms_sieve, ms_sha, ms_gemm, ms_fib, ms_mem, ms_single, ms_multi, total_suite_ms);
#else
    print_uint(total_suite_ms);
    BENCH_PRINT(" ms\n");
    BENCH_PRINT("BENCH_RESULT_MT:");
    BENCH_PRINT(environment_tag);
    BENCH_PRINT(":");
    print_uint(ms_sieve); BENCH_PRINT(":");
    print_uint(ms_sha); BENCH_PRINT(":");
    print_uint(ms_gemm); BENCH_PRINT(":");
    print_uint(ms_fib); BENCH_PRINT(":");
    print_uint(ms_mem); BENCH_PRINT(":");
    print_uint(ms_single); BENCH_PRINT(":");
    print_uint(ms_multi); BENCH_PRINT(":");
    print_uint(total_suite_ms); BENCH_PRINT("\n");
#endif
    BENCH_PRINT("==============================================================\n\n");
}

#if defined(__APPLE__) && !defined(__FREESTANDING__)
int main(int argc, char **argv) {
    const char *tag = "macOS-Darwin-Native";
    if (argc > 1) tag = argv[1];
    run_multithread_suite(tag);
    return 0;
}
#else
int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    run_multithread_suite("Linux-ARM64-LTM");
    return 0;
}

LTM_TEST_ENTRYPOINT
#endif
