#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>

/* 24 MHz ARM64 Virtual Counter */
static inline uint64_t timer_now(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static inline uint64_t timer_freq(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    if (val == 0) val = 24000000;
    return val;
}

/* ========================================================================= */
/* WORKLOAD 1: Matrix Multiplication (GEMM 300x300 Double)                   */
/* ========================================================================= */
#define GEMM_N 300

static double run_gemm_worker(double *A, double *B, double *C, int id) {
    for (int i = 0; i < GEMM_N; i++) {
        for (int j = 0; j < GEMM_N; j++) {
            A[i * GEMM_N + j] = (double)(i + j + id) * 0.001;
            B[i * GEMM_N + j] = (double)(i - j + id) * 0.001;
            C[i * GEMM_N + j] = 0.0;
        }
    }
    for (int i = 0; i < GEMM_N; i++) {
        for (int k = 0; k < GEMM_N; k++) {
            double aik = A[i * GEMM_N + k];
            for (int j = 0; j < GEMM_N; j++) {
                C[i * GEMM_N + j] += aik * B[k * GEMM_N + j];
            }
        }
    }
    double sum = 0.0;
    for (int i = 0; i < GEMM_N * GEMM_N; i++) sum += C[i];
    return sum;
}

/* ========================================================================= */
/* WORKLOAD 2: Prime Sieve of Eratosthenes (10,000,000 ints)                 */
/* ========================================================================= */
#define SIEVE_LIMIT 10000000

static uint64_t run_sieve_worker(uint8_t *sieve) {
    for (size_t i = 0; i < SIEVE_LIMIT; i++) sieve[i] = 1;
    sieve[0] = sieve[1] = 0;
    for (size_t p = 2; p * p < SIEVE_LIMIT; p++) {
        if (sieve[p]) {
            for (size_t i = p * p; i < SIEVE_LIMIT; i += p) sieve[i] = 0;
        }
    }
    uint64_t count = 0;
    for (size_t i = 0; i < SIEVE_LIMIT; i++) {
        if (sieve[i]) count++;
    }
    return count;
}

/* ========================================================================= */
/* WORKLOAD 3: Recursive Fibonacci F(40) (~331,160,281 calls)               */
/* ========================================================================= */
static uint64_t fib(uint64_t n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

/* ========================================================================= */
/* WORKLOAD 4: Memory Streaming (64MB per thread)                            */
/* ========================================================================= */
#define MEM_STREAM_BYTES (64 * 1024 * 1024)

static uint64_t run_mem_worker(uint64_t *buf, int id) {
    size_t count = MEM_STREAM_BYTES / sizeof(uint64_t);
    uint64_t pat = 0x5a5a5a5a5a5a5a5aULL ^ (uint64_t)id;
    for (size_t i = 0; i < count; i++) buf[i] = i ^ pat;
    for (size_t i = 0; i < count; i++) buf[i] = buf[i] + 1;
    for (size_t i = 0; i < count; i++) buf[i] = buf[i] ^ 0xa5a5a5a5a5a5a5a5ULL;
    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) sum += buf[i];
    return sum;
}

/* Thread Dispatcher */
typedef enum {
    WORK_GEMM,
    WORK_SIEVE,
    WORK_FIB,
    WORK_MEM
} work_kind_t;

typedef struct {
    int id;
    work_kind_t kind;
    double *gemm_A;
    double *gemm_B;
    double *gemm_C;
    uint8_t *sieve_buf;
    uint64_t *mem_buf;
    uint64_t result_u64;
    double result_double;
} worker_arg_t;

static void *thread_worker_func(void *arg) {
    worker_arg_t *wa = (worker_arg_t *)arg;
    switch (wa->kind) {
        case WORK_GEMM:
            wa->result_double = run_gemm_worker(wa->gemm_A, wa->gemm_B, wa->gemm_C, wa->id);
            break;
        case WORK_SIEVE:
            wa->result_u64 = run_sieve_worker(wa->sieve_buf);
            break;
        case WORK_FIB:
            wa->result_u64 = fib(40);
            break;
        case WORK_MEM:
            wa->result_u64 = run_mem_worker(wa->mem_buf, wa->id);
            break;
    }
    return NULL;
}

static uint64_t run_parallel_job(worker_arg_t *args, int nthreads, work_kind_t kind) {
    pthread_t threads[8];
    for (int i = 0; i < nthreads; i++) {
        args[i].kind = kind;
        pthread_create(&threads[i], NULL, thread_worker_func, &args[i]);
    }
    uint64_t freq = timer_freq();
    uint64_t t0 = timer_now();
    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i], NULL);
    }
    uint64_t t1 = timer_now();
    return (t1 - t0) * 1000 / freq;
}

int main(int argc, char **argv) {
    const char *tag = "Benchmark";
    if (argc > 1) tag = argv[1];

    printf("==============================================================================\n");
    printf("        LTM Apple Silicon 1 to 8 Core Multi-Thread Scaling Benchmark          \n");
    printf(" Target: %s\n", tag);
    printf("==============================================================================\n\n");

    /* Allocate buffers for up to 8 threads */
    worker_arg_t args[8];
    memset(args, 0, sizeof(args));
    for (int i = 0; i < 8; i++) {
        args[i].id = i;
        args[i].gemm_A = (double *)malloc(GEMM_N * GEMM_N * sizeof(double));
        args[i].gemm_B = (double *)malloc(GEMM_N * GEMM_N * sizeof(double));
        args[i].gemm_C = (double *)malloc(GEMM_N * GEMM_N * sizeof(double));
        args[i].sieve_buf = (uint8_t *)malloc(SIEVE_LIMIT);
        args[i].mem_buf = (uint64_t *)malloc(MEM_STREAM_BYTES);
    }

    int core_counts[] = {1, 2, 4, 8};
    int n_configs = 4;

    uint64_t gemm_times[4];
    uint64_t sieve_times[4];
    uint64_t fib_times[4];
    uint64_t mem_times[4];

    /* 1. Matrix GEMM */
    printf("[1/4] Benchmarking Matrix GEMM across 1, 2, 4, 8 cores...\n");
    for (int c = 0; c < n_configs; c++) {
        int threads = core_counts[c];
        gemm_times[c] = run_parallel_job(args, threads, WORK_GEMM);
        double total_gflops = (2.0 * GEMM_N * GEMM_N * GEMM_N * threads) / 1e9;
        double gflops_per_sec = gemm_times[c] > 0 ? (total_gflops / (gemm_times[c] / 1000.0)) : 0;
        printf("  -> %d Cores: %4llu ms (Total GFLOPs: %.3f, Throughput: %.2f GFLOP/s)\n",
               threads, (unsigned long long)gemm_times[c], total_gflops, gflops_per_sec);
    }

    /* 2. Prime Sieve */
    printf("\n[2/4] Benchmarking Prime Sieve (10M ints/core) across 1, 2, 4, 8 cores...\n");
    for (int c = 0; c < n_configs; c++) {
        int threads = core_counts[c];
        sieve_times[c] = run_parallel_job(args, threads, WORK_SIEVE);
        double total_primes_m = (threads * 10.0);
        double m_ints_per_sec = sieve_times[c] > 0 ? (total_primes_m / (sieve_times[c] / 1000.0)) : 0;
        printf("  -> %d Cores: %4llu ms (Total Range: %2.0fM ints, Rate: %.1f Mints/s)\n",
               threads, (unsigned long long)sieve_times[c], total_primes_m, m_ints_per_sec);
    }

    /* 3. Recursive Fibonacci F(40) */
    printf("\n[3/4] Benchmarking Recursive Fibonacci F(40) across 1, 2, 4, 8 cores...\n");
    for (int c = 0; c < n_configs; c++) {
        int threads = core_counts[c];
        fib_times[c] = run_parallel_job(args, threads, WORK_FIB);
        double total_calls_b = (threads * 0.33116);
        double b_calls_per_sec = fib_times[c] > 0 ? (total_calls_b / (fib_times[c] / 1000.0)) : 0;
        printf("  -> %d Cores: %4llu ms (Total Calls: %.2fB, Rate: %.2f Bcalls/s)\n",
               threads, (unsigned long long)fib_times[c], total_calls_b, b_calls_per_sec);
    }

    /* 4. Memory Streaming */
    printf("\n[4/4] Benchmarking Memory Streaming (64MB/core) across 1, 2, 4, 8 cores...\n");
    for (int c = 0; c < n_configs; c++) {
        int threads = core_counts[c];
        mem_times[c] = run_parallel_job(args, threads, WORK_MEM);
        size_t total_mb = threads * 64;
        double mb_per_sec = mem_times[c] > 0 ? ((double)total_mb / (mem_times[c] / 1000.0)) : 0;
        printf("  -> %d Cores: %4llu ms (Total Volume: %4zu MB, Aggregate Bandwidth: %6.1f MB/s)\n",
               threads, (unsigned long long)mem_times[c], total_mb, mb_per_sec);
    }

    /* Machine readable CSV output */
    printf("\nSCALING_DATA:%s", tag);
    for (int c = 0; c < n_configs; c++) printf(":%llu", (unsigned long long)gemm_times[c]);
    for (int c = 0; c < n_configs; c++) printf(":%llu", (unsigned long long)sieve_times[c]);
    for (int c = 0; c < n_configs; c++) printf(":%llu", (unsigned long long)fib_times[c]);
    for (int c = 0; c < n_configs; c++) printf(":%llu", (unsigned long long)mem_times[c]);
    printf("\n");

    /* Cleanup */
    for (int i = 0; i < 8; i++) {
        free(args[i].gemm_A);
        free(args[i].gemm_B);
        free(args[i].gemm_C);
        free(args[i].sieve_buf);
        free(args[i].mem_buf);
    }

    return 0;
}
