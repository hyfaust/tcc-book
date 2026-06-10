# 练习 3：修改 tcc 源码

## 目标

通过修改 TinyCC 编译器的源码并重新构建，验证你对编译器代码结构的理解。这是"自举"思维的入门练习——你将修改编译器本身。

## 前提条件

- 已构建 TinyCC（参见练习 1）
- 能够使用文本编辑器修改 C 源码
- 理解 `main()` 函数的基本结构

## 任务

修改 `tcc.c` 中的 `main()` 函数，在编译开始时打印一条自定义消息。

## 步骤

### 第一步：定位 main() 函数

打开 `tcc.c`，找到 `main()` 函数。它的原型是：

```c
int main(int argc, char **argv)
```

函数大约在文件的第 280 行附近（行号可能因版本略有差异）。

在 `main()` 函数体的开头，你会看到变量声明，然后是对 `tcc_new()` 的调用：

```c
int main(int argc, char **argv)
{
    TCCState *s, *s1;
    int ret, opt, n = 0, t = 0, done;
    unsigned start_time = 0, end_time = 0;
    const char *first_file;
    // ...

redo:
    argc = argc0, argv = argv0;
    s = s1 = tcc_new();
    // ...
```

### 第二步：添加 printf 语句

在 `tcc_new()` 调用之后（即 `s = s1 = tcc_new();` 这一行之后），添加一行 `printf` 调用：

```c
    s = s1 = tcc_new();
    printf("=== Hello from modified tcc! ===\n");
```

**注意**：`<stdio.h>` 已经通过 `tcc.h` 被间接包含，所以不需要额外添加 `#include`。

### 第三步：重新编译 tcc

```bash
make clean
make
```

### 第四步：验证修改

```bash
# 运行修改后的 tcc
./tcc --version
```

你应该在输出的开头看到：

```
=== Hello from modified tcc! ===
```

**注意**：`--version` 会触发 `main()` 中的正常流程，你的消息应该出现在版本信息之前。如果用 `-v` 选项（单个 `v`），流程略有不同（它在 `tcc_new()` 后就直接打印版本并返回），你的消息可能不会出现。请使用 `--version` 或正常编译来验证。

### 第五步：编译一个程序

```bash
./tcc -run book/ch01-intro/examples/hello.c
```

观察输出。你的自定义消息是否也出现了？为什么？

### 第六步：修改消息内容

将消息改为包含命令行参数信息：

```c
    s = s1 = tcc_new();
    printf("=== tcc modified: compiling with %d arguments ===\n", argc);
```

重新编译并测试：

```bash
make
./tcc book/ch01-intro/examples/hello.c -o /tmp/hello_test
./tcc -run book/ch01-intro/examples/hello.c
```

**问题 3.1**：`argc` 的值在不同调用方式下是多少？记录并解释。

### 第七步（进阶）：添加编译统计

在 `tcc.c` 的 `main()` 函数中，找到 `tcc_print_stats()` 的调用（在 `-bench` 选项的处理路径中）。研究 `TCCState` 中以下字段的含义：

```c
s->total_idents    // 编译期间遇到的标识符总数
s->total_lines     // 编译的源码行数
s->total_bytes     // 编译的源码字节数
```

在 `tcc_delete(s)` 之前，无条件打印一条编译统计消息：

```c
    printf("=== tcc stats: %d lines, %d idents, %u bytes ===\n",
           s->total_lines, s->total_idents, s->total_bytes);
    tcc_delete(s);
```

重新编译并使用不同大小的输入文件测试：

```bash
make
./tcc -run book/ch01-intro/examples/hello.c
./tcc -run book/ch01-intro/examples/simple_math.c
```

**问题 3.2**：两个程序的编译统计分别是多少？

## 思考题

**问题 3.3**：`main()` 函数中的 `redo:` 标签和 `goto redo;` 语句有什么作用？在什么情况下会触发重新编译？

**问题 3.4**：如果你在 `tcc_new()` 之前添加 printf，会发生什么？为什么？

**问题 3.5**：`tcc_delete(s)` 做了什么？如果注释掉这行会导致什么问题？

## 提交

- 回答所有 **问题 3.x**
- 提交你修改的 `tcc.c` 的 diff（使用 `git diff`）
- 附上运行修改后 tcc 的输出截图
