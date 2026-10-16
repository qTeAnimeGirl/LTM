#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <ucontext.h>

#define NUM_NORMAL_THREADS 16
#define THREAD_ITERS 2000
#define NUM_ASYNC_THREADS 8
#define ASYNC_THREAD_ITERS 1000

static atomic_int g_async_signals_caught = 0;
static atomic_int g_async_signal_errors = 0;
static atomic_int g_thread_errors = 0;
static pthread_mutex_t g_test_mutex = PTHREAD_MUTEX_INITIALIZER;

/* --- Phase 1: Normal Multi-Threaded x18 Virtualization --- */

typedef struct {
    int thread_id;
    int iters;
} thread_test_arg_t;

static void *normal_thread_worker(void *arg) {
    thread_test_arg_t *targ = (thread_test_arg_t *)arg;
    int id = targ->thread_id;
    int iters = targ->iters;

    for (int i = 0; i < iters; i++) {
        uint64_t expected = 0xCAFE000000000000ULL | ((uint64_t)id << 32) | (uint64_t)i;
        
        /* Write expected value to x18 */
        __asm__ volatile("mov x18, %0" : : "r"(expected) : "x18");

        /* Interleave with syscalls, yield, and mutex contention to trigger scheduler preemption */
        if ((i % 10) == 0) {
            sched_yield();
        } else if ((i % 25) == 0) {
            pthread_mutex_lock(&g_test_mutex);
            pthread_mutex_unlock(&g_test_mutex);
        } else if ((i % 50) == 0) {
            syscall(SYS_getpid);
        }

        /* Read back x18 value */
        uint64_t actual = 0;
        __asm__ volatile("mov %0, x18" : "=r"(actual));

        if (actual != expected) {
            atomic_fetch_add(&g_thread_errors, 1);
            fprintf(stderr, "[-] Thread %d iter %d: x18 MISMATCH! Expected 0x%016llx, got 0x%016llx\n",
                    id, i, (unsigned long long)expected, (unsigned long long)actual);
            return (void *)(intptr_t)-1;
        }
    }

    return NULL;
}

static int test_normal_threads(void) {
    printf("[Phase 1] Testing x18 Virtualization Across %d Normal Threads (%d iters each = %d ops)...\n",
           NUM_NORMAL_THREADS, THREAD_ITERS, NUM_NORMAL_THREADS * THREAD_ITERS);

    pthread_t threads[NUM_NORMAL_THREADS];
    thread_test_arg_t args[NUM_NORMAL_THREADS];

    for (int i = 0; i < NUM_NORMAL_THREADS; i++) {
        args[i].thread_id = i + 1;
        args[i].iters = THREAD_ITERS;
        if (pthread_create(&threads[i], NULL, normal_thread_worker, &args[i]) != 0) {
            perror("pthread_create");
            return -1;
        }
    }

    for (int i = 0; i < NUM_NORMAL_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    if (atomic_load(&g_thread_errors) != 0) {
        printf("  [FAIL] %d x18 register mismatches detected in normal threads!\n",
               atomic_load(&g_thread_errors));
        return -1;
    }

    printf("  [PASS] %d threads completed %d operations with zero x18 corruption!\n",
           NUM_NORMAL_THREADS, NUM_NORMAL_THREADS * THREAD_ITERS);
    return 0;
}

/* --- Phase 2: Asynchronous Signal x18 Preservation --- */

static void sigalrm_handler(int sig, siginfo_t *si, void *ctx) {
    (void)sig;
    (void)si;
    ucontext_t *uc = (ucontext_t *)ctx;
    
    if (uc) {
        uint64_t sig_x18 = uc->uc_mcontext.regs[18];
        /* Check that upper marker matches 0x55AA000000000000 */
        if ((sig_x18 & 0xFFFF000000000000ULL) == 0x55AA000000000000ULL) {
            atomic_fetch_add(&g_async_signals_caught, 1);
        } else if (sig_x18 == 0) {
            /* x18 was cleared to zero by host Darwin XNU! */
            atomic_fetch_add(&g_async_signal_errors, 1);
        }
    }
}

static int test_async_signals(void) {
    printf("[Phase 2] Testing x18 Virtualization Under Rapid Asynchronous Signals (ITIMER_REAL @ 1ms)...\n");

    atomic_store(&g_async_signals_caught, 0);
    atomic_store(&g_async_signal_errors, 0);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigalrm_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, NULL) != 0) {
        perror("sigaction");
        return -1;
    }

    /* Start interval timer: 1ms initial, 1ms interval */
    struct itimerval it;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 1000;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 1000;

    if (setitimer(ITIMER_REAL, &it, NULL) != 0) {
        perror("setitimer");
        return -1;
    }

    /* Loop with x18 continuously until at least 30 signals caught or 2000 iterations */
    int loop_mismatches = 0;
    for (int i = 0; i < 2000; i++) {
        uint64_t expected = 0x55AA000000000000ULL | (uint64_t)(i & 0xFFFFFFFF);
        __asm__ volatile("mov x18, %0" : : "r"(expected) : "x18");

        usleep(500); /* 500us gives timer interrupt time to deliver while x18 is live */

        uint64_t actual = 0;
        __asm__ volatile("mov %0, x18" : "=r"(actual));

        if (actual != expected) {
            if (loop_mismatches < 3) {
                fprintf(stderr, "[-] Loop iter %d: expected 0x%016llx, got 0x%016llx\n",
                        i, (unsigned long long)expected, (unsigned long long)actual);
            }
            loop_mismatches++;
        }

        if (atomic_load(&g_async_signals_caught) >= 30) {
            break; /* Caught enough signals to validate */
        }
    }

    /* Disable timer */
    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_REAL, &it, NULL);

    /* Restore default signal action */
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    int caught = atomic_load(&g_async_signals_caught);
    int sig_errors = atomic_load(&g_async_signal_errors);

    printf("  -> Asynchronous signals caught: %d (sigframe x18 zero-clobber errors: %d, loop mismatches: %d)\n",
           caught, sig_errors, loop_mismatches);

    if (sig_errors > 0 || loop_mismatches > 0) {
        printf("  [FAIL] x18 corruption detected during async signal delivery!\n");
        return -1;
    }
    if (caught < 5) {
        printf("  [WARN] Too few signals caught (%d), but no corruption detected.\n", caught);
    } else {
        printf("  [PASS] %d asynchronous signals intercepted and resumed with 100%% x18 integrity!\n", caught);
    }

    return 0;
}

/* --- Phase 3: Combined Multi-Threaded Execution Under Async Signal Load --- */

static atomic_bool g_stop_async_load = false;

static void *async_threaded_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    int mismatches = 0;

    for (int i = 0; i < ASYNC_THREAD_ITERS && !atomic_load(&g_stop_async_load); i++) {
        uint64_t expected = 0xBEEF000000000000ULL | ((uint64_t)id << 32) | (uint64_t)i;
        __asm__ volatile("mov x18, %0" : : "r"(expected) : "x18");

        if ((i % 20) == 0) {
            sched_yield();
        }

        uint64_t actual = 0;
        __asm__ volatile("mov %0, x18" : "=r"(actual));

        if (actual != expected) {
            mismatches++;
            atomic_fetch_add(&g_thread_errors, 1);
        }
    }

    return (void *)(intptr_t)mismatches;
}

static void sigusr1_handler(int sig, siginfo_t *si, void *ctx) {
    (void)sig;
    (void)si;
    (void)ctx;
    atomic_fetch_add(&g_async_signals_caught, 1);
}

static int test_async_threaded_load(void) {
    printf("[Phase 3] Testing Combined x18 Virtualization (%d Threads Concurrently Under Async Signal Blasts)...\n",
           NUM_ASYNC_THREADS);

    atomic_store(&g_thread_errors, 0);
    atomic_store(&g_async_signals_caught, 0);
    atomic_store(&g_stop_async_load, false);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigusr1_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    pthread_t threads[NUM_ASYNC_THREADS];
    for (int i = 0; i < NUM_ASYNC_THREADS; i++) {
        pthread_create(&threads[i], NULL, async_threaded_worker, (void *)(intptr_t)(i + 1));
    }

    /* Blast SIGUSR1 signals to self while worker threads are executing */
    for (int i = 0; i < 50; i++) {
        kill(getpid(), SIGUSR1);
        usleep(1000); /* 1ms sleep */
    }

    atomic_store(&g_stop_async_load, true);

    for (int i = 0; i < NUM_ASYNC_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    int caught = atomic_load(&g_async_signals_caught);
    int errs = atomic_load(&g_thread_errors);

    printf("  -> Concurrent signals caught: %d, Thread x18 mismatches: %d\n", caught, errs);

    if (errs > 0) {
        printf("  [FAIL] Thread x18 corruption occurred under concurrent signal load!\n");
        return -1;
    }

    printf("  [PASS] Concurrent threads and async signals verified with 100%% x18 isolation!\n");
    return 0;
}

int main(void) {
    printf("======================================================================\n");
    printf("   LTM x18 Register Real-Time Virtualization Test Suite (Apple Silicon) \n");
    printf("   Target: Linux ARM64 ELF (Asynchronous Signals & Multi-Threading)   \n");
    printf("======================================================================\n\n");

    if (test_normal_threads() != 0) return 1;
    if (test_async_signals() != 0) return 1;
    if (test_async_threaded_load() != 0) return 1;

    printf("\n======================================================================\n");
    printf(" [PASS] All x18 Virtualization (Async Signals & Normal Threads) Tests Passed Cleanly!\n");
    printf("======================================================================\n");
    return 0;
}
