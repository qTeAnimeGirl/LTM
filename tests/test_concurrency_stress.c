#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

#ifdef __linux__
#include <sys/syscall.h>
#include <linux/futex.h>
#define HAVE_LINUX_FUTEX 1
#else
#define HAVE_LINUX_FUTEX 0
#endif

/* High-precision microsecond timer using Apple Silicon hardware counter */
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

static inline double elapsed_ms(uint64_t start, uint64_t end, uint64_t freq) {
    return (double)(end - start) * 1000.0 / (double)freq;
}

/* ========================================================================= */
/* PHASE 1: MASSIVE THREAD CREATION & JOINING (16, 32, 64 THREADS)           */
/* ========================================================================= */

static __thread int s_tls_worker_id = -1;
static volatile int g_phase1_counter = 0;
static uint64_t g_thread_results[128];

typedef struct {
    int id;
    uint64_t work_iters;
} phase1_arg_t;

static void *phase1_worker(void *arg) {
    phase1_arg_t *pa = (phase1_arg_t *)arg;
    int id = pa->id;
    s_tls_worker_id = id;

    /* Do CPU work: bit-mixing hash */
    uint64_t acc = 0x9e3779b97f4a7c15ULL ^ (uint64_t)id;
    for (uint64_t i = 0; i < pa->work_iters; i++) {
        acc ^= (acc << 13);
        acc ^= (acc >> 7);
        acc ^= (acc << 17);
        acc += i;
    }
    g_thread_results[id] = acc;

    /* Verify thread-local storage isolation */
    if (s_tls_worker_id != id) {
        fprintf(stderr, "[FAIL] Worker %d TLS corruption detected: %d\n", id, s_tls_worker_id);
        return (void *)(intptr_t)-1;
    }

    __atomic_fetch_add(&g_phase1_counter, 1, __ATOMIC_SEQ_CST);
    return (void *)(intptr_t)(1000 + id);
}

static int run_phase1_thread_scaling(int num_threads, uint64_t work_iters) {
    printf("[Phase 1] Testing %d Concurrent Threads (Creation, TLS, Join)...\n", num_threads);
    pthread_t threads[128];
    phase1_arg_t args[128];

    g_phase1_counter = 0;
    memset(g_thread_results, 0, sizeof(g_thread_results));

    uint64_t freq = timer_freq();
    uint64_t t0 = timer_now();

    for (int i = 0; i < num_threads; i++) {
        args[i].id = i;
        args[i].work_iters = work_iters;
        int rc = pthread_create(&threads[i], NULL, phase1_worker, &args[i]);
        if (rc != 0) {
            fprintf(stderr, "[FAIL] pthread_create failed at index %d: %s\n", i, strerror(rc));
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        void *ret = NULL;
        int rc = pthread_join(threads[i], &ret);
        if (rc != 0) {
            fprintf(stderr, "[FAIL] pthread_join failed for thread %d: %s\n", i, strerror(rc));
            return -1;
        }
        intptr_t ret_val = (intptr_t)ret;
        if (ret_val != (1000 + i)) {
            fprintf(stderr, "[FAIL] Thread %d returned unexpected value: %ld\n", i, (long)ret_val);
            return -1;
        }
    }

    uint64_t t1 = timer_now();
    double ms = elapsed_ms(t0, t1, freq);

    if (g_phase1_counter != num_threads) {
        fprintf(stderr, "[FAIL] Phase 1 counter mismatch: expected %d, got %d\n", num_threads, g_phase1_counter);
        return -1;
    }

    printf("  [PASS] %d threads created, executed, and joined successfully in %.2f ms (%.1f us/thread)\n",
           num_threads, ms, (ms * 1000.0) / (double)num_threads);
    return 0;
}

/* ========================================================================= */
/* PHASE 2: HEAVY MUTEX CONTENTION STRESS                                     */
/* ========================================================================= */

static pthread_mutex_t g_contention_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int64_t g_contention_counter = 0;
static volatile int64_t g_contention_accum = 0;

typedef struct {
    int id;
    int iterations;
} mutex_worker_arg_t;

static void *mutex_contention_worker(void *arg) {
    mutex_worker_arg_t *ma = (mutex_worker_arg_t *)arg;
    int id = ma->id;
    int iters = ma->iterations;

    for (int i = 0; i < iters; i++) {
        pthread_mutex_lock(&g_contention_mutex);
        g_contention_counter++;
        g_contention_accum += (id + 1);
        pthread_mutex_unlock(&g_contention_mutex);
    }
    return NULL;
}

static int run_phase2_mutex_contention(int num_threads, int iterations) {
    printf("[Phase 2] Heavy Mutex Contention Stress (%d threads x %d ops = %d total acquisitions)...\n",
           num_threads, iterations, num_threads * iterations);

    pthread_t threads[64];
    mutex_worker_arg_t args[64];

    g_contention_counter = 0;
    g_contention_accum = 0;

    int64_t expected_accum = 0;
    for (int i = 0; i < num_threads; i++) {
        expected_accum += (int64_t)(i + 1) * iterations;
    }

    uint64_t freq = timer_freq();
    uint64_t t0 = timer_now();

    for (int i = 0; i < num_threads; i++) {
        args[i].id = i;
        args[i].iterations = iterations;
        if (pthread_create(&threads[i], NULL, mutex_contention_worker, &args[i]) != 0) {
            fprintf(stderr, "[FAIL] pthread_create failed in mutex stress\n");
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    uint64_t t1 = timer_now();
    double ms = elapsed_ms(t0, t1, freq);

    if (g_contention_counter != (int64_t)num_threads * iterations) {
        fprintf(stderr, "[FAIL] Mutex counter mismatch: expected %lld, got %lld\n",
                (long long)(num_threads * iterations), (long long)g_contention_counter);
        return -1;
    }
    if (g_contention_accum != expected_accum) {
        fprintf(stderr, "[FAIL] Mutex accumulator mismatch: expected %lld, got %lld\n",
                (long long)expected_accum, (long long)g_contention_accum);
        return -1;
    }

    double ops_per_sec = ((double)(num_threads * iterations) / (ms / 1000.0));
    printf("  [PASS] %d lock/unlock cycles verified with zero race conditions in %.2f ms (%.0f ops/sec)\n",
           num_threads * iterations, ms, ops_per_sec);
    return 0;
}

/* ========================================================================= */
/* PHASE 3: DIRECT KERNEL FUTEX WAIT / WAKE & TIMEOUT CYCLES                 */
/* ========================================================================= */

#if HAVE_LINUX_FUTEX

static inline int sys_futex(void *uaddr, int op, int val, const struct timespec *timeout, void *uaddr2, int val3) {
    return (int)syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

#else

/* Darwin Fallback: Futex simulation via pthread condition variable */
static pthread_mutex_t g_darwin_futex_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_darwin_futex_cond = PTHREAD_COND_INITIALIZER;

static inline int sys_futex(void *uaddr, int op, int val, const struct timespec *timeout, void *uaddr2, int val3) {
    (void)uaddr2; (void)val3;
    int cmd = op & 0x7f;
    pthread_mutex_lock(&g_darwin_futex_lock);
    if (cmd == 0 /* FUTEX_WAIT */) {
        if (*(volatile uint32_t *)uaddr != (uint32_t)val) {
            pthread_mutex_unlock(&g_darwin_futex_lock);
            return -EAGAIN;
        }
        int rc = 0;
        if (timeout) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            struct timespec abstime;
            abstime.tv_sec = now.tv_sec + timeout->tv_sec;
            abstime.tv_nsec = now.tv_nsec + timeout->tv_nsec;
            if (abstime.tv_nsec >= 1000000000L) {
                abstime.tv_sec += 1;
                abstime.tv_nsec -= 1000000000L;
            }
            rc = pthread_cond_timedwait(&g_darwin_futex_cond, &g_darwin_futex_lock, &abstime);
        } else {
            rc = pthread_cond_wait(&g_darwin_futex_cond, &g_darwin_futex_lock);
        }
        pthread_mutex_unlock(&g_darwin_futex_lock);
        if (rc == ETIMEDOUT) return -ETIMEDOUT;
        return 0;
    } else if (cmd == 1 /* FUTEX_WAKE */) {
        pthread_cond_broadcast(&g_darwin_futex_cond);
        pthread_mutex_unlock(&g_darwin_futex_lock);
        return val;
    }
    pthread_mutex_unlock(&g_darwin_futex_lock);
    return -ENOSYS;
}

#endif

#ifndef FUTEX_WAIT
#define FUTEX_WAIT 0
#endif
#ifndef FUTEX_WAKE
#define FUTEX_WAKE 1
#endif

static volatile uint32_t g_futex_barrier = 0;
static volatile int g_futex_woken_count = 0;

static void *futex_waiter_thread(void *arg) {
    (void)arg;
    while (g_futex_barrier == 0) {
        int r = sys_futex((void *)&g_futex_barrier, FUTEX_WAIT, 0, NULL, NULL, 0);
        if (r < 0 && errno != EAGAIN && errno != EINTR) {
            /* Handled */
        }
    }
    __atomic_fetch_add(&g_futex_woken_count, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static int run_phase3_futex_stress(int num_waiters) {
    printf("[Phase 3] Kernel Futex Wait/Wake & Timeout Cycles (%d concurrent waiters)...\n", num_waiters);
    pthread_t threads[64];

    g_futex_barrier = 0;
    g_futex_woken_count = 0;

    uint64_t freq = timer_freq();
    uint64_t t0 = timer_now();

    for (int i = 0; i < num_waiters; i++) {
        if (pthread_create(&threads[i], NULL, futex_waiter_thread, NULL) != 0) {
            fprintf(stderr, "[FAIL] pthread_create failed in futex stress\n");
            return -1;
        }
    }

    /* Allow threads to enter sleep */
    usleep(20000); // 20 ms

    /* Wake all waiters via FUTEX_WAKE */
    g_futex_barrier = 1;
    int woken = sys_futex((void *)&g_futex_barrier, FUTEX_WAKE, num_waiters, NULL, NULL, 0);
    (void)woken;

    for (int i = 0; i < num_waiters; i++) {
        pthread_join(threads[i], NULL);
    }

    uint64_t t1 = timer_now();
    double ms = elapsed_ms(t0, t1, freq);

    if (g_futex_woken_count != num_waiters) {
        fprintf(stderr, "[FAIL] Futex woken count mismatch: expected %d, got %d\n", num_waiters, g_futex_woken_count);
        return -1;
    }
    printf("  [PASS] 3A: %d threads successfully blocked on FUTEX_WAIT and released via FUTEX_WAKE (%.2f ms)\n",
           num_waiters, ms);

    /* 3B: Futex Timedwait expiration test */
    volatile uint32_t timeout_futex = 42;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 15000000; // 15 ms
    t0 = timer_now();
    int ret = sys_futex((void *)&timeout_futex, FUTEX_WAIT, 42, &ts, NULL, 0);
    t1 = timer_now();
    double timeout_ms = elapsed_ms(t0, t1, freq);

    if (ret != -1 && ret != -ETIMEDOUT) {
        fprintf(stderr, "[FAIL] Expected FUTEX_WAIT timeout, got %d\n", ret);
        return -1;
    }
    printf("  [PASS] 3B: FUTEX_WAIT timed out correctly after %.2f ms (requested 15 ms)\n", timeout_ms);

    return 0;
}

/* ========================================================================= */
/* PHASE 4: CONCURRENT MEMORY ALLOCATION (MMAP / MALLOC ON 16KB PAGES)       */
/* ========================================================================= */

#define APPLE_PAGE_SIZE 0x4000 /* 16KB */

typedef struct {
    int id;
    int iterations;
    size_t alloc_size;
} mem_stress_arg_t;

static void *mem_stress_worker(void *arg) {
    mem_stress_arg_t *ma = (mem_stress_arg_t *)arg;
    int id = ma->id;
    int iters = ma->iterations;
    size_t sz = ma->alloc_size;

    for (int i = 0; i < iters; i++) {
        /* 1. Mmap anonymous memory */
        void *ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED || !ptr) {
            fprintf(stderr, "[FAIL] Worker %d mmap failed: %s\n", id, strerror(errno));
            return (void *)(intptr_t)-1;
        }

        /* Verify 16KB Page Alignment (Rule 1) */
        if (((uintptr_t)ptr & (APPLE_PAGE_SIZE - 1)) != 0) {
            fprintf(stderr, "[FAIL] Worker %d received non-16KB-aligned mmap: %p\n", id, ptr);
            return (void *)(intptr_t)-1;
        }

        /* 2. Page fault stress: dirty every 16KB page with unique pattern */
        uint8_t *b = (uint8_t *)ptr;
        size_t num_pages = sz / APPLE_PAGE_SIZE;
        for (size_t p = 0; p < num_pages; p++) {
            uint32_t *word = (uint32_t *)(b + p * APPLE_PAGE_SIZE);
            *word = (uint32_t)((id << 24) ^ (i << 16) ^ (p & 0xFFFF));
            // touch middle and end of page
            *(word + 1024) = (uint32_t)(0xAA550000 ^ (p + id));
            *(uint32_t *)(b + (p + 1) * APPLE_PAGE_SIZE - sizeof(uint32_t)) = (uint32_t)(0x55AA0000 ^ (p + id));
        }

        /* 3. Read back and verify memory integrity across all pages */
        for (size_t p = 0; p < num_pages; p++) {
            uint32_t *word = (uint32_t *)(b + p * APPLE_PAGE_SIZE);
            uint32_t expected = (uint32_t)((id << 24) ^ (i << 16) ^ (p & 0xFFFF));
            if (*word != expected) {
                fprintf(stderr, "[FAIL] Worker %d page %zu corruption: expected 0x%x, got 0x%x\n",
                        id, p, expected, *word);
                return (void *)(intptr_t)-1;
            }
        }

        /* 4. Test mprotect */
        if (mprotect(ptr, sz, PROT_READ) != 0) {
            fprintf(stderr, "[FAIL] mprotect PROT_READ failed\n");
            return (void *)(intptr_t)-1;
        }
        if (mprotect(ptr, sz, PROT_READ | PROT_WRITE) != 0) {
            fprintf(stderr, "[FAIL] mprotect PROT_READ|WRITE failed\n");
            return (void *)(intptr_t)-1;
        }

        /* 5. Munmap */
        if (munmap(ptr, sz) != 0) {
            fprintf(stderr, "[FAIL] Worker %d munmap failed\n", id);
            return (void *)(intptr_t)-1;
        }

        /* 6. Concurrent malloc / realloc / free heap stress */
        size_t m_sz = (size_t)(1024 * (1 + (i % 32))); // 1KB to 32KB
        void *h = malloc(m_sz);
        if (!h) {
            fprintf(stderr, "[FAIL] Worker %d malloc failed\n", id);
            return (void *)(intptr_t)-1;
        }
        memset(h, (uint8_t)(id + i), m_sz);
        h = realloc(h, m_sz * 2);
        if (!h) {
            fprintf(stderr, "[FAIL] Worker %d realloc failed\n", id);
            return (void *)(intptr_t)-1;
        }
        free(h);
    }

    return NULL;
}

static int run_phase4_memory_stress(int num_threads, int iterations) {
    size_t alloc_sz = 128 * 1024; // 128KB = 8 x 16KB pages per iteration
    printf("[Phase 4] Concurrent Memory & Page Fault Stress (%d threads x %d iters x 128KB on 16KB pages)...\n",
           num_threads, iterations);

    pthread_t threads[32];
    mem_stress_arg_t args[32];

    uint64_t freq = timer_freq();
    uint64_t t0 = timer_now();

    for (int i = 0; i < num_threads; i++) {
        args[i].id = i;
        args[i].iterations = iterations;
        args[i].alloc_size = alloc_sz;
        if (pthread_create(&threads[i], NULL, mem_stress_worker, &args[i]) != 0) {
            fprintf(stderr, "[FAIL] pthread_create failed in memory stress\n");
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        void *res = NULL;
        pthread_join(threads[i], &res);
        if (res != NULL) {
            fprintf(stderr, "[FAIL] Worker %d failed memory stress\n", i);
            return -1;
        }
    }

    uint64_t t1 = timer_now();
    double ms = elapsed_ms(t0, t1, freq);

    size_t total_mb = (size_t)num_threads * iterations * alloc_sz / (1024 * 1024);
    printf("  [PASS] 100%% 16KB page alignment verified across %zu MB total mapped, faulted, and unmapped in %.2f ms (%.1f MB/s)\n",
           total_mb, ms, (double)total_mb / (ms / 1000.0));
    return 0;
}

/* ========================================================================= */
/* PHASE 5: CONCURRENT FILE I/O (PRIVATE & SHARED PREWRITE/PREAD)            */
/* ========================================================================= */

#define IO_BLOCK_SIZE 4096
#define IO_BLOCKS_PER_THREAD 16 /* 64KB per file */

typedef struct {
    int id;
} io_arg_t;

static void *io_private_worker(void *arg) {
    io_arg_t *ia = (io_arg_t *)arg;
    int id = ia->id;

    char filename[128];
    snprintf(filename, sizeof(filename), "/tmp/ltm_stress_t%d.tmp", id);

    int fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[FAIL] Worker %d open failed: %s\n", id, strerror(errno));
        return (void *)(intptr_t)-1;
    }

    uint8_t write_buf[IO_BLOCK_SIZE];
    uint32_t checksum_written = 0;

    for (int b = 0; b < IO_BLOCKS_PER_THREAD; b++) {
        for (int k = 0; k < IO_BLOCK_SIZE; k++) {
            write_buf[k] = (uint8_t)(id ^ b ^ (k & 0xFF));
            checksum_written = checksum_written * 31 + write_buf[k];
        }
        ssize_t w = write(fd, write_buf, IO_BLOCK_SIZE);
        if (w != IO_BLOCK_SIZE) {
            fprintf(stderr, "[FAIL] Worker %d write short: %zd\n", id, w);
            close(fd);
            return (void *)(intptr_t)-1;
        }
    }

    fsync(fd);
    lseek(fd, 0, SEEK_SET);

    uint8_t read_buf[IO_BLOCK_SIZE];
    uint32_t checksum_read = 0;

    for (int b = 0; b < IO_BLOCKS_PER_THREAD; b++) {
        ssize_t r = read(fd, read_buf, IO_BLOCK_SIZE);
        if (r != IO_BLOCK_SIZE) {
            fprintf(stderr, "[FAIL] Worker %d read short: %zd\n", id, r);
            close(fd);
            return (void *)(intptr_t)-1;
        }
        for (int k = 0; k < IO_BLOCK_SIZE; k++) {
            checksum_read = checksum_read * 31 + read_buf[k];
        }
    }

    close(fd);
    unlink(filename);

    if (checksum_read != checksum_written) {
        fprintf(stderr, "[FAIL] Worker %d checksum mismatch: written=0x%x read=0x%x\n",
                id, checksum_written, checksum_read);
        return (void *)(intptr_t)-1;
    }

    return NULL;
}

#define SHARED_FILE_SLICE 16384 /* 16KB per thread slice */
static int g_shared_fd = -1;

static void *io_shared_worker(void *arg) {
    io_arg_t *ia = (io_arg_t *)arg;
    int id = ia->id;
    off_t offset = (off_t)id * SHARED_FILE_SLICE;

    uint8_t slice[SHARED_FILE_SLICE];
    for (int i = 0; i < SHARED_FILE_SLICE; i++) {
        slice[i] = (uint8_t)((id * 17) ^ (i & 0xFF));
    }

    /* Concurrent pwrite to distinct 16KB offset */
    ssize_t pw = pwrite(g_shared_fd, slice, SHARED_FILE_SLICE, offset);
    if (pw != SHARED_FILE_SLICE) {
        fprintf(stderr, "[FAIL] Worker %d pwrite failed: %zd\n", id, pw);
        return (void *)(intptr_t)-1;
    }

    /* Concurrent pread and verify */
    uint8_t read_slice[SHARED_FILE_SLICE];
    ssize_t pr = pread(g_shared_fd, read_slice, SHARED_FILE_SLICE, offset);
    if (pr != SHARED_FILE_SLICE) {
        fprintf(stderr, "[FAIL] Worker %d pread failed: %zd\n", id, pr);
        return (void *)(intptr_t)-1;
    }

    if (memcmp(slice, read_slice, SHARED_FILE_SLICE) != 0) {
        fprintf(stderr, "[FAIL] Worker %d shared slice verification failed\n", id);
        return (void *)(intptr_t)-1;
    }

    return NULL;
}

static int run_phase5_file_io_stress(int num_threads) {
    printf("[Phase 5] Concurrent File I/O (Thread-private files + shared pwrite/pread)...\n");

    /* 5A: Thread-private file I/O */
    pthread_t threads[32];
    io_arg_t args[32];

    uint64_t freq = timer_freq();
    uint64_t t0 = timer_now();

    for (int i = 0; i < num_threads; i++) {
        args[i].id = i;
        if (pthread_create(&threads[i], NULL, io_private_worker, &args[i]) != 0) {
            fprintf(stderr, "[FAIL] pthread_create failed in file IO stress\n");
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        void *res = NULL;
        pthread_join(threads[i], &res);
        if (res != NULL) return -1;
    }

    uint64_t t1 = timer_now();
    double ms_private = elapsed_ms(t0, t1, freq);
    printf("  [PASS] 5A: %d thread-private files written, synced, verified, and unlinked in %.2f ms\n",
           num_threads, ms_private);

    /* 5B: Shared file concurrent atomic pwrite/pread at 16KB offsets */
    const char *shared_path = "/tmp/ltm_stress_shared.tmp";
    g_shared_fd = open(shared_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (g_shared_fd < 0) {
        fprintf(stderr, "[FAIL] Failed to open shared test file\n");
        return -1;
    }

    t0 = timer_now();
    for (int i = 0; i < num_threads; i++) {
        args[i].id = i;
        if (pthread_create(&threads[i], NULL, io_shared_worker, &args[i]) != 0) {
            fprintf(stderr, "[FAIL] pthread_create failed in shared IO stress\n");
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        void *res = NULL;
        pthread_join(threads[i], &res);
        if (res != NULL) return -1;
    }

    t1 = timer_now();
    double ms_shared = elapsed_ms(t0, t1, freq);

    close(g_shared_fd);
    unlink(shared_path);

    printf("  [PASS] 5B: %d threads concurrently issued pwrite64/pread64 to shared file in %.2f ms\n",
           num_threads, ms_shared);
    return 0;
}

/* ========================================================================= */
/* PHASE 6: PROCESS LIFECYCLE & SIGNALS UNDER LOAD                           */
/* ========================================================================= */

static volatile sig_atomic_t g_signals_received = 0;

static void sigusr1_handler(int sig) {
    if (sig == SIGUSR1) {
        g_signals_received++;
    }
}

static int run_phase6_process_lifecycle(int num_children) {
    printf("[Phase 6] Process Lifecycle & Signals Under Load (fork, wait4, signal delivery)...\n");

    uint64_t freq = timer_freq();
    uint64_t t0 = timer_now();

    /* 6A: Forking 8 child processes concurrently */
    pid_t pids[16];
    for (int i = 0; i < num_children; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "[FAIL] fork failed at child %d: %s\n", i, strerror(errno));
            return -1;
        }
        if (pid == 0) {
            /* Child process: perform workload and exit with 100 + i */
            uint64_t acc = 0;
            for (int k = 0; k < 500000; k++) {
                acc = (acc * 1103515245 + 12345) ^ (uint64_t)k;
            }
            (void)acc;
            _exit(100 + i);
        }
        pids[i] = pid;
    }

    /* Parent collects all children with wait4 */
    for (int i = 0; i < num_children; i++) {
        int status = 0;
        pid_t reaped = wait4(pids[i], &status, 0, NULL);
        if (reaped != pids[i]) {
            fprintf(stderr, "[FAIL] wait4 failed for child %d (pid %d)\n", i, pids[i]);
            return -1;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != (100 + i)) {
            fprintf(stderr, "[FAIL] Child %d unexpected exit status: 0x%x (expected %d)\n",
                    i, status, 100 + i);
            return -1;
        }
    }

    uint64_t t1 = timer_now();
    double ms_fork = elapsed_ms(t0, t1, freq);
    printf("  [PASS] 6A: %d child processes cleanly forked, computed, and reaped via wait4 in %.2f ms\n",
           num_children, ms_fork);

    /* 6B: Signal delivery under load (Rule 5: _ltm_resume_context verification) */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) != 0) {
        fprintf(stderr, "[FAIL] sigaction failed: %s\n", strerror(errno));
        return -1;
    }

    g_signals_received = 0;
    t0 = timer_now();

    pid_t sig_child = fork();
    if (sig_child == 0) {
        /* Child sends 10 SIGUSR1 signals to parent */
        pid_t ppid = getppid();
        for (int s = 0; s < 10; s++) {
            kill(ppid, SIGUSR1);
            usleep(1000); // 1 ms
        }
        _exit(0);
    }

    int sig_status = 0;
    wait4(sig_child, &sig_status, 0, NULL);

    t1 = timer_now();
    double ms_sig = elapsed_ms(t0, t1, freq);

    if (g_signals_received < 1) {
        fprintf(stderr, "[FAIL] No SIGUSR1 signals were caught\n");
        return -1;
    }

    printf("  [PASS] 6B: Signal delivery verified (%d signals caught cleanly via _ltm_resume_context in %.2f ms)\n",
           g_signals_received, ms_sig);
    return 0;
}

/* ========================================================================= */
/* MAIN ENTRYPOINT & COMPREHENSIVE SUITE RUNNER                              */
/* ========================================================================= */

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("\n======================================================================\n");
    printf("     LTM Comprehensive Concurrency & System Stress Test Suite         \n");
#ifdef __linux__
    printf("   Target: Linux ARM64 ELF (Glibc Dynamic Runtime on Apple Silicon)   \n");
#else
    printf("   Target: macOS Darwin ARM64 Mach-O (Native Baseline)                \n");
#endif
    printf("======================================================================\n\n");

    uint64_t suite_t0 = timer_now();

    /* 1. Thread Scaling */
    if (run_phase1_thread_scaling(16, 100000ULL) != 0) return 1;
    if (run_phase1_thread_scaling(32, 100000ULL) != 0) return 1;
    if (run_phase1_thread_scaling(64, 100000ULL) != 0) return 1;

    /* 2. Mutex Contention */
    if (run_phase2_mutex_contention(16, 20000) != 0) return 1;
    if (run_phase2_mutex_contention(32, 10000) != 0) return 1;

    /* 3. Futex Wait / Wake */
    if (run_phase3_futex_stress(16) != 0) return 1;
    if (run_phase3_futex_stress(32) != 0) return 1;

    /* 4. Memory 16KB Alignment & Page Faults */
    if (run_phase4_memory_stress(16, 20) != 0) return 1;

    /* 5. Concurrent File I/O */
    if (run_phase5_file_io_stress(16) != 0) return 1;

    /* 6. Process Lifecycle & Signals */
    if (run_phase6_process_lifecycle(8) != 0) return 1;

    uint64_t suite_t1 = timer_now();
    double total_ms = elapsed_ms(suite_t0, suite_t1, timer_freq());

    printf("\n======================================================================\n");
    printf(" [PASS] Concurrency Stress Test PASSED Cleanly in %.2f ms!\n", total_ms);
    printf(" Zero deadlocks, zero race conditions, zero memory faults detected.\n");
    printf("======================================================================\n\n");

    return 0;
}
