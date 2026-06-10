# 第七章 运行时与嵌入式API

TinyCC 不仅仅是一个编译器——它同时提供了一个完整的运行时支持库和一套嵌入式编程 API（libtcc），使得用户可以在自己的 C 程序中嵌入一个编译器实例。本章将深入探讨 TinyCC 的运行时基础设施和 libtcc 嵌入式 API 的方方面面。

---

## 7.1 运行时支持库 libtcc1.a

每一个 C 编译器都需要一个运行时支持库来提供目标硬件无法直接支持的操作。对于 GCC，这个库是 `libgcc`；对于 TinyCC，对应的库是 `libtcc1.a`。该库的源码位于 `lib/libtcc1.c`，在构建过程中被编译为静态库并安装到 tcc 的库搜索路径中。

### 7.1.1 为什么需要运行时库

现代处理器的指令集并不能覆盖 C 语言标准库或 ABI 所要求的全部操作。典型的"缺失指令"包括：

- **64 位整数除法**：在 32 位平台（如 i386）上，`long long` 类型的除法和取模无法用单条指令完成。
- **无符号到浮点的转换**：将一个 `unsigned long long` 转换为 `float` 或 `double` 时，不能简单地使用硬件浮点指令，因为中间结果可能溢出。
- **算术移位**：某些平台上对 64 位值的算术右移需要特殊处理。
- **128 位整数支持**：x86-64 ABI 中 `__int128` 类型的辅助函数。

编译器在代码生成阶段检测到这些操作时，会生成对运行时库函数的调用而非直接发射指令。

### 7.1.2 内存操作函数

`libtcc1.c` 中最基础的部分是内存操作函数的实现。当编译器需要内联展开 `memcpy` 或 `memmove` 但判断不值得内联时（例如长度不确定），就会调用这些运行时版本：

```c
/* lib/libtcc1.c 中不直接提供 memcpy/memmove，
   但 libtcc1.a 包含由汇编或编译器内建提供的版本。
   在链接时，这些符号会被解析到 libc 或 libtcc1 的实现。 */
```

在实际的 libtcc1.a 中，memcpy 等函数通常由编译器内建（`__builtin_memcpy`）展开而来，或者在没有 libc 可用的裸机环境中提供独立实现。

### 7.1.3 64 位整数除法

这是 libtcc1.c 中最核心也最复杂的部分。在 32 位 x86 平台上，TCC 无法用单条指令完成 64 位除法，因此需要软件模拟。相关函数包括：

| 函数 | 签名 | 功能 |
|------|------|------|
| `__divdi3` | `long long __divdi3(long long u, long long v)` | 有符号 64 位除法 |
| `__moddi3` | `long long __moddi3(long long u, long long v)` | 有符号 64 位取模 |
| `__udivdi3` | `unsigned long long __udivdi3(unsigned long long u, unsigned long long v)` | 无符号 64 位除法 |
| `__umoddi3` | `unsigned long long __umoddi3(unsigned long long u, unsigned long long v)` | 无符号 64 位取模 |

这些函数的实现都依赖一个核心函数 `__udivmoddi4`，它同时计算商和余数：

```c
/* lib/libtcc1.c - 核心除法算法（简化展示） */
static UDWtype __udivmoddi4(UDWtype n, UDWtype d, UDWtype *rp)
{
    DWunion ww, nn, dd, rr;
    UWtype d0, d1, n0, n1, n2;
    UWtype q0, q1;

    nn.ll = n;
    dd.ll = d;
    d0 = dd.s.low;
    d1 = dd.s.high;
    n0 = nn.s.low;
    n1 = nn.s.high;

    if (d1 == 0) {
        /* 除数为 32 位——使用一到两次硬件除法 */
        if (d0 > n1) {
            udiv_qrnnd(q0, n0, n1, n0, d0);
            q1 = 0;
        } else {
            udiv_qrnnd(q1, n1, 0, n1, d0);
            udiv_qrnnd(q0, n0, n1, n0, d0);
        }
    } else {
        /* 除数为 64 位——需要归一化和多位试商 */
        count_leading_zeros(bm, d1);
        /* ...归一化并执行试商法... */
    }

    ww.s.low = q0;
    ww.s.high = q1;
    return ww.ll;
}
```

关键的辅助宏 `udiv_qrnnd` 和 `umul_ppmm` 利用 i386 的 `divl` 和 `mull` 指令来完成单精度的除法和乘法操作：

```c
/* i386 平台上的汇编辅助宏 */
#define udiv_qrnnd(q, r, n1, n0, dv) \
    __asm__("divl %4" \
        : "=a"((USItype)(q)), "=d"((USItype)(r)) \
        : "0"((USItype)(n0)), "1"((USItype)(n1)), "rm"((USItype)(dv)))

#define umul_ppmm(w1, w0, u, v) \
    __asm__("mull %3" \
        : "=a"((USItype)(w0)), "=d"((USItype)(w1)) \
        : "%0"((USItype)(u)), "rm"((USItype)(v)))
```

有符号版本 `__divdi3` 在调用 `__udivmoddi4` 之前先处理符号：

```c
long long __divdi3(long long u, long long v)
{
    int c = 0;
    DWunion uu, vv;
    uu.ll = u;
    vv.ll = v;

    if (uu.s.high < 0) { c = ~c; uu.ll = -uu.ll; }
    if (vv.s.high < 0) { c = ~c; vv.ll = -vv.ll; }

    DWtype w = __udivmoddi4(uu.ll, vv.ll, NULL);
    return c ? -w : w;
}
```

注意：这些 64 位除法函数**仅在 32 位平台（i386）上需要**。在 x86-64 和 ARM64 等 64 位平台上，硬件原生支持 64 位除法（`idiv` 指令或 ARM 的 `sdiv`/`udiv`），因此 libtcc1.c 通过 `#if defined __i386__` 条件编译保护这些函数。

### 7.1.4 64 位移位操作

在 32 位平台上，对 `long long` 类型的移位操作也需要软件实现：

```c
/* lib/libtcc1.c */
long long __ashrdi3(long long a, int b)   /* 算术右移 */
unsigned long long __lshrdi3(unsigned long long a, int b)  /* 逻辑右移 */
long long __ashldi3(long long a, int b)   /* 左移 */
```

这些函数的实现利用 `DWunion` 联合体来分别访问 64 位值的高 32 位和低 32 位，然后手动执行跨字的移位操作。

### 7.1.5 浮点转换函数

当需要将 64 位无符号整数与浮点数之间相互转换时，存在一个特殊问题：标准的浮点转换指令假定输入是有符号的，对于超过 `LLONG_MAX` 的无符号值会产生错误结果。libtcc1.c 提供了专门的转换函数：

```c
/* 无符号 64 位 → 浮点 */
float __floatundisf(unsigned long long a);
double __floatundidf(unsigned long long a);
long double __floatundixf(unsigned long long a);

/* 浮点 → 无符号 64 位 */
unsigned long long __fixunssfdi(float a);
unsigned long long __fixunsdfdi(double a);
unsigned long long __fixunsxfdi(long double a);

/* 浮点 → 有符号 64 位 */
long long __fixsfdi(float a);
long long __fixdfdi(double a);
long long __fixxfdi(long double a);
```

`__floatundidf` 的实现策略是：如果值的最高位为 0（表示为正数），直接用标准转换；否则先转为 `long double`（其精度足够），再加上 `2^64` 的偏移来补偿符号位的影响：

```c
double __floatundidf(unsigned long long a)
{
    DWunion uu;
    XFtype r;
    uu.ll = a;
    if (uu.s.high >= 0) {
        return (double)uu.ll;
    } else {
        r = (XFtype)uu.ll;
        r += 18446744073709551616.0; /* 2^64 */
        return (double)r;
    }
}
```

### 7.1.6 libtcc1 的构建与安装

在 TCC 的构建系统中，libtcc1.a 的构建规则定义在 `Makefile` 中。编译时使用 `-nostdlib` 和 `-shared` 等选项，确保生成的库不依赖外部 libc 符号。该库被安装到 `CONFIG_TCCDIR`（默认 `/usr/local/lib/tcc`）下。

---

## 7.2 边界检查 bcheck.c

TCC 提供了一个独特的功能：**内置的内存和边界检查器**（bounds checker）。当使用 `-b` 选项编译时，TCC 会在所有内存访问前插入检查代码，运行时由 `lib/bcheck.c` 提供的检查逻辑来验证每次指针操作是否合法。

### 7.2.1 架构概述

边界检查系统由两部分协作完成：

1. **编译时**（`tccgen.c` 中相关代码）：编译器在每次指针算术和内存引用前插入对运行时检查函数的调用。
2. **运行时**（`lib/bcheck.c`）：维护一个用 Splay 树实现的内存区域注册表，每次检查时在树中查找指针是否落在合法区域内。

### 7.2.2 运行时数据结构

bcheck.c 使用 **Splay 树**来管理所有已分配的内存区域：

```c
/* lib/bcheck.c */
typedef struct tree_node Tree;
struct tree_node {
    Tree *left, *right;
    size_t start;    /* 区域起始地址 */
    size_t size;     /* 区域大小 */
    unsigned char type;        /* 分配类型: MALLOC, CALLOC 等 */
    unsigned char is_invalid;  /* 区域外的指针是否无效 */
};
```

Splay 树的选择并非偶然：它具有**自调整**特性——最近访问的节点会被移动到根部。在典型的程序执行模式中，同一个内存区域会被反复访问（例如遍历数组），Splay 树使得这种模式下的摊还时间复杂度为 O(log n)。

### 7.2.3 指针算术检查

编译器插入的核心检查函数是 `__bound_ptr_add`，用于指针算术：

```c
/* lib/bcheck.c */
void *__bound_ptr_add(void *p, size_t offset)
{
    size_t addr = (size_t)p;

    if (NO_CHECKING_GET())
        return p + offset;

    WAIT_SEM();
    if (tree) {
        /* 在 Splay 树中查找 p 所属的区域 */
        addr -= tree->start;
        if (addr >= tree->size) {
            tree = splay((size_t)p, tree);
            addr = (size_t)p - tree->start;
        }
        if (addr <= tree->size) {
            if (tree->is_invalid || addr + offset > tree->size) {
                POST_SEM();
                bound_warning("outside region");
                if (never_fatal <= 0)
                    return INVALID_POINTER;
                return p + offset;
            }
        }
    }
    POST_SEM();
    return p + offset;
}
```

注意 `__bound_ptr_add` 的语义与 `__bound_ptr_indir*` 不同：前者允许指针到达区域的**末尾之后一个字节**（因为 C 语言标准允许指向数组末尾的指针），而后者要求目标地址**严格在区域内**。

### 7.2.4 内存访问检查

对于实际的内存读写，编译器根据访问宽度插入不同的检查函数：

```c
/* lib/bcheck.c */
void *__bound_ptr_indir1(void *p, size_t offset);  /* 1 字节访问 */
void *__bound_ptr_indir2(void *p, size_t offset);  /* 2 字节访问 */
void *__bound_ptr_indir4(void *p, size_t offset);  /* 4 字节访问 */
void *__bound_ptr_indir8(void *p, size_t offset);  /* 8 字节访问 */
void *__bound_ptr_indir12(void *p, size_t offset); /* 12 字节访问 */
void *__bound_ptr_indir16(void *p, size_t offset); /* 16 字节访问 */
```

这些函数通过宏 `BOUND_PTR_INDIR(dsize)` 生成，核心检查逻辑为 `addr + offset + dsize > tree->size`——即验证从目标地址开始的 `dsize` 字节是否全部落在合法区域内。

### 7.2.5 局部变量追踪

边界检查器还需要追踪栈上分配的局部变量。每当进入一个函数时，`__bound_local_new` 被调用来注册局部变量区域；函数返回时，`__bound_local_delete` 被调用来注销：

```c
/* lib/bcheck.c */
void __bound_local_new(void *p1)
{
    size_t addr, fp, *p = p1;
    if (NO_CHECKING_GET()) return;

    GET_CALLER_FP(fp);
    /* p 指向一个描述局部变量区域的数组:
       [fp, addr1, size1, addr2, size2, ..., 0] */
    while ((addr = p[0])) {
        splay_insert(addr, p[1], tree);
        p += 2;
    }
}
```

### 7.2.6 标准库函数的包装

边界检查器还拦截了常见的内存和字符串操作函数，以确保它们不会越界访问：

```c
/* lib/bcheck.c - 拦截的函数 */
void *__bound_memcpy(void *dst, const void *src, size_t size);
void *__bound_memmove(void *dst, const void *src, size_t size);
void *__bound_memset(void *dst, int c, size_t size);
int   __bound_strlen(const char *s);
char *__bound_strcpy(char *dst, const char *src);
int   __bound_strcmp(const char *s1, const char *s2);
char *__bound_strdup(const char *s);
/* ... 以及更多 ... */
```

这些包装函数在执行实际操作前先验证所有参数的合法性。

### 7.2.7 多线程支持

bcheck.c 通过平台相关的互斥机制保护 Splay 树的并发访问：

```c
/* lib/bcheck.c - 各平台的锁实现 */
#if defined(__APPLE__)
    /* 使用 GCD dispatch_semaphore */
#elif defined(_WIN32)
    /* 使用 CRITICAL_SECTION */
#else
    /* 使用 pthread_spinlock（最快） */
    static pthread_spinlock_t bounds_spin;
    #define WAIT_SEM()  if (use_sem) pthread_spin_lock(&bounds_spin)
    #define POST_SEM()  if (use_sem) pthread_spin_unlock(&bounds_spin)
#endif
```

使用 spinlock 而非 mutex 是经过性能测试的决定——边界检查的临界区非常短（仅在 Splay 树中查找），spinlock 的开销更低。

### 7.2.8 使用方式

在命令行上使用 `-b` 选项启用边界检查：

```bash
tcc -b -o program program.c
```

在 libtcc API 中，通过设置编译状态的标志来启用：

```c
tcc_set_options(s, "-b");
```

运行时还可以通过 `__bounds_checking(int)` 函数动态启用/禁用检查（在信号处理器中很有用），以及通过 `__bound_never_fatal(int)` 控制越界访问是否导致程序终止。

---

## 7.3 原子操作 stdatomic.c/atomic.S

C11 标准引入了 `<stdatomic.h>` 头文件和原子操作支持。TinyCC 通过两个文件实现了完整的原子操作支持：

- `lib/stdatomic.c`：使用编译器内建函数实现的通用原子操作
- `lib/atomic.S`：各平台的底层汇编实现

### 7.3.1 stdatomic.c 的实现策略

由于 TCC 自身不提供 `__atomic_*` 编译器内建函数（不像 GCC/Clang 那样将原子操作内建到代码生成器中），`lib/stdatomic.c` 采用了一种巧妙的方式：使用**比较并交换（CAS）循环**来模拟所有原子操作。

核心宏 `ATOMIC_GEN_OP` 定义了通用的原子操作模板：

```c
/* lib/stdatomic.c */
#define ATOMIC_GEN_OP(TYPE, MODE, NAME, OP, RET) \
    TYPE __atomic_##NAME##_##MODE(volatile void *atom, TYPE value, \
                                  int memorder) \
    { \
        TYPE xchg, cmp; \
        __atomic_load((TYPE *)atom, (TYPE *)&cmp, __ATOMIC_RELAXED); \
        do { \
            xchg = (OP); \
        } while (!__atomic_compare_exchange( \
            (TYPE *)atom, &cmp, &xchg, true, \
            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)); \
        return RET; \
    }
```

这个宏接受四个参数：
- `TYPE`：操作的数据类型（如 `uint32_t`）
- `MODE`：大小标识（如 `4` 表示 4 字节）
- `NAME`：操作名称（如 `add_fetch`）
- `OP`：要执行的运算（如 `cmp + value`）
- `RET`：返回值（`xchg` 表示返回新值，`cmp` 表示返回旧值）

通过这个模板，所有 12 种原子操作都得以定义：

```c
/* lib/stdatomic.c */
ATOMIC_GEN(uint8_t, 1)   /* 1 字节原子操作 */
ATOMIC_GEN(uint16_t, 2)  /* 2 字节原子操作 */
ATOMIC_GEN(uint32_t, 4)  /* 4 字节原子操作 */
ATOMIC_GEN(uint64_t, 8)  /* 8 字节原子操作 */
```

其中 `ATOMIC_GEN` 宏进一步展开为 `ATOMIC_EXCHANGE`、`ATOMIC_ADD_FETCH`、`ATOMIC_FETCH_ADD` 等全部变体。

### 7.3.2 atomic.S 的平台实现

`lib/atomic.S` 提供了底层的原子原语实现，覆盖 i386、x86-64、ARM、ARM64 和 RISC-V 五个平台。这些原语是 `stdatomic.c` 中 CAS 循环的基石。

**x86-64 实现**：利用 `lock` 前缀和 `cmpxchg` 指令：

```asm
/* lib/atomic.S - x86-64 */
.globl __atomic_compare_exchange_8
__atomic_compare_exchange_8:
    mov    (%rsi),%rax          /* 加载当前期望值 */
    lock cmpxchg %rdx,(%rdi)   /* 原子比较并交换 */
    sete   %dl                  /* 设置成功标志 */
    je     .Lsuccess
    mov    %rax,(%rsi)          /* 失败：更新期望值为实际值 */
.Lsuccess:
    mov    %edx,%eax
    ret
```

`lock cmpxchg` 是 x86 架构上最核心的原子指令之一：它原子地比较内存中的值与 `rax` 寄存器的值，如果相等则将新值写入内存，否则将内存中的实际值加载到 `rax`。

**原子存储** 在 x86-64 上使用 `xchg` 指令（隐含 `lock` 前缀）：

```asm
/* lib/atomic.S - x86-64 */
.globl __atomic_store_8
__atomic_store_8:
    xchg   %rsi,(%rdi)   /* xchg 天然是原子的 */
    ret
```

**原子加载** 在 x86-64 上利用对齐的自然宽度读取天然原子性：

```asm
/* lib/atomic.S - x86-64 */
.globl __atomic_load_8
__atomic_load_8:
    mov    (%rdi),%rax   /* 对齐的 8 字节读取在 x86-64 上是原子的 */
    ret
```

**线程屏障** 利用 `lock orq` 实现完整的内存屏障：

```asm
/* lib/atomic.S - x86-64 */
.globl atomic_thread_fence
atomic_thread_fence:
    lock orq $0x0,(%rsp)  /* 全屏障 */
    ret

.globl atomic_signal_fence
atomic_signal_fence:
    ret                    /* 信号屏障仅需编译器屏障 */
```

**ARM 平台** 使用 `ldrex`/`strex`（ARMv6+）或 `ldrexd`/`strexd`（64 位原子操作）指令配对来实现 CAS 操作：

```asm
/* lib/atomic.S - ARM */
__atomic_compare_exchange_4:
    ldr    r12, [r0]         /* 加载当前值 */
.Lretry:
    ldrex  r3, [r0]          /* 独占加载 */
    cmp    r3, r12           /* 比较 */
    bne    .Lfail
    strex  r12, r2, [r0]     /* 尝试独占存储 */
    cmp    r12, #0
    bne    .Lretry           /* strex 失败则重试 */
    mov    r0, #1
    bx     lr
.Lfail:
    clrex
    str    r3, [r1]          /* 更新期望值 */
    mov    r0, #0
    bx     lr
```

`ldrex`/`strex` 是 ARM 的 Load-Linked/Store-Conditional 模式：`ldrex` 标记一个独占监视区域，`strex` 仅在该区域未被其他核心修改时才成功写入。如果 `strex` 失败（返回非零），必须从 `ldrex` 重新开始。

### 7.3.3 is_lock_free 查询

`stdatomic.c` 还实现了 `__atomic_is_lock_free` 函数：

```c
/* lib/stdatomic.c */
bool __atomic_is_lock_free(unsigned long size, const volatile void *ptr)
{
    switch (size) {
    case 1: case 2: case 4: return true;
#if defined __x86_64__ || defined __aarch64__ || defined __riscv
    case 8: return true;   /* 64 位平台原生支持 8 字节原子操作 */
#else
    case 8: return false;  /* 32 位平台需要 CAS 循环 */
#endif
    default: return false;
    }
}
```

---

## 7.4 alloca 实现

`alloca` 是一个在栈上动态分配内存的函数，分配的内存在函数返回时自动释放。与 `malloc` 不同，`alloca` 不需要（也不能）手动释放，且速度极快——它本质上只是移动栈指针。

TCC 的 `alloca` 实现在 `lib/alloca.S` 中，每个支持的架构都有独立的汇编实现。

### 7.4.1 x86-64 实现

```asm
/* lib/alloca.S - x86-64 */
.globl alloca
alloca:
    pop    %rdx           /* 保存返回地址 */
#ifdef _WIN32
    mov    %rcx,%rax      /* Windows: 参数在 rcx */
#else
    mov    %rdi,%rax      /* System V: 参数在 rdi */
#endif
    add    $15,%rax       /* 向上对齐到 16 字节 */
    and    $-16,%rax
    jz     .Ldone

#ifdef _WIN32
.Lprobe:
    cmp    $4096,%rax
    jb     .Lalloc
    test   %rax,-4096(%rsp) /* Windows: 触碰页面以触发栈扩展 */
    sub    $4096,%rsp
    sub    $4096,%rax
    jmp    .Lprobe
.Lalloc:
#endif
    sub    %rax,%rsp      /* 移动栈指针 */
    mov    %rsp,%rax      /* 返回分配的地址 */
.Ldone:
    push   %rdx           /* 恢复返回地址（通过 push+ret） */
    ret
```

几个关键点：

1. **对齐**：分配的大小向上取整到 16 字节边界（`add $15; and $-16`），这是 System V ABI 对栈对齐的要求。
2. **Windows 栈探测**：在 Windows 上，栈空间是按需分配的（guard page 机制）。如果一次移动栈指针超过一个页面（4096 字节），中间的 guard page 不会被触发，导致访问违规。因此 Windows 版本需要逐页探测。
3. **返回地址保存**：`alloca` 是一个特殊函数——它修改了栈指针本身。因此需要先 `pop` 保存返回地址，分配完成后再通过 `push` + `ret` 恢复控制流。

### 7.4.2 i386 实现

```asm
/* lib/alloca.S - i386 */
.globl alloca
alloca:
    pop    %edx           /* 保存返回地址 */
    pop    %eax           /* 获取参数 */
    add    $3,%eax
    and    $-4,%eax       /* 对齐到 4 字节 */
    jz     .Ldone
    sub    %eax,%esp
    mov    %esp,%eax
.Ldone:
    push   %edx           /* 恢复返回地址（双 push 确保栈平衡） */
    push   %edx
    ret
```

i386 版本比 x86-64 简单得多，因为 32 位栈对齐要求更宽松（4 字节而非 16 字节），且 Windows 版本也需要栈探测。

### 7.4.3 ARM64 实现

ARM64 版本在 TCC 自举时使用原始机器码（因为 TCC 的 ARM64 汇编器可能还未就绪），在非 TCC 编译器下使用标准汇编助记符：

```asm
/* lib/alloca.S - ARM64（非 TCC 版本） */
.globl alloca
alloca:
    add    x0, x0, #15     /* 向上对齐到 16 字节 */
    and    x0, x0, #-16
#ifdef _WIN32
    cbz    x0, .Ldone      /* 大小为 0 则跳过 */
    mov    x1, #4096
.Lprobe:
    cmp    x0, x1
    b.lo   .Lalloc
    sub    x2, sp, x1
    ldr    xzr, [x2]       /* 触碰 guard page */
    sub    sp, sp, x1
    sub    x0, x0, x1
    b      .Lprobe
.Lalloc:
    cbz    x0, .Ldone
    sub    sp, sp, x0
#else
    sub    sp, sp, x0      /* 直接减小栈指针 */
#endif
.Ldone:
    mov    x0, sp           /* 返回分配的地址 */
    ret
```

### 7.4.4 RISC-V 实现

```asm
/* lib/alloca.S - RISC-V */
.globl alloca
alloca:
    sub    sp, sp, a0       /* 在栈上分配空间 */
    addi   sp, sp, -15
    andi   sp, sp, -16      /* 对齐到 16 字节 */
    add    a0, sp, zero     /* 返回分配的地址 */
    ret
```

RISC-V 版本是最简洁的——RISC-V 架构没有隐式的栈探测需求，对齐操作直接使用 `andi` 指令完成。

---

## 7.5 libtcc API 完整参考

libtcc 是 TinyCC 提供的嵌入式 API，允许在应用程序中嵌入 C 编译器。这使得 TCC 可以作为 JIT（即时编译）后端、脚本引擎的执行后端、或动态代码生成工具使用。

### 7.5.1 tcc_new / tcc_delete 生命周期

```c
/* 创建一个新的 TCC 编译上下文 */
TCCState *tcc_new(void);

/* 释放 TCC 编译上下文 */
void tcc_delete(TCCState *s);
```

`tcc_new()` 分配并初始化一个 `TCCState` 结构体。该结构体包含了编译器的全部状态：符号表、段列表、预处理器状态、错误处理配置等。调用 `tcc_new()` 后的默认配置如下：

```c
/* libtcc.c - tcc_new() 中的默认设置 */
s->gnu_ext = 1;                    /* 启用 GNU 扩展 */
s->tcc_ext = 1;                    /* 启用 TCC 扩展 */
s->nocommon = 1;                   /* 不使用 common 符号 */
s->dollars_in_identifiers = 1;     /* 允许 $ 在标识符中 */
s->cversion = 199901;              /* 默认 C99 */
s->warn_implicit_function_declaration = 1;
s->warn_discarded_qualifiers = 1;
s->ms_extensions = 1;
s->unwind_tables = 1;
s->ppfp = stdout;                  /* 预处理输出到 stdout */
```

`tcc_delete()` 释放所有关联的内存，包括段数据、库路径、包含路径、符号表等。

**重要**：一个 `TCCState` 实例不应被并发使用。如果需要并行编译，应为每个线程创建独立的 `TCCState`。

### 7.5.2 tcc_set_output_type

```c
int tcc_set_output_type(TCCState *s, int output_type);
```

设置输出类型。**必须在任何编译操作之前调用**，因为它会初始化必要的段和路径。可用的输出类型：

| 常量 | 值 | 说明 |
|------|---|------|
| `TCC_OUTPUT_MEMORY` | 1 | 编译结果保留在内存中，通过 `tcc_relocate()` 加载后直接调用 |
| `TCC_OUTPUT_OBJ` | 3 | 输出 ELF/COFF 目标文件（.o） |
| `TCC_OUTPUT_EXE` | 2 | 输出可执行文件 |
| `TCC_OUTPUT_DLL` | 4 | 输出动态链接库（.so/.dll） |
| `TCC_OUTPUT_PREPROCESS` | 5 | 仅执行预处理（类似 `gcc -E`） |

当设置为 `TCC_OUTPUT_MEMORY` 时，TCC 不会搜索 CRT 对象文件和系统库路径，因为不需要链接。当设置为 `TCC_OUTPUT_EXE` 时，如果定义了 `CONFIG_TCC_PIE`，则输出类型会被调整为 `TCC_OUTPUT_EXE | TCC_OUTPUT_DYN`（位置无关可执行文件）。

### 7.5.3 tcc_compile_string / tcc_add_file

```c
/* 编译一个 C 源码字符串 */
int tcc_compile_string(TCCState *s, const char *buf);

/* 添加文件（C 源码、目标文件、库等） */
int tcc_add_file(TCCState *s, const char *filename);
```

`tcc_compile_string()` 将字符串作为 C 源代码编译。内部调用链为：

```
tcc_compile_string()
  → tcc_compile(s, filetype, str, fd=-1)
    → tcc_open_bf(s, "<string>", len)   // 创建虚拟文件
    → preprocess_start()                 // 初始化预处理器
    → tccgen_init()                      // 初始化代码生成器
    → tccgen_compile()                   // 编译
    → tccgen_finish() / preprocess_end()
```

`tcc_add_file()` 可以接受多种文件类型，通过文件扩展名自动识别：

```c
/* libtcc.c - guess_filetype() */
if (!strcmp(ext, "S"))
    filetype = AFF_TYPE_ASMPP;    /* 需要预处理的汇编 */
else if (!strcmp(ext, "s"))
    filetype = AFF_TYPE_ASM;      /* 不需要预处理的汇编 */
else if (!PATHCMP(ext, "c") || !PATHCMP(ext, "h") || !PATHCMP(ext, "i"))
    filetype = AFF_TYPE_C;        /* C 源码 */
else
    filetype |= AFF_TYPE_BIN;     /* 二进制文件(.o, .a, .so) */
```

对于二进制文件，`tcc_add_file()` 会根据 ELF 文件头判断是目标文件、归档还是共享库，并分别调用 `tcc_load_object_file()`、`tcc_load_archive()` 或 `tcc_load_dll()`。

一个实用的调试技巧：可以在字符串前添加 `#line` 指令来改善错误消息：

```c
tcc_compile_string(s,
    "#line 1 \"my_script.c\"\n"
    "int main() { return 42; }\n");
```

### 7.5.4 tcc_relocate / tcc_get_symbol / tcc_run

```c
/* 执行所有重定位（在使用 tcc_get_symbol 之前必须调用） */
int tcc_relocate(TCCState *s1);

/* 获取符号的地址（在 tcc_relocate 之后调用） */
void *tcc_get_symbol(TCCState *s, const char *name);

/* 链接并运行 main() 函数 */
int tcc_run(TCCState *s, int argc, char **argv);
```

`tcc_relocate()` 是内存输出模式的关键步骤。它完成以下工作：

1. 将编译产生的各段（`.text`、`.data`、`.rodata`、`.bss`）拷贝到可执行内存区域
2. 应用所有重定位——修正函数调用地址、全局变量地址等
3. 使代码段可执行（通过 `mprotect` 或 `VirtualProtect`）

`tcc_get_symbol()` 在重定位完成后查找全局符号。它遍历 ELF 符号表找到指定名称的符号，并返回其在已重定位内存中的地址。

`tcc_run()` 是一个便利函数，相当于 `tcc_relocate()` + 查找 `main` 符号 + 调用 `main(argc, argv)` + 清理。它等价于：

```c
/* tcc_run(s, argc, argv) 的等价展开 */
tcc_relocate(s);
int (*main_func)(int, char**) = tcc_get_symbol(s, "main");
int ret = main_func(argc, argv);
return ret;
```

### 7.5.5 tcc_add_symbol

```c
int tcc_add_symbol(TCCState *s, const char *name, const void *val);
```

这个函数允许将宿主程序中的符号注册到编译器上下文中。当编译的代码引用了这些符号时，链接器会将它们解析到宿主程序提供的地址。

典型用法：

```c
/* 在宿主程序中定义的函数 */
int my_add(int a, int b) { return a + b; }
const char greeting[] = "Hello from host!";

/* 注册到编译器上下文 */
tcc_add_symbol(s, "my_add", my_add);
tcc_add_symbol(s, "greeting", greeting);

/* 编译的代码可以使用这些符号 */
tcc_compile_string(s,
    "extern int my_add(int, int);\n"
    "extern const char greeting[];\n"
    "int run() { return my_add(1, 2); }\n");
```

`tcc_add_symbol()` 的实现将符号添加到 ELF 动态符号表中，在重定位时这些符号会被标记为已解析。

### 7.5.6 tcc_set_error_func

```c
typedef void TCCErrorFunc(void *opaque, const char *msg);
void tcc_set_error_func(TCCState *s, void *error_opaque,
                        TCCErrorFunc *error_func);
```

设置自定义的错误/警告回调函数。默认情况下，TCC 将错误消息输出到 `stderr`。通过设置回调，应用程序可以：

- 将错误重定向到日志文件
- 在 GUI 应用中显示错误对话框
- 收集错误信息进行程序化处理

错误消息的格式为：`文件名:行号: error/warning: 消息内容`

```c
/* 错误回调示例 */
void my_error_handler(void *opaque, const char *msg)
{
    /* opaque 可以是任意用户数据 */
    FILE *log = (FILE *)opaque;
    fprintf(log, "[TCC] %s\n", msg);
}

/* 设置回调 */
tcc_set_error_func(s, log_file, my_error_handler);
```

错误回调在编译和链接过程中都会被调用。编译错误会导致 `tcc_compile_string()` 或 `tcc_add_file()` 返回 -1。

---

## 7.6 使用示例

### 7.6.1 示例 1: Hello World

最基本的 libtcc 用法：编译并执行一个简单的 C 程序。

```c
/* examples/libtcc_hello.c */
#include "libtcc.h"
#include <stdio.h>

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    tcc_compile_string(s,
        "#include <tcclib.h>\n"
        "int main() {\n"
        "    printf(\"Hello from TCC!\\n\");\n"
        "    return 0;\n"
        "}\n");

    tcc_run(s, 0, NULL);
    tcc_delete(s);
    return 0;
}
```

### 7.6.2 示例 2: 调用编译后的函数

```c
/* examples/libtcc_functions.c */
#include "libtcc.h"
#include <stdio.h>

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    tcc_compile_string(s,
        "int factorial(int n) {\n"
        "    if (n <= 1) return 1;\n"
        "    return n * factorial(n - 1);\n"
        "}\n");

    tcc_relocate(s);

    typedef int (*factorial_fn)(int);
    factorial_fn fact = (factorial_fn)tcc_get_symbol(s, "factorial");

    for (int i = 0; i <= 10; i++)
        printf("factorial(%d) = %d\n", i, fact(i));

    tcc_delete(s);
    return 0;
}
```

### 7.6.3 示例 3: 错误处理

```c
#include "libtcc.h"
#include <stdio.h>

static int error_count = 0;

void error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    error_count++;
}

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_error_func(s, NULL, error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* 故意包含语法错误 */
    int ret = tcc_compile_string(s,
        "int main() {\n"
        "    int x = ;\n"  /* 语法错误 */
        "    return x;\n"
        "}\n");

    if (ret == -1) {
        printf("Compilation failed with %d error(s)\n", error_count);
    }

    tcc_delete(s);
    return 0;
}
```

### 7.6.4 示例 4: 宿主函数注册

```c
/* examples/libtcc_host.c */
#include "libtcc.h"
#include <stdio.h>
#include <math.h>

/* 宿主程序中定义的函数 */
double host_sin(double x) { return sin(x); }
double host_cos(double x) { return cos(x); }
void host_print(const char *msg) { printf("[Host] %s\n", msg); }

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* 注册宿主函数 */
    tcc_add_symbol(s, "host_sin", host_sin);
    tcc_add_symbol(s, "host_cos", host_cos);
    tcc_add_symbol(s, "host_print", host_print);

    tcc_compile_string(s,
        "extern double host_sin(double);\n"
        "extern double host_cos(double);\n"
        "extern void host_print(const char *);\n"
        "\n"
        "void compute(void) {\n"
        "    double pi = 3.14159265358979;\n"
        "    double s = host_sin(pi / 4);\n"
        "    double c = host_cos(pi / 4);\n"
        "    host_print(\"sin(pi/4) = cos(pi/4)\");\n"
        "}\n");

    tcc_relocate(s);

    void (*compute)(void) = tcc_get_symbol(s, "compute");
    compute();

    tcc_delete(s);
    return 0;
}
```

### 7.6.5 示例 5: 输出到文件

```c
#include "libtcc.h"
#include <stdio.h>

int main(void)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_EXE);

    tcc_compile_string(s,
        "#include <tcclib.h>\n"
        "int main() {\n"
        "    printf(\"Compiled and linked by TCC\\n\");\n"
        "    return 0;\n"
        "}\n");

    /* 输出可执行文件（不需要 tcc_relocate） */
    tcc_output_file(s, "output_program");

    tcc_delete(s);
    return 0;
}
```

输出目标文件或 DLL 的方式类似：

```c
/* 输出目标文件 */
tcc_set_output_type(s, TCC_OUTPUT_OBJ);
tcc_output_file(s, "output.o");

/* 输出动态库 */
tcc_set_output_type(s, TCC_OUTPUT_DLL);
tcc_output_file(s, "liboutput.so");
```

---

## 7.7 内部实现: TCCState 结构详解

`TCCState` 是 TCC 最核心的数据结构，它包含了编译器的全部状态。以下是其关键字段的分类说明：

### 7.7.1 编译选项

```c
struct TCCState {
    /* 通用选项 */
    unsigned char verbose;        /* 详细输出级别 (0/1/2/3) */
    unsigned char nostdinc;       /* 不添加标准包含路径 */
    unsigned char nostdlib;       /* 不链接标准库 */
    unsigned char nostdlib_paths; /* 不搜索默认库路径 */
    unsigned char nocommon;       /* 不使用 common 符号 */
    unsigned char static_link;    /* 静态链接 */
    unsigned char rdynamic;       /* 导出所有符号 */
    unsigned char filetype;       /* 文件类型: NONE/C/ASM */
    unsigned char optimize;       /* 是否定义 __OPTIMIZE__ */
    unsigned int  cversion;       /* C 标准版本: 199901/201112 */

    /* 语言选项 */
    unsigned char char_is_unsigned;
    unsigned char leading_underscore;
    unsigned char ms_extensions;
    unsigned char ms_bitfields;
    unsigned char gnu89_inline;
    unsigned char unwind_tables;

    /* 调试选项 */
    unsigned char do_debug;       /* 生成调试信息 (-g) */
    unsigned char dwarf;          /* 使用 DWARF 格式（而非 STAB） */
    unsigned char do_backtrace;   /* 启用运行时回溯 (-bt) */
    unsigned char do_bounds_check;/* 启用边界检查 (-b) */
    /* ... */
};
```

### 7.7.2 路径配置

```c
    char *tcc_lib_path;    /* CONFIG_TCCDIR 或 -B 选项值 */
    char *soname;          /* -soname 指定的 SO 名称 */
    char *rpath;           /* -Wl,-rpath= 指定的运行时路径 */
    char *elfint;          /* ELF 解释器路径 */

    /* 包含路径列表 */
    char **include_paths;
    int nb_include_paths;
    char **sysinclude_paths;
    int nb_sysinclude_paths;

    /* 库路径列表 */
    char **library_paths;
    int nb_library_paths;
    char **crt_paths;
    int nb_crt_paths;
```

### 7.7.3 段管理

```c
    /* 预定义段 */
    Section *text_section;     /* 代码段 .text */
    Section *data_section;     /* 已初始化数据段 .data */
    Section *rodata_section;   /* 只读数据段 .rodata */
    Section *bss_section;      /* 未初始化数据段 .bss */
    Section *tdata_section;    /* 线程局部数据段 .tdata */
    Section *tbss_section;     /* 线程局部 BSS 段 .tbss */
    Section *common_section;   /* Common 符号段 */

    Section *cur_text_section; /* 当前正在生成代码的段 */

    /* 符号与动态链接段 */
    Section *symtab_section;   /* 符号表 */
    Section *dynsymtab_section;/* 动态符号表（临时） */
    Section *dynsym;           /* 导出的动态符号表 */
    Section *got;              /* 全局偏移表 */
    Section *plt;              /* 过程链接表 */

    /* 调试段 */
    Section *stab_section;         /* STAB 调试段 */
    Section *dwarf_info_section;   /* DWARF .debug_info */
    Section *dwarf_abbrev_section; /* DWARF .debug_abbrev */
    Section *dwarf_line_section;   /* DWARF .debug_line */
    Section *dwarf_str_section;    /* DWARF .debug_str */

    /* 边界检查段 */
    Section *bounds_section;   /* 全局边界描述 */
    Section *lbounds_section;  /* 局部边界描述 */

    /* 段数组（动态增长） */
    Section **sections;
    int nb_sections;
```

### 7.7.4 预处理器状态

```c
    /* #include 栈 */
    BufferedFile *include_stack[INCLUDE_STACK_SIZE];
    BufferedFile **include_stack_ptr;

    /* #ifdef 栈 */
    int ifdef_stack[IFDEF_STACK_SIZE];
    int *ifdef_stack_ptr;

    /* 已包含文件缓存（加速重复包含检测） */
    int cached_includes_hash[CACHED_INCLUDES_HASH_SIZE];
    CachedInclude **cached_includes;
    int nb_cached_includes;

    /* #pragma pack 栈 */
    int pack_stack[PACK_STACK_SIZE];
    int *pack_stack_ptr;

    /* -D/-U 命令行宏定义 */
    CString cmdline_defs;
    /* -include 命令行包含文件 */
    CString cmdline_incl;
```

### 7.7.5 错误处理与运行时

```c
    /* 错误回调 */
    void *error_opaque;
    void (*error_func)(void *opaque, const char *msg);
    int error_set_jmp_enabled;
    jmp_buf error_jmp_buf;    /* 编译错误时跳转 */
    int nb_errors;            /* 已发生的错误数 */

    /* 运行时（仅 TCC_IS_NATIVE） */
    const char *run_main;     /* tcc_run() 的入口符号 */
    void *run_ptr;            /* 运行时内存分配 */
    unsigned run_size;        /* 运行时内存大小 */
    struct TCCState *next;    /* 运行时状态链表 */
    struct rt_context *rc;    /* 回溯信息块 */
```

---

## 7.8 线程安全与多实例使用

### 7.8.1 并发编译的支持

TCC 通过编译时信号量 `tcc_compile_sem` 支持多线程使用。核心保护机制在 `tcc_enter_state()` 和 `tcc_exit_state()` 中：

```c
/* libtcc.c */
ST_DATA struct TCCState *tcc_state;  /* 全局当前状态 */

PUB_FUNC void tcc_enter_state(TCCState *s1)
{
    if (s1->error_set_jmp_enabled)
        return;
    WAIT_SEM(&tcc_compile_sem);
    tcc_state = s1;
}

PUB_FUNC void tcc_exit_state(TCCState *s1)
{
    if (s1->error_set_jmp_enabled)
        return;
    tcc_state = NULL;
    POST_SEM(&tcc_compile_sem);
}
```

TCC 的解析器和代码生成器大量使用全局变量（在 `tccpp.c` 和 `tccgen.c` 中定义），因此同一时刻只能有一个 `TCCState` 处于编译状态。信号量确保了这一点。

如果定义了 `CONFIG_TCC_SEMLOCK`（默认启用），则 `tcc_compile_sem` 使用 pthread 互斥量（或 Windows 临界区）。如果不希望使用锁（例如确定单线程使用），可以在编译 libtcc 时定义 `CONFIG_TCC_SEMLOCK=0`。

### 7.8.2 多实例模式

典型的多线程使用模式是：每个线程拥有自己的 `TCCState` 实例，线程间不共享编译状态：

```c
/* 每个线程独立编译 */
void *thread_func(void *arg)
{
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    tcc_compile_string(s, (const char *)arg);
    tcc_relocate(s);
    /* ... 使用编译后的代码 ... */
    tcc_delete(s);
    return NULL;
}
```

TCC 的测试套件中 `tests/libtcc_test_mt.c` 提供了一个完整的多线程测试示例：它创建 20 个线程，每个线程独立编译并执行一个 Fibonacci 函数。

### 7.8.3 注意事项

1. **不要跨线程共享 TCCState**：虽然有信号量保护，但一个 TCCState 的内部状态（符号表、段数据等）不是为并发访问设计的。
2. **tcc_relocate 后的代码是线程安全的**：一旦代码被重定位并加载到内存，编译后的函数可以被任何线程安全调用（假设函数本身是线程安全的）。
3. **tcc_add_symbol 的时序**：符号注册必须在 `tcc_relocate()` 之前完成。
4. **错误回调可能在信号量内被调用**：如果错误回调中有耗时操作，可能影响其他线程的编译性能。

---

## 7.9 本章小结与练习

### 小结

本章介绍了 TinyCC 运行时生态系统的三大支柱：

1. **libtcc1.a**：为缺乏硬件支持的操作提供软件实现，包括 64 位除法、浮点转换和移位操作。这些函数仅在需要时由编译器自动链接。

2. **边界检查器**（bcheck.c）：通过 Splay 树追踪所有已分配的内存区域，在每次指针操作前验证合法性。它拦截了标准库的内存操作函数以提供完整的覆盖。

3. **libtcc 嵌入式 API**：提供了一套完整的生命周期管理接口（`tcc_new` → 配置 → 编译 → 重定位 → 使用 → `tcc_delete`），支持内存输出、文件输出、宿主符号注册和自定义错误处理。

此外，我们还详细分析了 TCCState 结构体的内部组织和线程安全模型。

### 练习 1：使用 libtcc 构建计算器

使用 libtcc API 构建一个简单的交互式计算器。程序从标准读取 C 表达式（如 `2 + 3 * 4`），用 libtcc 编译为函数，执行后输出结果。

**要求**：
- 使用 `TCC_OUTPUT_MEMORY` 模式
- 实现错误处理回调
- 支持数学函数（通过 `tcc_add_symbol` 注册 `sin`、`cos` 等）

### 练习 2：嵌入式编译器

编写一个嵌入式脚本系统：从配置文件中读取 C 代码片段，用 libtcc 编译并注册为回调函数，然后在主程序的事件循环中调用这些回调。

**要求**：
- 支持热重载（重新读取文件 → 重新编译 → 替换回调指针）
- 使用互斥锁保护回调指针的替换操作
- 测试多个脚本的并发执行
