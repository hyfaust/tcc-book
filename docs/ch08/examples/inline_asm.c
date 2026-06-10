/*
 * inline_asm.c - Inline assembly examples for various architectures
 *
 * Demonstrates GCC-style inline assembly as supported by TCC.
 * Each function uses #if to select the appropriate architecture.
 */
#include <stdio.h>
#include <string.h>

/* ========== x86-64 specific examples ========== */
#if defined(__x86_64__) || defined(_M_X64)

/*
 * Example 1: CPUID - Get CPU vendor string
 * Uses the CPUID instruction to query CPU information.
 */
void get_cpu_vendor(char *vendor)
{
    unsigned int eax_val, ebx_val, ecx_val, edx_val;

    asm volatile(
        "cpuid"
        : "=a"(eax_val), "=b"(ebx_val), "=c"(ecx_val), "=d"(edx_val)
        : "a"(0)  /* EAX=0: get vendor string */
    );

    /* Vendor string is in EBX+EDX+ECX (12 bytes) */
    memcpy(vendor, &ebx_val, 4);
    memcpy(vendor + 4, &edx_val, 4);
    memcpy(vendor + 8, &ecx_val, 4);
    vendor[12] = '\0';
}

/*
 * Example 2: Atomic compare-and-swap
 * Implements a spinlock using lock cmpxchg.
 */
typedef struct { volatile int locked; } spinlock_t;

static inline void spin_lock(spinlock_t *lock)
{
    int expected = 0;
    asm volatile(
        "1: lock cmpxchg %1, %0\n\t"
        "jnz 1b"
        : "+m"(lock->locked), "+a"(expected)
        : "r"(1)
        : "memory", "cc"
    );
}

static inline void spin_unlock(spinlock_t *lock)
{
    asm volatile(
        "movl $0, %0"
        : "=m"(lock->locked)
        :
        : "memory"
    );
}

/*
 * Example 3: RDTSC - Read timestamp counter
 */
static inline unsigned long long rdtsc(void)
{
    unsigned int lo, hi;
    asm volatile(
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return ((unsigned long long)hi << 32) | lo;
}

/*
 * Example 4: BSF (Bit Scan Forward) - Find lowest set bit
 */
static inline int find_lowest_bit(unsigned long val)
{
    int result;
    asm volatile(
        "bsf %1, %0"
        : "=r"(result)
        : "r"(val)
        : "cc"
    );
    return result;
}

/*
 * Example 5: Using memory operands
 */
static inline void atomic_add(volatile int *ptr, int val)
{
    asm volatile(
        "lock addl %1, %0"
        : "+m"(*ptr)
        : "r"(val)
        : "memory"
    );
}

#define ARCH_NAME "x86-64"

/* ========== ARM64 (AArch64) specific examples ========== */
#elif defined(__aarch64__) || defined(_M_ARM64)

/*
 * Example 1: Read CPU ID register
 */
unsigned long read_ctr_el0(void)
{
    unsigned long val;
    asm volatile("mrs %0, ctr_el0" : "=r"(val));
    return val;
}

/*
 * Example 2: Atomic compare-and-swap using LDXR/STXR
 */
static inline long atomic_cas(volatile long *ptr, long oldval, long newval)
{
    long result;
    asm volatile(
        "1: ldxr %0, [%1]\n\t"
        "   cmp  %0, %2\n\t"
        "   b.ne 2f\n\t"
        "   stxr %w0, %3, [%1]\n\t"
        "   cbnz %w0, 1b\n\t"
        "   mov  %0, #1\n\t"
        "   b    3f\n\t"
        "2: clrex\n\t"
        "   mov  %0, #0\n\t"
        "3:\n\t"
        : "=&r"(result)
        : "r"(ptr), "r"(oldval), "r"(newval)
        : "memory", "cc"
    );
    return result;
}

/*
 * Example 3: DMB (Data Memory Barrier)
 */
static inline void dmb_sy(void)
{
    asm volatile("dmb sy" ::: "memory");
}

/*
 * Example 4: NOP and yield
 */
static inline void cpu_relax(void)
{
    asm volatile("yield" ::: "memory");
}

/*
 * Example 5: Cache line operations
 */
static inline void dc_cvac(unsigned long addr)
{
    asm volatile("dc cvac, %0" :: "r"(addr) : "memory");
}

#define ARCH_NAME "AArch64"

/* ========== RISC-V 64 specific examples ========== */
#elif defined(__riscv) && __riscv_xlen == 64

/*
 * Example 1: Read cycle counter
 */
static inline unsigned long long rdtime(void)
{
    unsigned long long val;
    asm volatile("rdtime %0" : "=r"(val));
    return val;
}

/*
 * Example 2: Memory fence
 */
static inline void fence_rw_rw(void)
{
    asm volatile("fence rw, rw" ::: "memory");
}

/*
 * Example 3: Atomic LR/SC (Load-Reserved/Store-Conditional)
 */
static inline long atomic_cas(volatile long *ptr, long oldval, long newval)
{
    long result;
    int status;
    asm volatile(
        "1: lr.d    %0, %2\n\t"
        "   bne     %0, %3, 2f\n\t"
        "   sc.d    %1, %4, %2\n\t"
        "   bnez    %1, 1b\n\t"
        "   li      %0, 1\n\t"
        "   j       3f\n\t"
        "2: li      %0, 0\n\t"
        "3:\n\t"
        : "=&r"(result), "=&r"(status), "+A"(*ptr)
        : "r"(oldval), "r"(newval)
        : "memory"
    );
    return result;
}

/*
 * Example 4: NOP
 */
static inline void nop(void)
{
    asm volatile("nop");
}

/*
 * Example 5: Read CSR
 */
static inline unsigned long read_mvendorid(void)
{
    unsigned long val;
    asm volatile("csrr %0, mvendorid" : "=r"(val));
    return val;
}

#define ARCH_NAME "RISC-V 64"

/* ========== ARM 32-bit specific examples ========== */
#elif defined(__arm__)

/*
 * Example 1: SWI (Software Interrupt) for system call
 */
static inline int syscall0(int nr)
{
    register int r7 asm("r7") = nr;
    register int r0 asm("r0");
    asm volatile("swi #0" : "=r"(r0) : "r"(r7) : "memory");
    return r0;
}

/*
 * Example 2: MRS - Read CPSR
 */
static inline unsigned int read_cpsr(void)
{
    unsigned int val;
    asm volatile("mrs %0, cpsr" : "=r"(val));
    return val;
}

/*
 * Example 3: Disable/Enable IRQs
 */
static inline unsigned int disable_irq(void)
{
    unsigned int cpsr, new_cpsr;
    asm volatile(
        "mrs %0, cpsr\n\t"
        "orr %1, %0, #0x80\n\t"
        "msr cpsr_c, %1"
        : "=r"(cpsr), "=r"(new_cpsr)
        :
        : "memory"
    );
    return cpsr;
}

static inline void restore_cpsr(unsigned int cpsr)
{
    asm volatile(
        "msr cpsr_c, %0"
        :
        : "r"(cpsr)
        : "memory"
    );
}

/*
 * Example 4: CLZ (Count Leading Zeros)
 */
static inline int clz(unsigned int val)
{
    int result;
    asm volatile("clz %0, %1" : "=r"(result) : "r"(val));
    return result;
}

#define ARCH_NAME "ARM 32-bit"

#else
#error "Unsupported architecture for inline assembly examples"
#endif

/* ========== Common test code ========== */

int main(void)
{
    printf("Inline Assembly Examples (%s)\n", ARCH_NAME);
    printf("=============================\n\n");

#if defined(__x86_64__) || defined(_M_X64)
    /* CPUID */
    char vendor[13];
    get_cpu_vendor(vendor);
    printf("CPU Vendor: %s\n", vendor);

    /* Timestamp counter */
    unsigned long long t1 = rdtsc();
    volatile int i;
    volatile int sum = 0;
    for (i = 0; i < 1000000; i++) sum += i;
    unsigned long long t2 = rdtsc();
    printf("Loop cycles (approx): %llu\n", t2 - t1);
    printf("Sum: %d\n", sum);

    /* Bit scan */
    unsigned long bits = 0b10100000;
    printf("Lowest set bit in 0x%lx: %d\n", bits, find_lowest_bit(bits));

    /* Atomic add */
    volatile int counter = 0;
    atomic_add(&counter, 42);
    printf("After atomic_add(42): %d\n", counter);

    /* Spinlock demo */
    spinlock_t lock = { .locked = 0 };
    spin_lock(&lock);
    printf("Lock acquired\n");
    spin_unlock(&lock);
    printf("Lock released\n");

#elif defined(__aarch64__) || defined(_M_ARM64)
    unsigned long ctr = read_ctr_el0();
    printf("CTR_EL0: 0x%lx\n", ctr);
    dmb_sy();
    printf("Data memory barrier executed\n");
    cpu_relax();
    printf("CPU relax executed\n");

#elif defined(__riscv) && __riscv_xlen == 64
    unsigned long long t = rdtime();
    printf("Timer: %llu\n", t);
    fence_rw_rw();
    printf("Fence executed\n");
    nop();
    printf("NOP executed\n");

#elif defined(__arm__)
    unsigned int cpsr = read_cpsr();
    printf("CPSR: 0x%08x\n", cpsr);
    printf("CLZ(0xFF000000) = %d\n", clz(0xFF000000));
#endif

    printf("\nAll examples completed successfully.\n");
    return 0;
}
