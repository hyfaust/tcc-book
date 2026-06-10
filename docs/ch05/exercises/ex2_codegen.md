# 练习 5.2：汇编预测与验证

## 目标

通过手动预测 TinyCC 生成的 x86-64 汇编代码，然后与实际输出对比，加深对代码生成器行为的理解。

## 前置知识

- TinyCC 代码生成的基本流程（参见 5.1 节）
- `gv()` 的物化逻辑（参见 5.4 节）
- `gen_opi()` 的指令生成（参见 5.10.4 节）
- `gfunc_prolog()` / `gfunc_epilog()` 生成的序言/尾声（参见 5.10.3 节）
- 条件跳转的处理（参见 5.8 节）

## 环境准备

确保已安装 TinyCC：

```bash
# 如果尚未安装
cd /path/to/tinycc
./configure && make
# tcc 现在在当前目录
```

## 题目 1：基本条件分支

给定以下 C 代码：

```c
int abs_diff(int a, int b) {
    int d = a - b;
    if (d < 0)
        d = -d;
    return d;
}
```

### 任务 A：手动预测

根据你对 TinyCC 代码生成器的理解，预测它会生成的 x86-64 汇编。回答以下问题：

1. 函数序言需要分配多少栈空间？为什么？
2. `d = a - b` 生成哪些指令？
3. `if (d < 0)` 如何生成条件跳转？是用 `testl %eax, %eax; js` 还是 `cmpl $0, %eax; jl`？
4. `d = -d` 如何实现？是用 `neg` 指令还是 `0 - d`？
5. `return d` 需要额外的 `mov` 指令吗？

请在下方写出你预测的完整汇编：

```asm
; 你的预测
abs_diff:
    ; ...
```

### 任务 B：实际验证

使用 TinyCC 生成汇编并与你的预测对比：

```bash
# 将上述代码保存为 abs_diff.c，然后：
./tcc -S -o abs_diff.s abs_diff.c

# 查看生成的汇编
cat abs_diff.s
```

### 任务 C：差异分析

对比你的预测和实际输出，回答：

1. 有哪些差异？
2. TinyCC 使用了你没预料到的指令吗？
3. TinyCC 的寄存器分配与你预期的一致吗？

---

## 题目 2：循环

给定以下 C 代码：

```c
int sum_array(int *arr, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += arr[i];
    }
    return sum;
}
```

### 任务 A：手动预测

预测 TinyCC 生成的汇编。特别关注：

1. 循环条件 `i < n` 的比较和跳转指令
2. `arr[i]` 的地址计算：`arr + i * sizeof(int)` 如何翻译？
3. `sum += arr[i]` 的指令序列
4. 循环的跳转结构（前向跳转还是后向跳转？）

### 任务 B：验证

```bash
# 保存为 sum_array.c
./tcc -S -o sum_array.s sum_array.c
cat sum_array.s
```

### 任务 C：分析

1. TinyCC 是否对 `arr[i]` 的地址计算使用了 `lea` 还是 `imul`？
2. 循环的跳转指令是 `jmp`（无条件回跳）还是 `jcc`（条件回跳）？
3. 与 GCC -O0 的输出相比，有什么结构性差异？

---

## 题目 3：类型转换

给定以下 C 代码：

```c
double int_to_double(int x) {
    double result = (double)x;
    return result;
}

int double_to_int(double x) {
    int result = (int)x;
    return result;
}

int mixed_arithmetic(int a, float b) {
    return a + (int)b;
}
```

### 任务 A：手动预测

对每个函数预测汇编输出。特别关注：

1. `int → double`：使用什么指令？（`cvtsi2sd`）
2. `double → int`：使用什么指令？（`cvttsd2si`）
3. `float` 参数通过什么寄存器传入？（`xmm0` 还是通用寄存器？）
4. 浮点返回值使用什么寄存器？（`xmm0`）

### 任务 B：验证

```bash
./tcc -S -o type_conv.s type_conv.c
cat type_conv.s
```

---

## 题目 4：结构体操作

给定以下 C 代码：

```c
struct Point {
    int x;
    int y;
};

struct Point add_points(struct Point a, struct Point b) {
    struct Point result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    return result;
}
```

### 任务 A：手动预测

根据 SysV AMD64 ABI：

1. `struct Point`（8 字节）如何作为参数传递？是通过寄存器还是栈？
2. 如果通过寄存器，使用哪些寄存器？
3. 返回值如何传递？是通过 `rax` 还是通过隐式指针参数？
4. `a.x` 和 `b.x` 如何从寄存器中提取？

### 任务 B：验证

```bash
./tcc -S -o struct.s struct.c
cat struct.s
```

### 评分标准

| 评分项 | 满分 | 说明 |
|--------|------|------|
| 函数序言/尾声正确 | 20 | push/sub/mov 序列、leave/ret |
| 参数加载正确 | 20 | 寄存器→栈的保存指令 |
| 算术运算正确 | 20 | 正确的指令和操作数 |
| 控制流正确 | 20 | 条件跳转的类型和目标 |
| 寄存器分配合理 | 20 | 使用了正确的寄存器 |

每题满分 100 分，共 4 题。总分 400 分。

## 提示

1. TinyCC 不做优化，所以每个 C 语句通常对应独立的指令序列。不要期望看到跨语句的寄存器复用优化。
2. TinyCC 的函数序言总是使用 `push %rbp; mov %rsp, %rbp; sub $N, %rsp`，参数总是先保存到栈帧。
3. 局部变量的地址相对于 `%rbp` 是负偏移。
4. 条件表达式 `if (x < 0)` 可能被编译为 `cmpl $0, %eax; jge`（跳过 if 体）或 `testl %eax, %eax; jns`（测试符号位）。
5. 结构体赋值可能使用 `memmove` 调用而非逐字段复制。
