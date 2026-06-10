# 练习 2：追踪外部函数调用的重定位过程

## 目标

编写一个包含外部函数调用的 C 程序，使用 TinyCC 编译为目标文件，然后手动追踪重定位条目的含义和链接器的修补过程。

## 前置知识

- ELF 重定位条目结构（`Elf64_Rela`）
- x86-64 重定位类型（`R_X86_64_PLT32`、`R_X86_64_GOTPCREL`、`R_X86_64_PC32`）
- PC 相对寻址的计算方式

## 任务 1：创建测试程序

创建 `reloc_test.c`：

```c
#include <stdio.h>

int global_arr[4] = {1, 2, 3, 4};

int sum(int *arr, int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += arr[i];
    return s;
}

int main(void) {
    int result = sum(global_arr, 4);
    printf("sum = %d\n", result);
    return 0;
}
```

编译为目标文件：

```bash
tcc -c -o reloc_test.o reloc_test.c
```

## 任务 2：分析 .text 节中的占位符

### 2.1 反汇编 .text 节

```bash
objdump -d -r reloc_test.o
```

`-r` 标志会在每条指令旁边显示关联的重定位条目。

### 2.2 识别占位符

回答以下问题：

**Q1**: 在 `main` 函数中，有多少条指令含有重定位占位符（即 `00 00 00 00` 操作数）？分别是什么指令？

**Q2**: `sum` 函数中的 `for` 循环访问 `arr[i]` 时，是否有重定位？为什么？

**Q3**: `global_arr` 在 `main` 中是如何被引用的？是直接 PC 相对还是通过 GOT？

## 任务 3：逐条分析重定位条目

### 3.1 查看重定位表

```bash
readelf -r reloc_test.o
```

### 3.2 分析每条重定位

对于每个重定位条目，完成以下分析表：

```
条目 0:
  r_offset = ________
  r_info   = sym=______, type=______
  r_addend = ________
  
  对应的 .text 指令地址: ________
  对应的指令: ________________
  
  修补公式: *ptr = ________________
  
  如果目标符号的最终地址是 0x401234，
  而指令地址是 0x401050，那么修补后的值是：
  ________________________________________
```

对所有重定位条目重复以上分析。

### 3.3 PC 相对偏移的计算

对于 `R_X86_64_PLT32` 类型的重定位：

```
修补值 = 目标地址 + addend - 重定位位置地址
       = S + A - P
```

注意：x86-64 的 PC 相对寻址是从**下一条指令**开始计算的，
但 ELF 重定位中的 P 是指**当前重定位位置的地址**（即操作数的地址，不是下一条指令的地址）。
由于操作数在指令中位于 `call` 之后的 4 字节处，而 `call` 本身占 5 字节，
所以 P 实际上等于"下一条指令地址 - 1"。

**Q4**: 验证上面的公式。假设 `main` 的地址是 0x00，`sum` 的地址是 0x1d，
`call` 指令在 0x3d，操作数在 0x3e，计算修补后的值是否等于 `0x1d + (-4) - 0x3e = -0x25`？
验证 `call -0x25` 是否确实会跳转到 0x1d。

## 任务 4：追踪链接过程

### 4.1 两步链接

```bash
# 第一步：编译为 .o
tcc -c -o reloc_test.o reloc_test.c

# 第二步：链接为可执行文件
tcc -o reloc_test reloc_test.o
```

### 4.2 验证修补结果

```bash
# 反汇编可执行文件
objdump -d reloc_test | grep -A 40 '<main>:'
```

**Q5**: `main` 函数中，原来 `call 0` 的占位符现在变成了什么目标地址？

**Q6**: `global_arr` 的引用方式是否改变了？（提示：在 .o 中可能是 GOTPCREL，在可执行文件中可能直接是 PC32）

**Q7**: 如果使用 `tcc -static` 链接，`printf` 的调用方式会有什么变化？

### 4.3 对比动态链接和静态链接

```bash
# 动态链接（默认）
tcc -o reloc_dyn reloc_test.o

# 静态链接
tcc -static -o reloc_sta reloc_test.o

# 对比两者的 main 函数反汇编
objdump -d reloc_dyn | grep -A 40 '<main>:'
objdump -d reloc_sta | grep -A 40 '<main>:'
```

**Q8**: 在动态链接版本中，`printf` 是通过什么地址调用的？PLT 条目的地址是什么？

**Q9**: 在静态链接版本中，`printf` 是直接调用还是通过 PLT？

## 任务 5：手动模拟链接器

### 5.1 编写两文件程序

`a.c`：
```c
extern int multiply(int x, int y);
int factor = 5;

int main(void) {
    return multiply(factor, 3);
}
```

`b.c`：
```c
int multiply(int x, int y) {
    return x * y;
}
```

```bash
tcc -c -o a.o a.c
tcc -c -o b.o b.c
```

### 5.2 分析各自的重定位

```bash
readelf -r a.o
readelf -r b.o
readelf -s a.o
readelf -s b.o
```

**Q10**: `a.o` 中有哪些未定义符号？它们分别在哪个文件中定义？

**Q11**: `b.o` 中有重定位条目吗？为什么？

### 5.3 手动计算链接结果

假设链接后：
- `.text` 节基地址 = 0x401000
- `a.o` 的 `.text` 在 0x401000，大小 0x20
- `b.o` 的 `.text` 在 0x401020，大小 0x10
- `.data` 节基地址 = 0x402000
- `a.o` 的 `.data` 在 0x402000，`factor` 在 0x402000

**Q12**: `multiply` 的最终地址是什么？

**Q13**: `a.o` 中调用 `multiply` 的 `call` 指令在 0x40101x 处，计算修补后的相对偏移。

**Q14**: 验证你的计算：`call` 目标 = 当前 PC + 偏移 = 0x40102x（应该等于 multiply 的地址）。

## 思考题

1. 为什么 `global_arr` 在动态链接时需要通过 GOT 访问，而在静态链接时可以直接 PC 相对访问？
2. `R_X86_64_PLT32` 和 `R_X86_64_PC32` 有什么区别？为什么 tcc 对内部函数也使用 PLT32？
3. 重定位条目中的 `r_addend` 字段有什么作用？为什么 tcc 生成的 addend 经常是 -4？
4. 如果一个函数既被调用（call）又被取地址（&func），会产生几种重定位？

## 提交要求

1. 完成所有 Q1-Q14 的回答
2. 附上关键的反汇编输出
3. 对于 Q13-Q14，展示完整的计算过程
