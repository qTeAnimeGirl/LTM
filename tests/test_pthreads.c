#include "include/ltm_test.h"

#define CLONE_VM             0x00000100
#define CLONE_FS             0x00000200
#define CLONE_FILES          0x00000400
#define CLONE_SIGHAND        0x00000800
#define CLONE_THREAD         0x00010000
#define CLONE_SYSVSEM        0x00040000
#define CLONE_SETTLS         0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID   0x01000000

#define FUTEX_WAIT           0
#define FUTEX_WAKE           1

#define NUM_THREADS 4
#define STACK_SIZE (64 * 1024)

static volatile int g_start_gate = 0;
static volatile int g_shared_results[NUM_THREADS];
static volatile int g_shared_tids[NUM_THREADS];
static volatile uintptr_t g_shared_tls[NUM_THREADS];
static volatile int g_atomic_counter = 0;

struct thread_data {
    int id;
    uintptr_t expected_tls;
};

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

static int thread_worker(void *arg) {
    struct thread_data *td = (struct thread_data *)arg;
    int id = td->id;

    // 1. Wait for start gate to open
    while (g_start_gate == 0) {
        sys_futex((int *)&g_start_gate, FUTEX_WAIT, 0, NULL, NULL, 0);
    }

    // 2. Read virtualized TPIDR_EL0
    uintptr_t read_tls = 0;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(read_tls));
    g_shared_tls[id] = read_tls;

    // 3. Record TID
    g_shared_tids[id] = sys_gettid();

    // 4. Record result
    g_shared_results[id] = (id + 1) * 1111;

    // 5. Atomically increment counter
    __atomic_fetch_add(&g_atomic_counter, 1, __ATOMIC_SEQ_CST);

    return 0; // ltm_clone stub calls sys_exit(0)
}

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    ltm_print("[TEST] test_pthreads: Validating multi-threaded clone and futex synchronization...\n");

    int main_pid = sys_getpid();
    int main_tid = sys_gettid();
    TEST_ASSERT(main_pid == main_tid, "Main thread PID must equal TID");

    int ptids[NUM_THREADS] = {0};
    int ctids[NUM_THREADS] = {0};
    void *stacks[NUM_THREADS] = {0};
    struct thread_data tdata[NUM_THREADS];

    // 1. Case A: Spawn NUM_THREADS concurrent worker threads
    uint64_t clone_flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
                           CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS |
                           CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

    for (int i = 0; i < NUM_THREADS; i++) {
        tdata[i].id = i;
        tdata[i].expected_tls = 0x10000000ULL + (uintptr_t)(i + 1) * 0x1000ULL;

        stacks[i] = sys_mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        TEST_ASSERT(stacks[i] != MAP_FAILED && stacks[i] != NULL, "mmap failed for thread stack");
        void *stack_top = (void *)((uintptr_t)stacks[i] + STACK_SIZE - 16);

        long res = ltm_clone(thread_worker, stack_top, clone_flags, &tdata[i], &ptids[i], (void *)tdata[i].expected_tls, &ctids[i]);
        TEST_ASSERT(res > 0, "ltm_clone failed to spawn thread");
        TEST_ASSERT(ptids[i] == (int)res, "CLONE_PARENT_SETTID mismatch");
        TEST_ASSERT(ptids[i] != main_pid, "Worker TID must not equal main PID");
        TEST_ASSERT(ctids[i] == (int)res, "CLONE_CHILD_SETTID mismatch");
    }

    ltm_print("  Step 1: Successfully created 4 threads with separate stacks and distinct TIDs.\n");

    // Open start gate to unleash threads simultaneously
    g_start_gate = 1;
    sys_futex((int *)&g_start_gate, FUTEX_WAKE, NUM_THREADS, NULL, NULL, 0);

    // 2. Join all worker threads by waiting on ctids via futex
    for (int i = 0; i < NUM_THREADS; i++) {
        while (ctids[i] != 0) {
            sys_futex(&ctids[i], FUTEX_WAIT, ctids[i], NULL, NULL, 0);
        }
        // Verify thread completed and wrote correct data
        TEST_ASSERT(g_shared_results[i] == (i + 1) * 1111, "Thread shared result mismatch");
        TEST_ASSERT(g_shared_tls[i] == (0x10000000ULL + (uintptr_t)(i + 1) * 0x1000ULL), "Thread TLS mismatch");
        TEST_ASSERT(g_shared_tids[i] == ptids[i], "Thread reported TID mismatch");
    }

    TEST_ASSERT(g_atomic_counter == NUM_THREADS, "Atomic counter mismatch");
    ltm_print("  Step 2: All 4 threads joined. Shared memory, atomic counters, and per-thread TLS verified!\n");

    // 3. Case B: Boundary Cases (clone with size 64 clone3 or repeated joins)
    struct clone_args ca;
    ltm_memset(&ca, 0, sizeof(ca));
    ca.flags = CLONE_VM | CLONE_THREAD;
    ca.stack = 0; // Invalid stack with CLONE_VM
    long bad_c3 = sys_clone3(&ca, sizeof(ca));
    TEST_ASSERT(bad_c3 == -EINVAL, "clone3 with CLONE_VM and NULL stack must return -EINVAL");

    // 4. Case C: Error Handling & Errno Mapping
    long bad_clone = sys_clone(CLONE_VM | CLONE_THREAD, NULL, NULL, NULL, NULL);
    TEST_ASSERT(bad_clone == -EINVAL, "clone with CLONE_VM and NULL stack must return -EINVAL");

    int bad_tkill = sys_tgkill(main_pid, 999999, 0);
    TEST_ASSERT(bad_tkill == -ESRCH, "tgkill with invalid tid must return -ESRCH");

    int bad_futex = sys_futex(NULL, FUTEX_WAIT, 0, NULL, NULL, 0);
    TEST_ASSERT(bad_futex == -EFAULT, "futex with NULL uaddr must return -EFAULT");

    int bad_futex_align = sys_futex((int *)((uintptr_t)&g_start_gate | 1), FUTEX_WAIT, 0, NULL, NULL, 0);
    TEST_ASSERT(bad_futex_align == -EINVAL, "futex with unaligned uaddr must return -EINVAL");

    TEST_PASS("test_pthreads passed! Multi-threading verified with 100% accuracy.");
    return 0;
}
