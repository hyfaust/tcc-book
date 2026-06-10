# 练习 2：追踪程序的编译阶段

## 目标

使用 TinyCC 的命令行选项，观察一个 C 程序在预处理、编译、汇编、链接各阶段的输出，加深对编译器各阶段功能的理解。

## 前提条件

- 已构建 TinyCC（参见练习 1）
- 对 C 语言有基本了解

## 被编译的程序

使用以下程序作为实验对象（也可以使用 `examples/simple_math.c`）：

```c
#define SQUARE(x) ((x) * (x))

static int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    int x = 5;
    int y = SQUARE(x + 1);
    int z = add(y, 3);
    return z;
}
```

将此代码保存为 `/tmp/trace.c`。

## 步骤

### 阶段 1：预处理（-E）

```bash
tcc -E /tmp/trace.c -o /tmp/trace.i
```

**问题 2.1**：查看 `/tmp/trace.i` 的内容。回答以下问题：
- `SQUARE(x + 1)` 被展开成了什么？注意括号的作用。
- 文件中有多少行？为什么比原始源码多那么多行？
- 你是否看到了 `#` 开头的行标记？它们的格式是什么？有什么作用？

**提示**：`-P` 选项可以去掉行标记：`tcc -E -P /tmp/trace.c`

### 阶段 2：汇编代码生成（-S）

```bash
tcc -S /tmp/trace.c -o /tmp/trace.s
```

**问题 2.2**：查看 `/tmp/trace.s` 的内容。回答以下问题：
- 识别 `main` 函数对应的汇编代码段。
- `add` 函数是否出现在汇编输出中？为什么？（提示：`static` 关键字。）
- `SQUARE(x + 1)` 的计算对应哪些汇编指令？
- 如果你是 x86-64 平台，函数参数通过哪些寄存器传递？

### 阶段 3：目标文件生成（-c）

```bash
tcc -c /tmp/trace.c -o /tmp/trace.o
```

**问题 2.3**：使用工具分析目标文件：

```bash
# 查看目标文件类型
file /tmp/trace.o

# 查看节头（ELF section headers）
readelf -S /tmp/trace.o

# 查看符号表
readelf -s /tmp/trace.o

# 查看重定位条目（如果有）
readelf -r /tmp/trace.o
```

回答：
- 目标文件中有哪些节（section）？
- 符号表中列出了哪些符号？哪些是局部的（local），哪些是全局的（global）？
- `add` 函数出现在符号表中吗？它的绑定（bind）属性是什么？
- 有没有重定位条目？如果有，它们引用了什么符号？

### 阶段 4：链接（生成可执行文件）

```bash
tcc /tmp/trace.c -o /tmp/trace
```

**问题 2.4**：使用工具分析可执行文件：

```bash
# 查看文件类型
file /tmp/trace

# 查看程序头（program headers，描述如何加载到内存）
readelf -l /tmp/trace

# 查看动态段（dynamic section）
readelf -d /tmp/trace

# 查看使用了哪些动态库
ldd /tmp/trace
```

回答：
- 可执行文件的入口点地址是什么？
- 有哪些可加载段（LOAD segments）？它们的权限是什么（R, W, E）？
- 程序依赖哪些动态库？
- ELF 解释器（interpreter）的路径是什么？

### 阶段 5：运行

```bash
/tmp/trace
echo "Exit code: $?"
```

**问题 2.5**：程序的返回值是什么？为什么？（提示：追踪 `SQUARE(5+1)` 和 `add()` 的计算过程。）

### 综合分析

**问题 2.6**：填写下表，比较各阶段的输出规模：

| 阶段 | 选项 | 输出格式 | 输出大小（字节） | 输出行数 |
|:-----|:-----|:---------|:----------------|:---------|
| 预处理 | `-E` | C 源码 | ? | ? |
| 编译 | `-S` | 汇编代码 | ? | ? |
| 汇编 | `-c` | 目标文件 (ELF) | ? | N/A |
| 链接 | `-o` | 可执行文件 (ELF) | ? | N/A |

**问题 2.7**：使用 `trace_compile.sh` 脚本自动完成上述分析：

```bash
./book/ch01-intro/examples/trace_compile.sh /tmp/trace.c
```

比较脚本输出与你手动分析的结果是否一致。

## 提交

回答所有 **问题 2.x**，并附上关键命令的输出。
