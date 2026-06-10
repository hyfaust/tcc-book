# 附录 A：新手入门预备知识

> 本附录面向完全没有编译器开发经验的读者，介绍理解 tinycc 源码所需的 C 语言、
> 编译原理和计算机系统基础知识，并提供学习路径建议。

---

## 第一部分：C 语言基础回顾

### 1.1 你需要掌握的 C 语言核心概念

阅读 tinycc 源码需要扎实的 C 语言基础。以下是必须掌握的概念：

#### 指针与内存

```c
int x = 42;
int *p = &x;      // p 指向 x 的地址
*p = 100;          // 通过指针修改 x 的值

// 指针算术
int arr[5] = {10, 20, 30, 40, 50};
int *q = arr;      // q 指向 arr[0]
q++;               // q 现在指向 arr[1]

// 函数指针
int (*func_ptr)(int, int) = add;  // func_ptr 指向 add 函数
int result = func_ptr(3, 4);      // 通过函数指针调用
```

在 tinycc 中，指针无处不在。`Sym *s` 表示一个符号，`Section *sec` 表示一个 ELF 段，
`char *data` 表示一段数据缓冲区。

#### 结构体与联合体

```c
// 结构体：多个字段组合在一起
struct Point {
    int x;
    int y;
};
struct Point p = {10, 20};

// 联合体：多个字段共享同一块内存
union Value {
    int i;
    float f;
    char *s;
};
union Value v;
v.i = 42;     // 此时内存中存的是整数
v.f = 3.14;   // 此时内存中存的是浮点数（覆盖了 i）

// 位域：在结构体中精确控制每个字段占多少位
struct Flags {
    unsigned int is_const : 1;   // 1 bit
    unsigned int is_static : 1;  // 1 bit
    unsigned int type : 4;       // 4 bits
};
```

tinycc 中的 `CType` 结构体使用位域来紧凑地编码类型信息（见 04-语法分析文档）。

#### 位操作

```c
// 按位与 &：两个位都为 1 时结果为 1
int a = 0b1100 & 0b1010;  // 结果: 0b1000 = 8

// 按位或 |：任一位为 1 时结果为 1
int b = 0b1100 | 0b1010;  // 结果: 0b1110 = 14

// 按位异或 ^：两位不同时结果为 1
int c = 0b1100 ^ 0b1010;  // 结果: 0b0110 = 6

// 按位取反 ~：0 变 1，1 变 0
int d = ~0b1100;           // 结果: ...11110011

// 左移 << 和右移 >>
int e = 1 << 4;            // 结果: 16 (0b10000)
int f = 32 >> 3;           // 结果: 4

// 常见用法：标志位检查
#define VT_UNSIGNED  0x0010
#define VT_CONSTANT  0x0100

int type = VT_INT | VT_UNSIGNED | VT_CONSTANT;

if (type & VT_UNSIGNED)     // 检查是否设置了 unsigned 标志
    printf("unsigned\n");

int base_type = type & 0x0F;  // 取低 4 位作为基本类型
```

tinycc 大量使用位操作来编码和解码类型信息。`CType.t` 字段的不同比特位分别表示
基本类型、修饰符、存储类等信息。

#### 函数指针与回调

```c
// 回调函数模式
typedef void (*ErrorFunc)(void *opaque, const char *msg);

void set_error_handler(ErrorFunc func, void *data) {
    // 保存 func 和 data，出错时调用
}

void my_handler(void *opaque, const char *msg) {
    printf("Error: %s\n", msg);
}

// 使用
set_error_handler(my_handler, NULL);
```

tinycc 的 libtcc API 使用这种模式实现错误回调（见 07-libtcc 文档）。

#### 预处理器

```c
// 宏定义
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 条件编译
#ifdef TCC_TARGET_X86_64
    // x86_64 特定代码
#elif defined(TCC_TARGET_ARM64)
    // ARM64 特定代码
#endif

// 宏拼接 ##
#define MAKE_FUNC(name) void name##_init(void)
MAKE_FUNC(server);  // 展开为: void server_init(void)
```

tinycc 自身就是一个预处理器的实现（tccpp.c），同时也大量使用预处理器来管理
多平台代码。

### 1.2 C 语言中新手容易混淆的概念

| 概念 | 说明 |
|------|------|
| 声明 vs 定义 | 声明告诉编译器"有这个东西"，定义是"创建这个东西" |
| 左值 vs 右值 | 左值有地址（可以取地址），右值是临时值 |
| 数组 vs 指针 | 数组名在大多数上下文中退化为指针，但 `sizeof` 行为不同 |
| `const int *p` vs `int *const p` | 前者指针指向的内容不可变，后者指针本身不可变 |
| `void *` | 通用指针，可以与任何指针类型互转（C 语言中） |
| 位域 | 结构体中可以指定每个字段占多少位 |

---

## 第二部分：编译器基础知识

### 2.1 什么是编译器？

编译器是一个**翻译程序**，将人类可读的源代码翻译为计算机可执行的机器代码。

```
人类写的代码              计算机执行的指令
─────────────            ────────────────
int x = 1 + 2;    →     mov eax, 1
                          add eax, 2
                          mov [rbp-4], eax
```

### 2.2 编译的四个经典阶段

大多数编译器教科书将编译过程分为四个阶段：

```
┌─────────────┐
│  源代码      │    "int x = 1 + 2;"
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  词法分析    │    将字符流拆分为 token
│  (Lexer)    │    "int" "x" "=" "1" "+" "2" ";"
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  语法分析    │    将 token 流组织成语法树
│  (Parser)   │    声明(类型=int, 名=x, 初值=加法(1, 2))
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  语义分析    │    检查类型、作用域等
│  + 优化      │    常量折叠: 1+2 → 3
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  代码生成    │    生成目标机器代码
│  (CodeGen)  │    mov eax, 3; mov [rbp-4], eax
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  目标代码    │    可执行文件或目标文件
└─────────────┘
```

#### 阶段 1：词法分析 (Lexical Analysis)

**做什么**：将源代码的字符流切分为有意义的单元——**token**（词法单元）。

```
源代码: int x = 42 + y;

Token 序列:
  [关键字:int] [标识符:x] [运算符:=] [整数:42] [运算符:+] [标识符:y] [分号:;]
```

就像英语中把一句话拆分成单词和标点符号。

#### 阶段 2：语法分析 (Syntax Analysis)

**做什么**：根据语法规则将 token 组织成**语法树**（AST, Abstract Syntax Tree）。

```
       声明
      / | \
    int  x  =
            |
          加法
         /    \
       42      y
```

就像英语中分析句子的主语、谓语、宾语结构。

#### 阶段 3：语义分析 + 优化

**做什么**：检查语义正确性（类型匹配、变量是否声明等），并进行优化。

```
优化前: 42 + y
优化后: 如果 y 是常量 8，直接计算 42 + 8 = 50
```

#### 阶段 4：代码生成

**做什么**：将语法树翻译为目标机器的指令序列。

```
x86_64 汇编:
    mov  eax, 50          ; 将 50 放入寄存器 eax
    mov  [rbp-4], eax     ; 将 eax 的值存到栈上变量 x 的位置
```

### 2.3 tinycc 的特殊之处：单遍编译

传统编译器（如 GCC、Clang）会完整地构建语法树，然后遍历树来生成代码。
tinycc 则完全不同——它**边解析边生成代码**，没有中间的语法树：

```
传统编译器 (GCC/Clang):
  源码 → 词法分析 → 语法树 → 语义分析 → 优化 → 代码生成 → 目标代码

tinycc:
  源码 → 词法分析 → [解析+代码生成同时进行] → 目标代码
```

这就像同声传译（边听边翻译）vs 笔译（听完再翻译）。同声传译更快，但翻译质量可能不如笔译。

### 2.4 关键编译术语表

| 术语 | 英文 | 含义 |
|------|------|------|
| Token | Token | 词法单元，如关键字、标识符、运算符 |
| AST | Abstract Syntax Tree | 抽象语法树，源码的树形表示 |
| 类型系统 | Type System | 编译器如何理解和检查数据类型 |
| 符号表 | Symbol Table | 存储变量、函数等名称及其属性的表 |
| 作用域 | Scope | 变量可见的代码范围 |
| 常量折叠 | Constant Folding | 编译时计算常量表达式（如 1+2→3） |
| 寄存器分配 | Register Allocation | 决定哪些变量放在 CPU 寄存器中 |
| 重定位 | Relocation | 链接时修正地址引用 |
| 段/节 | Section | ELF 文件中的逻辑分区（代码、数据等） |
| 链接 | Linking | 将多个目标文件合并为一个可执行文件 |
| JIT | Just-In-Time | 运行时即时编译执行 |

### 2.5 一张图理解编译器的输入输出

```
                    编译器 (tcc)
                    ┌─────────────────────────────────────────┐
                    │                                         │
 hello.c ──────────►│  词法分析 → 语法分析 → 代码生成 → 链接  │──────► hello (可执行文件)
 printf.c ─────────►│                                         │
 libc.a ───────────►│                                         │
                    └─────────────────────────────────────────┘

 hello.c:  用户写的源代码
 printf.c: 标准库源代码（可选）
 libc.a:   预编译的标准库（静态库）
 hello:    最终的可执行文件（机器码）
```

---

## 第三部分：计算机系统基础

### 3.1 CPU、寄存器和内存

```
┌─────────────────────────────────────────────┐
│                 CPU                          │
│  ┌─────────────────────────────────────┐    │
│  │  寄存器 (Registers)                  │    │
│  │  rax: 通用寄存器 (常用于返回值)      │    │
│  │  rbx: 通用寄存器                     │    │
│  │  rcx: 通用寄存器 (常用于计数)        │    │
│  │  rdx: 通用寄存器                     │    │
│  │  rsi: 通用寄存器 (源索引)            │    │
│  │  rdi: 通用寄存器 (目标索引)          │    │
│  │  rsp: 栈指针 (指向栈顶)              │    │
│  │  rbp: 帧指针 (指向当前函数的栈帧)    │    │
│  │  rip: 指令指针 (指向下一条指令)      │    │
│  └─────────────────────────────────────┘    │
│  ┌─────────────────────────────────────┐    │
│  │  ALU (算术逻辑单元)                  │    │
│  │  执行加减乘除、位运算、比较          │    │
│  └─────────────────────────────────────┘    │
└────────────────────┬────────────────────────┘
                     │ 总线
┌────────────────────▼────────────────────────┐
│                 内存 (RAM)                    │
│  ┌─────────────────────────────────────┐    │
│  │  高地址                              │    │
│  │  ┌─────────────────────────────┐    │    │
│  │  │  栈 (Stack)                  │    │    │
│  │  │  局部变量、函数调用信息       │    │    │
│  │  │  向下增长 ←                   │    │    │
│  │  └─────────────────────────────┘    │    │
│  │                                      │    │
│  │  ┌─────────────────────────────┐    │    │
│  │  │  堆 (Heap)                   │    │    │
│  │  │  动态分配的内存               │    │    │
│  │  │  向上增长 →                   │    │    │
│  │  └─────────────────────────────┘    │    │
│  │                                      │    │
│  │  ┌─────────────────────────────┐    │    │
│  │  │  .data (已初始化全局变量)    │    │    │
│  │  │  .bss  (未初始化全局变量)    │    │    │
│  │  │  .text (代码段)              │    │    │
│  │  │  .rodata (只读数据)          │    │    │
│  │  └─────────────────────────────┘    │    │
│  │  低地址                              │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

### 3.2 函数调用时栈帧的变化

```c
int add(int a, int b) {
    int result = a + b;
    return result;
}

int main() {
    int x = add(3, 4);
    return x;
}
```

```
调用 add(3, 4) 时的栈帧：

高地址
┌──────────────────────┐
│  main 的栈帧          │
│  ...                  │
│  x (未初始化)         │
├──────────────────────┤  ← 进入 add 后
│  返回地址 (main 中)   │  ← call 指令自动压入
│  旧的 rbp             │  ← push rbp
│  a = 3               │  ← 参数
│  b = 4               │  ← 参数
│  result = 7          │  ← 局部变量
├──────────────────────┤  ← rsp (栈指针)
低地址
```

x86_64 的函数调用约定（ABI）规定：
- 前 6 个整数参数通过寄存器传递：rdi, rsi, rdx, rcx, r8, r9
- 返回值放在 rax 中
- 调用者保存：rax, rcx, rdx, rsi, rdi, r8-r11
- 被调用者保存：rbx, rbp, r12-r15

### 3.3 ELF 文件格式

ELF (Executable and Linkable Format) 是 Linux 上的可执行文件格式：

```
┌─────────────────────┐
│  ELF Header          │  魔术数字、架构、入口地址
├─────────────────────┤
│  Program Headers     │  告诉操作系统如何加载（段信息）
├─────────────────────┤
│  .text               │  机器代码（可执行指令）
├─────────────────────┤
│  .rodata             │  只读数据（字符串常量等）
├─────────────────────┤
│  .data               │  已初始化的全局变量
├─────────────────────┤
│  .bss                │  未初始化的全局变量（不占文件空间）
├─────────────────────┤
│  .symtab             │  符号表（函数名、变量名）
├─────────────────────┤
│  .strtab             │  字符串表
├─────────────────────┤
│  .rel.text           │  代码段的重定位信息
├─────────────────────┤
│  .debug_*            │  调试信息（可选）
├─────────────────────┤
│  Section Headers     │  描述每个段的属性
└─────────────────────┘
```

你可以用 `readelf -a hello` 命令查看任何 ELF 文件的详细结构。

### 3.4 链接：将多个文件合并

```
main.c                    math.c
┌──────────────┐          ┌──────────────┐
│ int add();   │          │ int add(int a,│
│ int main() { │          │   int b) {    │
│   add(3,4);  │          │   return a+b; │
│ }            │          │ }             │
└──────┬───────┘          └──────┬────────┘
       │                         │
       ▼                         ▼
   main.o                    math.o
┌──────────────┐          ┌──────────────┐
│ 代码:        │          │ 代码:        │
│ call <未解析>│          │ add:         │
│              │          │  mov eax,edi │
│ 符号:        │          │  add eax,esi │
│ add (未定义) │          │  ret         │
│ main (导出)  │          │ 符号:        │
└──────┬───────┘          │ add (导出)   │
       │                  └──────┬────────┘
       │                         │
       └────────┬────────────────┘
                │
                ▼  链接器 (linker)
        ┌──────────────┐
        │ hello (可执行) │
        │ 代码:          │
        │ main:          │
        │  call add  ←── 解析了 add 的地址
        │ add:           │
        │  mov eax,edi   │
        │  add eax,esi   │
        │  ret           │
        └──────────────┘
```

链接器的工作：
1. **符号解析**：找到每个未定义符号（如 `add`）在哪个目标文件中定义
2. **重定位**：修正代码中对符号地址的引用

---

## 第四部分：编译器 vs 解释器

### 4.1 核心区别

```
编译器 (Compiler):
  源代码 ──编译──→ 机器码文件 ──执行──→ 结果
  (一次性翻译，之后可以反复运行)

解释器 (Interpreter):
  源代码 ──逐行读取、翻译、执行──→ 结果
  (边读边执行，每次运行都需要解释)

JIT 编译器 (Just-In-Time Compiler):
  源代码 ──编译到内存──→ 内存中的机器码 ──执行──→ 结果
  (运行时编译，tcc -run 就是这种模式)
```

### 4.2 对比表

| 特性 | 编译器 | 解释器 | JIT |
|------|--------|--------|-----|
| 执行速度 | 快（提前编译） | 慢（逐行解释） | 较快（运行时编译） |
| 启动速度 | 慢（需要先编译） | 快（直接执行） | 中等 |
| 代表语言 | C, C++, Rust | Python, Ruby | Java, JavaScript |
| tcc 对应 | `tcc -c` + 链接 | - | `tcc -run` |
| 错误发现 | 编译时发现所有错误 | 运行时才发现错误 | 混合 |

### 4.3 tcc 的独特定位

tcc 同时支持三种模式：

```bash
# 1. 传统编译模式：编译为可执行文件
tcc hello.c -o hello
./hello

# 2. JIT 模式：直接在内存中编译并执行
tcc -run hello.c

# 3. 嵌入模式：作为库嵌入到其他程序中
# 通过 libtcc API 动态编译和执行 C 代码
```

---

## 第五部分：理解 tinycc 源码的学习路径

### 5.1 推荐学习顺序

```
阶段 1：打基础（1-2 周）
├── 学习 C 语言指针、结构体、位操作
├── 了解 ELF 文件格式（readelf, objdump 命令）
├── 了解 x86_64 基本指令（mov, add, call, ret）
└── 阅读：docs/00-overview.md

阶段 2：理解编译流程（2-3 周）
├── 学习词法分析概念（正则表达式、DFA）
├── 学习语法分析概念（上下文无关文法、递归下降）
├── 阅读：docs/02-lexer.md，对照 tccpp.c 源码
├── 阅读：docs/03-preprocessor.md
└── 实践：用 tcc -E 观察预处理输出

阶段 3：深入代码（3-4 周）
├── 阅读：docs/04-parser.md，对照 tccgen.c 源码
├── 阅读：docs/05-codegen.md
├── 实践：用 tcc -S 生成汇编，对照源码理解
└── 实践：修改 tcc，添加一个简单的警告

阶段 4：系统级理解（2-3 周）
├── 阅读：docs/06-linker.md，理解 ELF 和链接
├── 阅读：docs/08-platforms.md，理解多架构支持
├── 实践：用 readelf 分析 tcc 生成的文件
└── 实践：编写使用 libtcc API 的小程序

阶段 5：专家级（持续）
├── 阅读所有文档，深入理解每个模块
├── 为 tcc 修复 bug 或添加特性
├── 参与 tcc 邮件列表讨论
└── 尝试为 tcc 添加新的语言特性
```

### 5.2 必备工具

```bash
# 编译和构建
gcc          # C 编译器（用于编译 tcc 自身）
make         # 构建工具
gdb          # 调试器（单步跟踪 tcc 执行）

# 二进制分析
readelf      # 查看 ELF 文件结构
objdump      # 反汇编目标文件
nm           # 查看符号表
strings      # 提取文件中的字符串
hexdump      # 查看文件的十六进制内容

# 代码阅读
grep/ripgrep # 在源码中搜索
ctags/cscope # 代码导航（可选）
```

### 5.3 动手实验建议

#### 实验 1：观察编译过程

```bash
# 查看预处理输出（词法分析 + 预处理的结果）
tcc -E hello.c

# 查看汇编输出（代码生成的结果）
tcc -S hello.c

# 查看目标文件（ELF 格式）
tcc -c hello.c
readelf -a hello.o

# 查看符号表
nm hello.o
```

#### 实验 2：用 GDB 跟踪 tcc 编译过程

```bash
# 用调试器运行 tcc 自身
gdb --args ./tcc -c hello.c

# 在关键函数设置断点
(gdb) break next          # 词法分析
(gdb) break decl          # 声明解析
(gdb) break unary         # 表达式解析
(gdb) break gen_op        # 代码生成
(gdb) run
(gdb) step                # 单步执行
(gdb) print tok           # 查看当前 token
(gdb) print vtop[0]       # 查看虚拟栈顶
```

#### 实验 3：修改 tcc 添加功能

```c
// 在 tccgen.c 的 decl() 函数中添加一行调试输出
// 当遇到 typedef 时打印消息
if (type.t & VT_TYPEDEF) {
    printf("Found typedef: %s\n", get_tok_str(v, NULL));
}
```

重新编译 tcc，然后编译一个包含 typedef 的 C 文件，观察输出。

#### 实验 4：使用 libtcc API

```c
// test_libtcc.c
#include "libtcc.h"
#include <stdio.h>

int main() {
    TCCState *s = tcc_new();
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    tcc_compile_string(s,
        "int square(int x) { return x * x; }");

    tcc_relocate(s, NULL);

    typedef int (*func_t)(int);
    func_t square = (func_t)tcc_get_symbol(s, "square");

    printf("square(7) = %d\n", square(7));  // 49

    tcc_delete(s);
    return 0;
}

// 编译：
// gcc -o test_libtcc test_libtcc.c -I. -L. -ltcc -ldl -lpthread
```

### 5.4 推荐阅读资料

#### 书籍

| 书名 | 适合阶段 | 说明 |
|------|----------|------|
| 《C 程序设计语言》(K&R) | 阶段 1 | C 语言经典教材 |
| 《深入理解计算机系统》(CSAPP) | 阶段 1-2 | 系统级编程必读 |
| 《编译原理》(龙书) | 阶段 2 | 编译器理论权威教材 |
| 《自己动手构造编译器》 | 阶段 2-3 | 实践导向的编译器教程 |
| 《程序员的自我修养》 | 阶段 3-4 | 链接、装载与库 |
| 《ELF 文件格式分析》 | 阶段 4 | ELF 格式详解 |

#### 在线资源

| 资源 | 说明 |
|------|------|
| [tcc 官方邮件列表](https://lists.nongnu.org/mailman/listinfo/tinycc-devel) | tcc 开发者社区 |
| [Compiler Explorer](https://godbolt.org/) | 在线查看 C 代码对应的汇编输出 |
| [ELF Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf) | ELF 格式官方规范 |
| [x86_64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI) | x86_64 调用约定规范 |
| [Crafting Interpreters](https://craftinginterpreters.com/) | 免费的编译器/解释器教程 |

### 5.5 常见问题

**Q: 我需要懂汇编语言才能读懂 tinycc 吗？**

A: 不需要精通，但需要了解基本概念。tinycc 的代码生成部分（tccgen.c + xxx-gen.c）
确实涉及汇编指令的生成，但你可以先理解平台无关的部分（词法分析、语法分析、类型系统），
这些完全不需要汇编知识。需要汇编知识的部分主要是 05-代码生成 和 08-平台支持 文档。

**Q: tinycc 的代码质量如何？适合学习吗？**

A: tinycc 的代码风格紧凑，有些地方为了性能做了较复杂的优化（如 tccpp.c 的快速路径）。
但整体来说，它是学习编译器实现的优秀材料，因为：
- 代码量小（核心文件约 2 万行），可以完整阅读
- 单遍编译架构简单直接
- 没有复杂的优化遍，逻辑清晰
- 有完整的测试套件，可以验证修改

**Q: 从 tinycc 学到的知识可以应用到其他编译器吗？**

A: 可以，但需要注意差异：
- tinycc 的单遍架构在现代编译器中不常见（GCC/Clang 使用多遍）
- tinycc 不做优化，而生产编译器的优化器非常复杂
- tinycc 的类型系统和符号表设计是通用的编译器知识
- ELF 处理、链接器、JIT 等知识完全通用

**Q: 如何为 tinycc 贡献代码？**

A: 1) 先阅读所有文档，理解架构
   2) 订阅 tcc-devel 邮件列表
   3) 从修复简单的 bug 开始（查看 bug 追踪器）
   4) 运行测试套件确保不引入回归
   5) 提交补丁到邮件列表

---

## 第六部分：快速参考卡

### 6.1 x86_64 常用指令速查

```asm
; 数据传输
mov  rax, rbx      ; rax = rbx
mov  rax, [rbx]    ; rax = *rbx (从内存加载)
mov  [rbx], rax    ; *rbx = rax (存到内存)
lea  rax, [rbx+8]  ; rax = rbx + 8 (地址计算，不访问内存)

; 算术运算
add  rax, rbx      ; rax += rbx
sub  rax, rbx      ; rax -= rbx
imul rax, rbx      ; rax *= rbx
neg  rax           ; rax = -rax

; 位运算
and  rax, rbx      ; rax &= rbx
or   rax, rbx      ; rax |= rbx
xor  rax, rbx      ; rax ^= rbx
shl  rax, 4        ; rax <<= 4 (左移)
shr  rax, 4        ; rax >>= 4 (右移)

; 比较和跳转
cmp  rax, rbx      ; 比较 rax 和 rbx，设置标志位
je   label         ; 相等时跳转 (Jump if Equal)
jne  label         ; 不等时跳转
jl   label         ; 小于时跳转 (Jump if Less)
jmp  label         ; 无条件跳转

; 函数调用
call func          ; 调用函数（压入返回地址）
ret                ; 返回（弹出返回地址并跳转）
push rax           ; 压栈
pop  rax           ; 出栈
```

### 6.2 ELF 文件常用命令

```bash
readelf -h file       # 查看 ELF 头
readelf -S file       # 查看所有段（Section）
readelf -s file       # 查看符号表
readelf -r file       # 查看重定位信息
readelf -l file       # 查看程序头（Segment）

objdump -d file       # 反汇编代码段
objdump -t file       # 查看符号表
objdump -s file       # 以十六进制显示所有段

nm file               # 列出符号
file file             # 识别文件类型
size file             # 查看各段大小
```

### 6.3 GDB 调试速查

```bash
gdb ./program         # 启动调试
break main            # 在 main 设置断点
break tccgen.c:464    # 在指定文件行号设置断点
run                   # 开始运行
next (n)              # 单步（不进入函数）
step (s)              # 单步（进入函数）
continue (c)          # 继续运行
print expr            # 打印表达式
backtrace (bt)        # 查看调用栈
info locals           # 查看局部变量
list                  # 显示源代码
quit (q)              # 退出
```
