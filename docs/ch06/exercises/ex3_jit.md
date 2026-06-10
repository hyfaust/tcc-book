# 练习 3：使用 tcc -run 观察 JIT 行为

## 目标

使用 TinyCC 的 `-run` 选项执行 C 程序，通过 `/proc/[pid]/maps` 和其他工具观察 JIT 运行时的内存布局和行为。

## 前置知识

- `tcc_relocate_ex()` 的内存分配策略
- `mmap` 和 `mprotect` 系统调用
- `/proc/[pid]/maps` 文件格式

## 任务 1：基本 JIT 执行

### 1.1 编写测试程序

创建 `jit_test.c`：

```c
#include <stdio.h>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    for (int i = 0; i <= 10; i++)
        printf("factorial(%d) = %d\n", i, factorial(i));
    return 0;
}
```

### 1.2 正常编译执行 vs JIT 执行

```bash
# 方式 1：正常编译为可执行文件再运行
tcc -o jit_test jit_test.c
./jit_test

# 方式 2：JIT 执行
tcc -run jit_test.c
```

**Q1**: 两种方式的输出是否相同？执行速度是否有可感知的差异？

### 1.3 使用 time 对比

```bash
# 编译 + 执行
time (tcc -o jit_test jit_test.c && ./jit_test)

# JIT 执行
time tcc -run jit_test.c
```

**Q2**: JIT 模式的总时间与"编译+执行"相比如何？JIT 模式省去了哪些步骤？

## 任务 2：观察 JIT 内存布局

### 2.1 使用 /proc/self/maps

创建 `jit_maps.c`：

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    /* 先打印地址空间布局 */
    printf("=== Before reading maps ===\n");
    system("cat /proc/self/maps | head -30");
    
    /* 打印一个函数的地址 */
    printf("\nmain is at: %p\n", (void*)main);
    
    return 0;
}
```

```bash
# 正常执行
tcc -o jit_maps jit_maps.c
./jit_maps

# JIT 执行
tcc -run jit_maps.c
```

**Q3**: 在 JIT 模式下，`main` 函数的地址在什么范围内？这个地址对应的内存映射条目的权限是什么？

**Q4**: 在正常执行模式下，`main` 的地址在哪里？与 JIT 模式有何不同？

### 2.2 深入分析 JIT 内存

创建 `jit_memory.c`：

```c
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* 打印当前进程的内存映射 */
static void print_maps(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[256];
    printf("=== Memory Maps ===\n");
    while (fgets(line, sizeof(line), f)) {
        /* 只显示包含 tcc 或 rwx 的条目 */
        if (strstr(line, "rwx") || strstr(line, "[stack]"))
            printf("%s", line);
    }
    fclose(f);
}

int test_func(int x) {
    return x * x + 1;
}

int main(void) {
    printf("test_func at: %p\n", (void*)test_func);
    printf("main at:      %p\n", (void*)main);
    
    print_maps();
    
    /* 验证代码可执行 */
    int result = test_func(5);
    printf("test_func(5) = %d\n", result);
    
    return 0;
}
```

```bash
# JIT 执行
tcc -run jit_memory.c
```

**Q5**: 是否存在权限为 `rwx`（读+写+执行）的内存映射？这对应 `tcc_relocate_ex()` 中的哪种内存分配策略？

**Q6**: 如果系统启用了 SELinux，`tcc_relocate_ex()` 使用什么策略来分配内存？（提示：参考 `CONFIG_SELINUX`）

### 2.3 使用 -bench 选项

```bash
tcc -bench -run jit_test.c
```

**Q7**: `-bench` 选项输出了什么信息？这些信息分别对应代码中的哪些节？

## 任务 3：符号解析观察

### 3.1 JIT 模式下的外部符号

创建 `jit_extern.c`：

```c
#include <stdio.h>
#include <math.h>

int main(void) {
    double pi = acos(-1.0);
    printf("pi = %.10f\n", pi);
    printf("sin(pi/2) = %.10f\n", sin(pi / 2));
    return 0;
}
```

```bash
# JIT 执行（需要链接 libm）
tcc -lm -run jit_extern.c
```

**Q8**: 在 JIT 模式下，`acos` 和 `sin` 这些外部函数是如何被解析的？
（提示：参考 `relocate_syms()` 中 `do_resolve=1` 的分支）

**Q9**: 如果去掉 `-lm` 选项，会发生什么？错误信息是什么？

### 3.2 使用 tcc_get_symbol

创建 `jit_symbol.c`，使用 libtcc API：

```c
/* 此程序需要使用 libtcc API 编译 */
/* 编译: tcc -o jit_symbol jit_symbol.c -ltcc -ldl */
#include <stdio.h>
#include "libtcc.h"

const char *code = 
    "int add(int a, int b) { return a + b; }\n"
    "int mul(int a, int b) { return a * b; }\n";

int main(void) {
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    
    if (tcc_compile_string(s, code) == -1) {
        fprintf(stderr, "Compilation failed\n");
        return 1;
    }
    
    if (tcc_relocate(s) == -1) {
        fprintf(stderr, "Relocation failed\n");
        return 1;
    }
    
    /* 获取编译后的函数地址 */
    int (*add)(int, int) = tcc_get_symbol(s, "add");
    int (*mul)(int, int) = tcc_get_symbol(s, "mul");
    
    if (add && mul) {
        printf("add(3, 4) = %d\n", add(3, 4));
        printf("mul(3, 4) = %d\n", mul(3, 4));
    }
    
    tcc_delete(s);
    return 0;
}
```

**Q10**: `tcc_get_symbol()` 返回的函数指针指向的地址在哪个内存区域？

**Q11**: 调用 `tcc_delete()` 后再调用这些函数指针会发生什么？为什么？

## 任务 4：JIT 与普通链接的对比

### 4.1 对比 GOT/PLT 的使用

创建 `jit_vs_normal.c`：

```c
#include <stdio.h>

int global_val = 42;

int get_val(void) {
    return global_val;
}

int main(void) {
    printf("val = %d\n", get_val());
    return 0;
}
```

```bash
# 普通编译
tcc -o jvn_normal jit_vs_normal.c
objdump -d jvn_normal | grep -A 20 '<main>:'
objdump -d jvn_normal | grep -A 5 '<get_val>:'

# 注意：JIT 模式下无法直接 objdump，但可以通过反汇编
# 或使用 GDB 观察
```

**Q12**: 在普通编译的可执行文件中，`get_val()` 访问 `global_val` 使用了什么指令序列？

**Q13**: 在 JIT 模式下，同样的访问方式是否相同？（提示：JIT 模式下 `build_got_entries()` 仍然会为 GOTPCREL 创建 GOT 条目）

### 4.2 GDB 观察 JIT 代码

```bash
# 编译带调试信息的 JIT 程序
# 使用 gdb 附加到 tcc -run 进程
# 或使用以下技巧：

# 创建一个在 main 中暂停的程序
cat > jit_gdb.c << 'EOF'
#include <stdio.h>

int main(void) {
    volatile int x = 42;
    printf("x = %d, &x = %p\n", x, &x);
    return 0;
}
EOF

# 在 GDB 中运行
# gdb --args tcc -run jit_gdb.c
# (gdb) break main
# (gdb) run
# (gdb) info registers
# (gdb) x/10i $rip
# (gdb) info proc mappings
```

**Q14**: 在 GDB 中，`main` 函数的地址在什么范围？这个范围对应 `/proc/maps` 中的哪个条目？

## 任务 5：性能和限制

### 5.1 JIT 编译大文件

创建一个包含多个函数的较大 C 文件，测试 JIT 编译时间：

```bash
# 生成一个较大的 C 文件
cat > jit_large.c << 'EOF'
#include <stdio.h>

int fib(int n) {
    if (n <= 1) return n;
    return fib(n-1) + fib(n-2);
}

int collatz(int n) {
    int steps = 0;
    while (n != 1) {
        n = (n % 2) ? 3*n+1 : n/2;
        steps++;
    }
    return steps;
}

int main(void) {
    printf("fib(30) = %d\n", fib(30));
    int max_steps = 0, max_n = 0;
    for (int i = 1; i <= 1000; i++) {
        int s = collatz(i);
        if (s > max_steps) { max_steps = s; max_n = i; }
    }
    printf("longest collatz under 1000: %d (%d steps)\n", max_n, max_steps);
    return 0;
}
EOF

# JIT 执行
time tcc -run jit_large.c
```

**Q15**: JIT 模式下，递归函数（如 `fib`）的性能与普通编译相比如何？

### 5.2 JIT 的限制

```bash
# 尝试在 JIT 模式下使用内联汇编
cat > jit_asm.c << 'EOF'
#include <stdio.h>

int main(void) {
    int result;
    __asm__ ("movl $42, %0" : "=r"(result));
    printf("result = %d\n", result);
    return 0;
}
EOF

tcc -run jit_asm.c
```

**Q16**: JIT 模式是否支持内联汇编？如果有问题，是什么问题？

## 思考题

1. `tcc_relocate_ex()` 中的三遍策略（计算大小、复制数据、设置权限）为什么要分三次？能否合并？
2. JIT 模式下，`dlsym(RTLD_DEFAULT, name)` 用于解析未定义符号。这意味着 JIT 程序可以访问哪些符号？
3. 为什么 JIT 模式需要将代码和数据分离到不同的内存页？如果全部放在可写可执行的页中会有什么安全问题？
4. TinyCC 的 JIT 模式与 LuaJIT 的 JIT 编译器有什么本质区别？
5. 如果要在生产环境中使用 `libtcc` 实现一个嵌入式脚本引擎，需要考虑哪些安全和性能问题？

## 提交要求

1. 完成所有 Q1-Q16 的回答
2. 附上关键命令的输出
3. 对于思考题，写一段 200-300 字的分析
