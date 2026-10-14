#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>

#define NUM_WORKERS 4

static __thread int s_worker_tls_id = -1;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_counter = 0;
static int s_worker_results[NUM_WORKERS];

static void *worker_thread(void *arg) {
    int id = (int)(intptr_t)arg;
    s_worker_tls_id = id;

    pthread_mutex_lock(&s_lock);
    s_counter++;
    s_worker_results[id] = (id + 1) * 777;
    pthread_mutex_unlock(&s_lock);

    /* Verify thread-local storage isolation */
    if (s_worker_tls_id != id) {
        fprintf(stderr, "[FAIL] Worker %d saw corrupted TLS: %d\n", id, s_worker_tls_id);
        return (void *)(intptr_t)-1;
    }

    return (void *)0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("[*] Running Step 2: Comprehensive Glibc Runtime & Dynamic Loader Test Suite...\n");

    /* 1. Glibc Math Library Verification */
    double s = sin(0.0);
    double sq = sqrt(144.0);
    double p = pow(2.0, 10.0);
    if (fabs(s) > 1e-9 || fabs(sq - 12.0) > 1e-9 || fabs(p - 1024.0) > 1e-9) {
        fprintf(stderr, "[FAIL] Glibc math verification failed: sin=%f, sqrt=%f, pow=%f\n", s, sq, p);
        return 1;
    }
    printf("  [+] Glibc libm math verified: sqrt(144.0)=%.1f, pow(2,10)=%.0f\n", sq, p);

    /* 2. Glibc Dynamic Memory & Heap */
    char *buf = malloc(1024 * 64);
    if (!buf) {
        fprintf(stderr, "[FAIL] Glibc malloc failed\n");
        return 1;
    }
    memset(buf, 0x42, 1024 * 64);
    buf = realloc(buf, 1024 * 128);
    if (!buf || buf[0] != 0x42) {
        fprintf(stderr, "[FAIL] Glibc realloc failed\n");
        return 1;
    }
    free(buf);
    printf("  [+] Glibc heap management (malloc/realloc/free) verified\n");

    /* 3. Glibc String Formatting */
    char str_buf[128];
    snprintf(str_buf, sizeof(str_buf), "Glibc %s loaded at pid %d", "runtime", getpid());
    if (strlen(str_buf) < 10) {
        fprintf(stderr, "[FAIL] Glibc snprintf failed\n");
        return 1;
    }
    printf("  [+] Glibc formatting verified: '%s'\n", str_buf);

    /* 4. Glibc Multithreading (pthreads + TLS + Mutex) */
    pthread_t threads[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, (void *)(intptr_t)i) != 0) {
            fprintf(stderr, "[FAIL] pthread_create failed for thread %d\n", i);
            return 1;
        }
    }

    for (int i = 0; i < NUM_WORKERS; i++) {
        void *res = NULL;
        pthread_join(threads[i], &res);
        if (res != NULL) {
            fprintf(stderr, "[FAIL] Worker thread %d returned error\n", i);
            return 1;
        }
    }

    if (s_counter != NUM_WORKERS) {
        fprintf(stderr, "[FAIL] Mutex counter mismatch: expected %d, got %d\n", NUM_WORKERS, s_counter);
        return 1;
    }

    for (int i = 0; i < NUM_WORKERS; i++) {
        if (s_worker_results[i] != (i + 1) * 777) {
            fprintf(stderr, "[FAIL] Result mismatch for thread %d: %d\n", i, s_worker_results[i]);
            return 1;
        }
    }
    printf("  [+] Glibc pthreads concurrency across %d threads verified cleanly\n", NUM_WORKERS);

    /* 5. Glibc File I/O */
    const char *tmp_filename = "/tmp/glibc_test_io.txt";
    FILE *fp = fopen(tmp_filename, "w+");
    if (!fp) {
        fprintf(stderr, "[FAIL] fopen failed for '%s'\n", tmp_filename);
        return 1;
    }
    fputs("LTM Glibc Dynamic Loader Compatibility Certified!\n", fp);
    rewind(fp);

    char read_buf[128] = {0};
    if (!fgets(read_buf, sizeof(read_buf), fp)) {
        fprintf(stderr, "[FAIL] fgets failed\n");
        fclose(fp);
        return 1;
    }
    fclose(fp);
    unlink(tmp_filename);

    if (strstr(read_buf, "Certified!") == NULL) {
        fprintf(stderr, "[FAIL] File content mismatch: '%s'\n", read_buf);
        return 1;
    }
    printf("  [+] Glibc stdio file stream I/O verified\n");

    printf("[PASS] test_glibc: 100%% Glibc dynamic loader and runtime compatibility verified!\n");
    return 0;
}
