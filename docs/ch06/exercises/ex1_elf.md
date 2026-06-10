# 练习 1：使用 readelf 分析 tcc 输出

## 目标

通过 `readelf`、`objdump` 和本章提供的 `read_elf.c` 工具，分析 TinyCC 编译输出的 ELF 文件，加深对 ELF 格式和链接过程的理解。

## 前置准备

```bash
# 确保 tcc 已编译
cd /home/faust/tinycc
make

# 编译本章的 read_elf 工具
./tcc -o book/ch06-linker/examples/read_elf book/ch06-linker/examples/read_elf.c
```

## 任务 1：分析目标文件（.o）

### 1.1 编写并编译测试程序

创建 `test1.c`：

```c
int shared_var = 100;

static int helper(int x) {
    return x * 2;
}

int compute(int a, int b) {
    return helper(a) + b + shared_var;
}
```

编译：

```bash
tcc -c -o test1.o test1.c
```

### 1.2 使用 readelf 分析

```bash
# 查看 ELF 头
readelf -h test1.o

# 回答以下问题：
# Q1: e_type 是什么？为什么？
# Q2: e_entry 是多少？为什么？
# Q3: e_machine 的值是多少？对应什么架构？
```

```bash
# 查看节头表
readelf -S test1.o

# 回答以下问题：
# Q4: 有多少个节？列出所有节名。
# Q5: .text 节的 sh_flags 是什么？这些标志的含义是什么？
# Q6: .bss 节的 sh_type 是什么？它在文件中占多少空间？
# Q7: 哪些节具有 SHF_ALLOC 标志？哪些没有？
```

```bash
# 查看符号表
readelf -s test1.o

# 回答以下问题：
# Q8: 哪些符号是 STB_LOCAL？哪些是 STB_GLOBAL？
# Q9: shared_var 的 st_shndx 是什么？为什么指向 .data？
# Q10: helper 函数的符号绑定是什么？为什么不是 GLOBAL？
```

```bash
# 查看重定位表
readelf -r test1.o

# 回答以下问题：
# Q11: 有哪些重定位条目？分别是什么类型？
# Q12: 如果没有外部函数调用，为什么可能没有重定位条目？
```

### 1.3 使用 read_elf 工具分析

```bash
book/ch06-linker/examples/read_elf test1.o
```

对比 `readelf` 和 `read_elf` 的输出，确认两者是否一致。

## 任务 2：分析可执行文件

### 2.1 编译为可执行文件

创建 `test2.c`：

```c
#include <stdio.h>

int main(void) {
    printf("hello\n");
    return 0;
}
```

```bash
tcc -o test2 test2.c
```

### 2.2 对比 .o 和可执行文件的差异

```bash
# 查看可执行文件的 ELF 头
readelf -h test2

# 回答以下问题：
# Q13: e_type 变成了什么？
# Q14: e_entry 现在是多少？这个地址对应哪个函数？
# Q15: 有多少个程序头（e_phnum）？
```

```bash
# 查看程序头
readelf -l test2

# 回答以下问题：
# Q16: 有几个 PT_LOAD 段？它们的权限分别是什么？
# Q17: PT_INTERP 段的内容是什么？它指定了什么？
# Q18: PT_GNU_RELRO 段保护了哪些节？
```

```bash
# 查看动态节
readelf -d test2

# 回答以下问题：
# Q19: DT_NEEDED 条目列出了哪些共享库？
# Q20: DT_FLAGS 的值是什么？DF_BIND_NOW 意味着什么？
```

```bash
# 查看节头
readelf -S test2

# 回答以下问题：
# Q21: 与 test1.o 相比，新增了哪些节？
# Q22: .got 和 .plt 节分别是什么类型？什么标志？
# Q23: .dynsym 和 .symtab 有什么区别？
```

```bash
# 查看动态符号表
readelf --dyn-syms test2

# 回答以下问题：
# Q24: 动态符号表中有哪些符号？
# Q25: printf 的 st_shndx 是什么？
```

## 任务 3：使用 objdump 反汇编

```bash
# 反汇编 .text 节
objdump -d test1.o

# 回答以下问题：
# Q26: compute 函数中，shared_var 的访问使用了什么寻址模式？
# Q27: helper 函数是直接调用还是通过 PLT？
```

```bash
# 反汇编可执行文件的 main 函数
objdump -d test2 | grep -A 30 '<main>:'

# 回答以下问题：
# Q28: printf 的调用是直接 call 还是 call 到 PLT？
# Q29: PLT 条目的指令序列是什么？
```

## 任务 4：使用 objdump 查看重定位

```bash
# 查看 .o 文件的重定位信息
objdump -r test1.o

# 回答以下问题：
# Q30: 每个重定位条目引用了哪个符号？
# Q31: 如果有 R_X86_64_PLT32 类型，说明该符号是什么性质的？
```

## 任务 5：对比不同优化级别

```bash
# 使用 -O0 和 -O2 分别编译
tcc -c -o test1_O0.o test1.c
tcc -c -o test1_O2.o -O2 test1.c

# 对比两者的 .text 节大小
readelf -S test1_O0.o | grep .text
readelf -S test1_O2.o | grep .text

# 反汇编对比
objdump -d test1_O0.o
objdump -d test1_O2.o
```

## 思考题

1. 为什么可重定位文件（.o）的 e_entry 是 0？
2. 为什么 .bss 节的 sh_type 是 SHT_NOBITS 而不是 SHT_PROGBITS？
3. `static` 函数在符号表中的绑定类型是什么？这与 `static` 变量有何不同？
4. 为什么 TinyCC 的 .data.ro 节（只读数据）不叫 .rodata？
5. 在可执行文件中，.text 和 .plt 都是可执行的，它们有什么区别？

## 提交要求

将以上所有问题的回答整理为一份报告，附上关键命令的输出截图或文本。
