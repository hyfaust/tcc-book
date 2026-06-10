# 第一章：编译器导论与 TinyCC 概览

> *"Any sufficiently complicated C or Fortran program contains an ad hoc,
> informally-specified, bug-ridden, slow implementation of half of Common Lisp."*
> — Greenspun's Tenth Rule

> *"C is quirky, flawed, and an enormous success."*
> — Dennis Ritchie

---

## 1.1 什么是编译器

### 1.1.1 从源码到机器码

编译器（compiler）是一种将高级编程语言编写的**源程序**翻译为目标机器可直接执行的**低级代码**的程序。这个翻译过程不是简单的逐字替换——它涉及对源程序语法结构和语义含义的深层理解，以及对目标机器指令集的精确映射。

从形式化角度而言，编译器是一个函数：

```
compile : SourceCode -> TargetCode
```

其中 `SourceCode` 是满足某编程语言语法和语义约束的字符串集合，`TargetCode` 是目标机器指令编码的集合。

### 1.1.2 编译器、解释器与 JIT

程序执行的三种主要范式各有不同的权衡：

```
┌─────────────────────────────────────────────────────────────────────┐
│                    程序执行模型对比                                   │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  编译器 (Compiler)                                                   │
│  ═══════════════                                                     │
│  源码 ──[编译]──> 目标文件 ──[链接]──> 可执行文件 ──[运行]──> 结果     │
│                                                                     │
│  特点: 编译一次, 运行多次。运行时无额外开销。                           │
│  代表: GCC, Clang, TinyCC                                            │
│                                                                     │
│  ┌─────────┐    ┌─────────┐    ┌──────────┐    ┌────────┐           │
│  │ hello.c │───>│ compiler│───>│ hello.o  │───>│ hello  │──> 输出    │
│  └─────────┘    └─────────┘    └──────────┘    └────────┘           │
│                                                                     │
│                                                                     │
│  解释器 (Interpreter)                                                │
│  ═══════════════════                                                 │
│  源码 ──[逐行解释执行]──> 结果                                        │
│                                                                     │
│  特点: 无需编译步骤, 但每次执行都需要重新解析源码, 运行速度慢。          │
│  代表: Python (CPython), Ruby (MRI), Bash                            │
│                                                                     │
│  ┌─────────┐    ┌─────────────┐                                     │
│  │ hello.py│───>│ interpreter │──> 逐行执行并输出                     │
│  └─────────┘    └─────────────┘                                     │
│                                                                     │
│                                                                     │
│  即时编译器 (JIT Compiler)                                            │
│  ═════════════════════════                                           │
│  源码/字节码 ──[运行时编译热点代码]──> 机器码 ──[执行]──> 结果          │
│                                                                     │
│  特点: 兼顾跨平台与运行速度, 首次执行较慢, 热点代码加速。               │
│  代表: Java (HotSpot), JavaScript (V8), .NET ( RyuJIT)               │
│                                                                     │
│  ┌─────────┐    ┌──────────┐    ┌─────────────┐    ┌────────┐       │
│  │ Foo.java│───>│ javac    │───>│ Foo.class   │───>│  JVM   │       │
│  └─────────┘    │ (前端)   │    │ (字节码)    │    │  JIT   │       │
│                 └──────────┘    └─────────────┘    │ 编译   │──>结果│
│                                                    │ 执行   │       │
│                                                    └────────┘       │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

三者的核心区别在于**编译时机**：

| 特性         | 编译器         | 解释器         | JIT 编译器       |
|:------------|:--------------|:--------------|:----------------|
| 编译时机      | 程序运行前      | 边运行边解释    | 运行时动态编译    |
| 运行速度      | 快             | 慢            | 快（热身后）      |
| 启动速度      | 慢（需编译）    | 快            | 快               |
| 错误检测时机   | 编译时         | 运行时         | 运行时            |
| 跨平台性      | 需重新编译      | 需要解释器      | 需要虚拟机        |

TinyCC 是一个**纯编译器**：它将 C 源码翻译为本地机器码，生成标准的 ELF（Linux）、PE（Windows）或 Mach-O（macOS）目标文件。但 TinyCC 同时支持 `-run` 选项，可以像脚本解释器一样直接运行 C 源码——这赋予了它类似解释器的便利性，但内部机制仍然是先编译再执行。

### 1.1.3 为什么学习编译器

理解编译器的工作原理有三个重要的实际意义：

1. **写出更高效的代码**：理解编译器如何翻译代码，可以写出更利于优化的程序。
2. **调试更有效率**：理解汇编输出和调试信息的生成方式，有助于定位底层 bug。
3. **掌握计算基础设施**：编译器是整个软件栈的根基；理解它，就理解了从源码到执行的完整链路。

选择 TinyCC 作为学习对象，是因为它的代码量小（约 7 万行 C 代码），架构清晰，且是**真正可用的编译器**——不是教学用的玩具。

---

## 1.2 编译器的经典阶段

一个编译器的内部工作通常被分解为若干**阶段**（phase）。虽然不同编译器的实现细节各异，但经典框架如下：

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      编译器的经典阶段                                      │
│                                                                          │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌───────────┐             │
│  │ 源代码    │──>│ 词法分析  │──>│ 语法分析  │──>│ 语义分析   │             │
│  │ (文本)   │   │ (Lexer)  │   │ (Parser) │   │ (Semantic)│             │
│  └──────────┘   └──────────┘   └──────────┘   └───────────┘             │
│                      │              │               │                     │
│                      v              v               v                     │
│                   token流       语法树/            带类型标注的             │
│                               递归下降             中间表示                │
│                                                          │               │
│                                                          v               │
│                                                  ┌──────────────┐        │
│                                                  │ 中间代码生成  │        │
│                                                  │ (IR Gen)     │        │
│                                                  └──────────────┘        │
│                                                          │               │
│                                                          v               │
│                                                  ┌──────────────┐        │
│                                                  │ 代码优化     │        │
│                                                  │ (Optimize)   │        │
│                                                  └──────────────┘        │
│                                                          │               │
│                                                          v               │
│                                                  ┌──────────────┐        │
│                                                  │ 目标代码生成  │        │
│                                                  │ (Code Gen)   │        │
│                                                  └──────────────┘        │
│                                                          │               │
│                                                          v               │
│                                                  ┌──────────────┐        │
│                                                  │ 汇编/链接     │        │
│                                                  │ (Asm/Link)   │        │
│                                                  └──────────────┘        │
│                                                          │               │
│                                                          v               │
│                                                    可执行文件             │
└──────────────────────────────────────────────────────────────────────────┘
```

下面我们逐一讲解每个阶段。

### 1.2.1 词法分析（Lexical Analysis）

词法分析器（lexer / scanner / tokenizer）的任务是将源代码字符流转换为**记号流**（token stream）。

源代码在文件中表现为一连串的字符（字节）。词法分析器按照语言的**词法规则**（通常用正则表达式描述）将这些字符切分为有意义的基本单元——记号。例如，C 语言源码：

```c
int main(void) {
    return 0;
}
```

经过词法分析后，产生如下记号序列：

```
TOK_INT    "int"
TOK_IDENT  "main"
'('        "("
TOK_VOID   "void"
')'        ")"
'{'        "{"
TOK_RETURN "return"
TOK_CINT   0
';'        ";"
'}'        "}"
TOK_EOF
```

每个记号（token）是一个二元组 `<记号类型, 属性值>`。记号类型是一个枚举整数（如 `TOK_INT`），属性值是记号的具体内容（如标识符的名字、整数常量的值）。

词法分析需要处理的技术细节包括：

- **空白和注释的跳过**：空格、制表符、换行符和注释不产生记号。
- **预处理指令**：在 C 语言中，`#include`、`#define` 等预处理指令在词法阶段就需要被处理。
- **关键字与标识符的区分**：`int` 是关键字（`TOK_INT`），而 `integer` 是标识符（`TOK_IDENT`）。
- **数字常量的解析**：需要处理十进制、八进制（`0` 前缀）、十六进制（`0x` 前缀）、浮点数、后缀（`L`, `U`, `f`）等。
- **字符串常量的解析**：处理转义序列（`\n`, `\t`, `\\`）和拼接。

在 TinyCC 中，词法分析由 `tccpp.c`（预处理器 + 词法分析器）完成。核心函数是 `next()` 和 `next_nomacro()`，它们从输入缓冲区读取字符并返回下一个记号。记号的类型定义在 `tcctok.h` 中，通过 `DEF(id, str)` 宏展开为枚举值。

### 1.2.2 语法分析（Syntax Analysis）

语法分析器（parser）的任务是根据语言的**语法规则**（通常用上下文无关文法描述）将记号序列组织为**语法结构**。

在传统编译器中，语法分析通常产生一棵**抽象语法树**（Abstract Syntax Tree, AST）。例如：

```
                    ┌───────────┐
                    │ FunctionDecl│
                    │ name: main  │
                    │ ret: int    │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │  Compound  │
                    │  Stmt      │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │  Return    │
                    │  Stmt      │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │  IntLit    │
                    │  val: 0    │
                    └───────────┘
```

语法分析器需要解决的核心问题包括：

- **表达式的优先级和结合性**：`a + b * c` 应被解析为 `a + (b * c)`。
- **二义性消除**：`if (a) if (b) s1 else s2` 中的 `else` 归属问题（悬垂 else 问题）。
- **函数声明与函数调用的区分**：`f(x)` 在不同上下文中含义不同。

### 1.2.3 语义分析（Semantic Analysis）

语义分析器在语法分析的基础上检查程序的**语义正确性**——即程序的含义是否符合语言规范。语法正确的程序不一定语义正确。例如：

```c
int x = "hello";    // 语法正确, 语义错误: 类型不兼容
int f() { return; } // 语法正确, 语义错误: 缺少返回值
int a[3]; a[5];     // 语法正确, 语义上可能有数组越界
```

语义分析的核心工作包括：

1. **类型检查**：验证表达式的类型是否合法，如赋值语句两侧的类型兼容性。
2. **类型推导/转换**：确定隐式类型转换规则（如 `int` 到 `float` 的提升）。
3. **符号表管理**：维护变量、函数、类型的作用域和绑定关系。
4. **声明匹配**：检查函数的声明与定义是否一致。
5. **常量求值**：计算编译期可确定的常量表达式。

### 1.2.4 中间代码生成与优化

在经典编译器（如 GCC、LLVM/Clang）中，经过语义分析后，源程序会被翻译为一种**中间表示**（Intermediate Representation, IR）。IR 是一种与源语言和目标机器都无关的抽象表示。

常见的 IR 形式包括：

- **三地址码**（Three-Address Code）：每条指令最多三个操作数。如 `t1 = a + b; t2 = t1 * c;`
- **SSA**（Static Single Assignment）：每个变量只被赋值一次。如 `x_1 = a_1 + b_1; x_2 = x_1 * c_1;`
- **LLVM IR**：LLVM 使用的带类型的汇编语言。

基于 IR，编译器可以执行各种**优化**，如常量折叠、死代码消除、循环不变量外提等。

### 1.2.5 目标代码生成

代码生成器将 IR（或直接从语义分析的结果）翻译为目标机器的汇编代码或机器码。这一步需要解决**寄存器分配**（哪些变量放在寄存器中）、**指令选择**（选择哪种机器指令来实现语义）、**指令调度**（指令排列顺序以利用流水线）等问题。

### 1.2.6 汇编与链接

最后阶段将汇编代码编码为二进制机器码（汇编器的工作），并将多个目标文件和库组合为最终的可执行文件（链接器的工作）。链接器解决的核心问题是**符号解析**——将函数调用和全局变量引用绑定到其定义所在的地址。

### 1.2.7 多遍编译 vs 单遍编译

上述阶段的组织方式分为两大流派：

**多遍编译**（multi-pass）：如 GCC 和 Clang，将编译过程明确分为多个独立的阶段，每阶段之间通过数据结构（AST、IR）传递信息。优点是每个阶段可以独立优化，便于支持多种源语言和目标机器。缺点是内存占用大，编译速度较慢。

**单遍编译**（single-pass）：如传统的 C 编译器（包括早期的 `cc`）和 TinyCC，在读取源代码的同时直接生成目标代码，不构建完整的 AST。优点是内存占用小、编译速度极快。缺点是优化能力有限，某些语言特性（如需要前向引用的特性）实现较复杂。

TinyCC 采用的是**单遍编译**架构。这是一个核心设计选择，深刻影响了整个代码库的组织方式。我们将在 1.4 节详细讨论。

---

## 1.3 TinyCC 项目简介

### 1.3.1 历史与作者

TinyCC（简称 tcc）由法国程序员 **Fabrice Bellard** 于 2001 年创建。Bellard 是计算机科学界的传奇人物，他同时还创建了：

- **QEMU**：广泛使用的开源机器模拟器和虚拟化平台。
- **FFmpeg**：多媒体处理框架（联合创始人）。
- **圆周率计算记录**：2009 年使用个人计算机计算了 2.7 万亿位圆周率。

TinyCC 的设计目标在项目的 `README` 文件中有清晰的陈述：

> **SMALL!** You can compile and execute C code everywhere, for example on rescue disks.
>
> **FAST!** tcc generates machine code for i386, x86_64, arm, aarch64 or riscv64. Compiles and links about 10 times faster than `gcc -O0`.
>
> **UNLIMITED!** Any C dynamic library can be used directly. TCC is heading toward full ISOC99 compliance. TCC can of course compile itself.

这三个词——小（Small）、快（Fast）、无限（Unlimited）——精确概括了 TinyCC 的设计哲学。

### 1.3.2 设计目标与特性

TinyCC 的设计目标可以概括为以下几个方面：

1. **极小的二进制大小**：tcc 可执行文件仅约 100-200KB，可以在软盘、救援磁盘等极度受限的环境中使用。
2. **极快的编译速度**：编译速度比 `gcc -O0` 快约 10 倍。这主要归功于单遍编译架构和零优化策略。
3. **C 脚本模式**：支持 `#!/usr/local/bin/tcc -run` 的 shebang 行，允许将 C 程序作为脚本直接运行。
4. **内嵌运行时**：支持 `-run` 选项在内存中编译并执行 C 代码，无需生成磁盘文件。
5. **边界检查**：可选的 `-b` 选项启用运行时内存边界检查，用于调试。
6. **自举能力**：tcc 可以编译自身——这是编译器正确性的有力证明。
7. **跨平台支持**：支持 Linux、Windows、macOS、FreeBSD 等操作系统，支持 i386、x86_64、ARM、AArch64、RISC-V 64 等目标架构。

### 1.3.3 版本与社区

本书基于 **TinyCC 0.9.28** 版本。该版本的主要更新包括：

- 新增 RISC-V 64 位目标架构支持。
- 原生 macOS（Darwin）支持。
- ARM 和 RISC-V 汇编器。
- `_Static_assert()` 和 `__attribute__((cleanup()))` 支持。
- `stdatomic.h` 支持。
- `asm goto` 支持。
- DWARF 调试信息格式支持。

TinyCC 以 **LGPL v2** 许可证发布。项目的邮件列表和 bug 跟踪在 `https://lists.nongnu.org/mailman/listinfo/tinycc-devel`。

### 1.3.4 tcc 能做什么，不能做什么

**tcc 能做的**：

- 编译大多数符合 C99 标准的 C 程序。
- 直接链接系统动态库（如 libc、libm）。
- 生成标准 ELF/PE/Mach-O 格式的目标文件和可执行文件。
- 作为 C "解释器"直接运行 C 源码。
- 生成调试信息（stabs 和 DWARF 格式）。
- 处理 GCC 的大量扩展语法（`__attribute__`、`__builtin_*` 等）。

**tcc 不擅长的**：

- 代码优化：tcc 几乎不做任何优化，直接生成简单的、逐语句对应的机器码。
- 完整的 C11/C23 支持：tcc 持续跟踪标准，但某些边缘特性可能未实现。
- 大型项目的并行编译：tcc 是单线程编译器。
- C++ 支持：tcc 仅支持 C 语言。

这些限制恰恰是 TinyCC 的设计选择：用优化能力换取编译速度和代码简洁性。

---

## 1.4 tcc 的单遍编译架构

### 1.4.1 什么是单遍编译

单遍编译（single-pass compilation）是指编译器在**一次扫描源代码的过程中**完成所有的分析和代码生成工作。与多遍编译器不同，单遍编译器不构建中间的抽象语法树（AST），不进行独立的优化阶段，而是在解析语法结构的同时直接输出目标代码。

```
多遍编译器 (GCC / Clang):

源码 ──> [词法分析] ──> token流 ──> [语法分析] ──> AST ──> [语义分析]
                                                              │
                                                              v
                                                       [IR 生成]
                                                              │
                                                              v
                                                        [IR 优化]
                                                              │
                                                              v
                                                       [代码生成]
                                                              │
                                                              v
                                                           机器码


单遍编译器 (TinyCC):

源码 ──> [词法分析 + 语法分析 + 语义分析 + 代码生成] ──> 机器码
              │
              └── 全部在一次扫描中完成, 不构建 AST
```

### 1.4.2 TinyCC 的单遍架构

在 TinyCC 中，单遍编译的具体实现方式是**边解析边生成**（parse-and-generate）。核心流程在 `tccgen.c` 的 `tccgen_compile()` 函数中：

```c
// tccgen.c 第 400 行
ST_FUNC int tccgen_compile(TCCState *s1)
{
    // ... 初始化 ...
    parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_TOK_STR;
    next();           // 读取第一个 token
    decl(VT_CONST);   // 从顶层声明开始递归下降解析
    gen_inline_functions(s1);
    // ... 收尾 ...
    return 0;
}
```

这里 `decl()` 函数是整个编译的核心递归下降入口。它处理顶层声明（变量声明、函数定义等）。当遇到函数定义时，`decl()` 调用 `block()` 来解析函数体，`block()` 又调用 `expr_eq()` 等表达式解析函数，而表达式解析函数在解析表达式的同时直接调用代码生成函数（如 `gadd()`, `gv()` 等）向当前代码段（`cur_text_section`）写入机器码。

整个过程的关键全局变量：

- `tok`：当前 token 的类型。
- `tokc`：当前 token 的值（如果是常量）。
- `vtop`：虚拟栈栈顶指针。TinyCC 使用一个**虚拟栈**来追踪表达式求值过程中的中间值。
- `ind`：当前代码段的写入位置（字节偏移）。
- `loc`：当前函数的局部变量栈帧偏移。

### 1.4.3 虚拟栈

TinyCC 引入了一个精巧的抽象——**虚拟栈**（virtual stack），用 `SValue` 结构数组实现。在解析表达式时，操作数和中间结果被推入虚拟栈；在需要时，代码生成器将虚拟栈上的值"溢出"（spill）到实际的机器寄存器或栈上。

例如，表达式 `a + b * c` 的处理过程：

```
解析 a:    vpush_sym(a)         -> 虚拟栈: [a]
解析 b:    vpush_sym(b)         -> 虚拟栈: [a, b]
解析 c:    vpush_sym(c)         -> 虚拟栈: [a, b, c]
解析 *:    gen_op(TOK_STAR)     -> 虚拟栈: [a, b*c]
解析 +:    gen_op(TOK_PLUS)     -> 虚拟栈: [a+b*c]
```

`gen_op()` 在两个操作数都是编译期常量时可以直接计算结果（常量折叠），否则生成相应的机器指令。

### 1.4.4 与 GCC / Clang 的对比

| 维度           | TinyCC               | GCC                  | Clang/LLVM           |
|:--------------|:---------------------|:---------------------|:---------------------|
| 编译遍数       | 单遍                  | 多遍                  | 多遍                 |
| 中间表示       | 无 AST，虚拟栈        | GIMPLE (SSA IR)       | LLVM IR (SSA)        |
| 优化级别       | 无优化                | O0-O3, Os, Ofast      | O0-O3, Os, Oz        |
| 编译速度       | 极快 (~10x gcc -O0)   | 慢                    | 中等                  |
| 输出代码质量   | 低（逐语句翻译）       | 高                    | 高                    |
| 代码量         | ~7万行                | ~1500万行             | ~500万行              |
| 适用场景       | 快速编译、脚本、嵌入式  | 生产环境、高性能计算   | 生产环境、开发调试    |
| 支持语言       | C                     | C/C++/Fortran/Go/...  | C/C++/Obj-C/...      |

GCC 的编译过程经过多次变换：源码 -> AST -> GENERIC -> GIMPLE -> SSA GIMPLE -> RTL -> 机器码。每一步都可以进行独立的优化。这种架构的优势在于优化能力和模块化，代价是巨大的代码量和较慢的编译速度。

TinyCC 选择了一条完全不同的路：放弃优化能力，换取极致的编译速度和极小的代码量。对于不需要优化的场景（快速原型、C 脚本、教学、嵌入式启动代码），TinyCC 是更好的选择。

### 1.4.5 单遍编译的代价

单遍架构也带来了固有的限制：

1. **前向引用问题**：在单遍扫描中，当遇到一个函数调用时，如果被调用函数尚未定义，编译器不知道其参数类型和返回类型。TinyCC 的解决方法是：对未声明的函数调用发出隐式声明警告，并假设返回 `int`（C89 的传统行为）。

2. **无法进行跨语句优化**：由于没有全局视图，无法进行循环优化、公共子表达式消除等优化。

3. **类型信息的延迟处理**：结构体的大小在遇到完整定义之前可能未知，TinyCC 使用 `Sym` 链表来延迟处理前向引用的结构体。

---

## 1.5 源码文件地图

TinyCC 0.9.28 的源码树由约 7 万行 C 代码组成。以下是每个文件的功能说明和代码行数。理解这些文件的职责，是深入阅读源码的第一步。

### 1.5.1 核心编译器文件

| 文件 | 行数 | 职责 |
|:-----|-----:|:-----|
| `tcc.h` | 2032 | **主头文件**。定义了所有核心数据结构（`TCCState`, `Sym`, `CType`, `SValue`, `Section` 等）、类型编码常量（`VT_INT`, `VT_FUNC` 等）、token 编码、以及所有模块的函数声明。所有 `.c` 文件都包含此头文件。 |
| `tccpp.c` | 3961 | **预处理器和词法分析器**。实现 `next()` 和 `next_nomacro()` 函数，负责将源代码字符流分割为 token。同时实现 C 预处理器的全部功能：`#include`、`#define`（含宏展开）、`#if`/`#ifdef` 条件编译、`#pragma` 等。 |
| `tccgen.c` | 8986 | **语法分析器 + 语义分析器 + 代码生成器**。这是 tcc 最大的单个文件。实现递归下降解析器，直接将 C 语法结构翻译为目标机器代码。核心函数包括 `decl()`（声明解析）、`block()`（语句块解析）、`expr_eq()`（表达式解析）、`unary()`（一元表达式）等。 |
| `tccasm.c` | 1525 | **内联汇编支持**。处理 `asm()` / `__asm__()` 语句，解析 AT&T 或 Intel 格式的汇编语法，将其翻译为目标机器码。 |
| `tccelf.c` | 4186 | **ELF 文件格式处理**。负责生成和读取 ELF 格式的目标文件和可执行文件，包括节（section）管理、符号表管理、重定位处理、动态链接支持（GOT/PLT）。 |
| `libtcc.c` | 2267 | **库接口和初始化**。提供 `tcc_new()`、`tcc_delete()`、`tcc_compile()`、`tcc_run()` 等公共 API。当以 ONE_SOURCE 模式编译时，此文件 `#include` 所有其他 `.c` 文件，形成单一编译单元。 |
| `tccdbg.c` | 2676 | **调试信息生成**。生成 stabs 和 DWARF 格式的调试信息，使编译产物可用 `gdb` 等调试器调试。 |
| `tccrun.c` | 1586 | **运行时执行引擎**。实现 `-run` 选项的内存编译和执行功能。将编译后的机器码加载到可执行内存中并跳转执行。 |
| `tccpe.c` | 2313 | **PE 文件格式处理**（Windows）。生成 PE 格式的 `.exe` 和 `.dll` 文件。 |
| `tccmacho.c` | 2477 | **Mach-O 文件格式处理**（macOS）。生成 Mach-O 格式的可执行文件和动态库。 |
| `tcctools.c` | 651 | **辅助工具**。实现 `tcc -ar`（静态库创建）和 `tcc -impdef`（导入库定义文件生成）功能。 |

### 1.5.2 目标架构后端

每种目标架构有三个文件：代码生成器（`-gen.c`）、链接器辅助（`-link.c`）和汇编器（`-asm.c`）。编译时只包含目标架构对应的文件。

| 文件 | 行数 | 目标架构 | 职责 |
|:-----|-----:|:---------|:-----|
| `i386-gen.c` | 1326 | Intel 32 位 x86 | 生成 32 位 x86 机器码。调用者负责寄存器分配。 |
| `i386-link.c` | 360 | Intel 32 位 x86 | 处理 32 位 x86 的重定位类型。 |
| `i386-asm.c` | 1750 | Intel 32 位 x86 | 解析 32 位 x86 内联汇编。 |
| `x86_64-gen.c` | 2332 | AMD64 / x86-64 | 生成 64 位 x86 机器码。比 i386 复杂，需处理 SSE 寄存器。 |
| `x86_64-link.c` | 452 | AMD64 / x86-64 | 处理 64 位重定位类型。 |
| `arm-gen.c` | 2369 | ARM (32 位) | 生成 ARMv4+ 机器码。 |
| `arm-link.c` | 472 | ARM (32 位) | ARM 重定位处理。 |
| `arm-asm.c` | 3092 | ARM (32 位) | ARM 内联汇编解析器。 |
| `arm64-gen.c` | 2353 | AArch64 (ARM 64 位) | 生成 ARMv8 64 位机器码。 |
| `arm64-link.c` | 406 | AArch64 | AArch64 重定位处理。 |
| `arm64-asm.c` | 2276 | AArch64 | AArch64 内联汇编解析器。 |
| `riscv64-gen.c` | 1480 | RISC-V 64 位 | 生成 RISC-V 64 位机器码。 |
| `riscv64-link.c` | 440 | RISC-V 64 位 | RISC-V 重定位处理。 |
| `riscv64-asm.c` | 3059 | RISC-V 64 位 | RISC-V 内联汇编解析器。 |
| `c67-gen.c` | 2543 | TMS320C67xx (DSP) | 生成 TI C67 DSP 机器码。 |
| `c67-link.c` | 125 | TMS320C67xx | C67 重定位处理。 |
| `il-gen.c` | 657 | .NET IL | .NET 中间语言代码生成（实验性）。 |
| `tcccoff.c` | 951 | COFF 格式 | COFF 目标文件格式处理（C67 使用）。 |

### 1.5.3 头文件

| 文件 | 行数 | 职责 |
|:-----|-----:|:-----|
| `libtcc.h` | 116 | 公共 API 头文件。定义 `tcc_new()` 等函数原型，供外部程序使用 libtcc 库。 |
| `tcclib.h` | 82 | 简化的 libc 头文件，用于在软盘等受限环境中替代标准头文件。 |
| `tcctok.h` | 441 | Token 定义文件。使用 `DEF(id, str)` 宏定义 C 关键字和内置函数名。 |
| `tccdefs_.h` | 329 | 内置宏定义。定义 `__SIZE_TYPE__`、`__INT_MAX__` 等编译器内置宏。 |
| `config.h` | 18 | 构建配置。通常由 `configure` 脚本生成。 |
| `elf.h` | 3325 | ELF 文件格式定义（数据结构和常量）。 |
| `coff.h` | 446 | COFF 文件格式定义。 |
| `dwarf.h` | 1046 | DWARF 调试信息格式定义。 |
| `stab.h` | 17 | Stabs 调试信息格式定义。 |
| `stab.def` | — | Stabs 类型定义。 |
| `i386-asm.h` | 490 | i386 汇编指令和寄存器定义。 |
| `x86_64-asm.h` | 559 | x86-64 汇编指令和寄存器定义。 |
| `i386-tok.h` | 332 | i386 汇编 token 定义。 |
| `arm-tok.h` | 406 | ARM 汇编 token 定义。 |
| `arm64-tok.h` | 840 | AArch64 汇编 token 定义。 |
| `riscv64-tok.h` | 612 | RISC-V 汇编 token 定义。 |
| `il-opcodes.h` | 251 | .NET IL 操作码定义。 |

### 1.5.4 运行时库和构建文件

| 文件/目录 | 职责 |
|:----------|:-----|
| `lib/` | 运行时支持库源码。包含 `libtcc1.c`（基本运行时函数如 `__udivdi3`）、`bcheck.c`（边界检查运行时）、`alloca.S`（栈分配实现）、`atomic.S`（原子操作）等。编译后生成 `libtcc1.a`。 |
| `include/` | tcc 自带的 C 标准头文件子集。包含 `stdarg.h`、`stddef.h`、`float.h`、`stdbool.h`、`tccdefs.h` 等。 |
| `Makefile` | 构建系统主文件。 |
| `configure` | 配置脚本，检测系统环境并生成 `config.mak`。 |
| `tests/` | 测试套件。包含 `tcctest.c`（主测试文件）等。 |
| `tcc-doc.texi` | Texinfo 格式的完整文档。 |
| `VERSION` | 版本号文件（当前内容：`0.9.28rc`）。 |

---

## 1.6 核心数据结构预览

理解 TinyCC 的关键在于理解其核心数据结构。本节对五个最重要的结构体做初步介绍，后续章节将深入分析。

### 1.6.1 TCCState：编译器全局状态

`TCCState`（定义在 `tcc.h` 中）是 TinyCC 最重要的结构体。它封装了编译器的**所有状态**——包括命令行选项、搜索路径、错误处理、节管理、符号表等。每个编译实例对应一个 `TCCState` 实例。

```c
// tcc.h 中 TCCState 的关键成员（简化）
struct TCCState {
    // --- 命令行选项 ---
    unsigned char verbose;          // -v 详细输出
    unsigned char nostdinc;         // -nostdinc 不搜索标准头文件路径
    unsigned char nostdlib;         // -nostdlib 不链接标准库
    unsigned char char_is_unsigned; // -funsigned-char
    unsigned char gnu_ext;          // GNU 扩展语法
    unsigned char tcc_ext;          // TinyCC 扩展语法
    int output_type;                // 输出类型: TCC_OUTPUT_EXE / _OBJ / _DLL / _MEMORY / _PREPROCESS
    unsigned int cversion;          // C 标准版本 (199901, 201112, ...)

    // --- 搜索路径 ---
    char **include_paths;           // -I 用户头文件搜索路径
    int nb_include_paths;
    char **sysinclude_paths;        // 系统头文件搜索路径
    int nb_sysinclude_paths;
    char **library_paths;           // -L 库搜索路径
    int nb_library_paths;

    // --- 预处理器状态 ---
    BufferedFile *include_stack[INCLUDE_STACK_SIZE]; // #include 文件栈
    int ifdef_stack[IFDEF_STACK_SIZE];                // #ifdef 条件栈

    // --- 节 (Section) 管理 ---
    Section **sections;             // 所有节的数组
    int nb_sections;
    Section *text_section;          // 代码段 (.text)
    Section *data_section;          // 数据段 (.data)
    Section *rodata_section;        // 只读数据段 (.rodata)
    Section *bss_section;           // 未初始化数据段 (.bss)
    Section *symtab_section;        // 符号表 (.symtab)
    Section *cur_text_section;      // 当前正在写入的代码段

    // --- 错误处理 ---
    int nb_errors;                  // 编译错误计数
    jmp_buf error_jmp_buf;          // 错误恢复点

    // --- 文件列表 ---
    struct filespec **files;        // 命令行输入文件列表
    int nb_files;
    char *outfile;                  // 输出文件名

    // ... 更多成员 (调试, PE/Mach-O 特定, 运行时, 性能统计等)
};
```

`TCCState` 通过 `tcc_new()` 创建，通过 `tcc_delete()` 销毁。使用 libtcc API 的典型模式：

```c
TCCState *s = tcc_new();
tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
tcc_add_file(s, "hello.c");
tcc_run(s, 0, NULL);
tcc_delete(s);
```

### 1.6.2 Sym：符号表条目

`Sym`（symbol）是 TinyCC 中表示所有程序实体的核心结构——变量、函数、类型、标签、枚举常量、宏定义等，全部用 `Sym` 表示。

```c
// tcc.h 中 Sym 的定义（简化）
typedef struct Sym {
    int v;             // 符号的 token 编号 (标识符在符号表中的唯一 ID)
    unsigned short r;  // 关联的寄存器或位置 (VT_CONST / VT_LOCAL / ...)
    struct SymAttr a;  // 符号属性 (对齐、packed、weak、visibility 等)

    union {
        struct {
            int c;     // 关联数值: 栈帧偏移 / ELF 符号索引 / 枚举值
            union {
                int sym_scope;       // 局部变量的作用域层级
                struct FuncAttr f;   // 函数属性 (调用约定、noreturn 等)
            };
        };
        long long enum_val;          // 枚举常量的值 (当 IS_ENUM_VAL 时)
        int *d;                      // 宏定义的 token 流 (当为宏时)
    };

    CType type;        // 符号的类型信息
    struct Sym *prev;  // 指向前一个同名符号 (作用域栈)
    struct Sym *prev_tok; // 指向同一 token 的前一个定义 (哈希链)
} Sym;
```

符号通过两条链管理：

- **作用域栈**（`prev` 链）：将所有符号链接为一个栈。内层作用域的符号在外层作用域同名符号之前。查找时从栈顶向下搜索。
- **token 哈希链**（`prev_tok` 链）：同一标识符名的所有定义通过此链连接，用于宏展开和符号查找。

全局符号维护在 `global_stack`，局部符号维护在 `local_stack`。进入新作用域时压栈，退出时弹栈。

### 1.6.3 CType：类型表示

`CType` 是 TinyCC 的类型描述结构，极其紧凑——只有两个字段：

```c
typedef struct CType {
    int t;            // 类型编码 (位域组合)
    struct Sym *ref;  // 引用的符号 (struct/union/enum 的定义, 或函数的参数列表)
} CType;
```

类型编码 `t` 是一个 32 位整数，通过位域组合来编码复杂的类型信息：

```
  31                    20  19   16 15  12 11    8 7  6  5 4 3  0
 ┌────────────────────────┬───────┬──────┬───────┬───┬──┬─┬──────┐
 │  bitfield shift/size   │storage│qualif│attrib │VLA│BF│A│ base │
 │  (struct fields only)  │       │      │       │   │  │R│ type │
 └────────────────────────┴───────┴──────┴───────┴───┴──┴─┴──────┘

 基本类型 (VT_BTYPE, 低 4 位):
   0  = VT_VOID      void
   1  = VT_BYTE      signed char
   2  = VT_SHORT     short
   3  = VT_INT       int
   4  = VT_LLONG     long long
   5  = VT_PTR       pointer (ref 指向所指类型)
   6  = VT_FUNC      function (ref 指向参数链表)
   7  = VT_STRUCT    struct/union (ref 指向字段链表)
   8  = VT_FLOAT     float
   9  = VT_DOUBLE    double
  10  = VT_LDOUBLE   long double
  11  = VT_BOOL      _Bool

 修饰符 (高位标志):
   VT_UNSIGNED  = 0x0010   unsigned
   VT_ARRAY     = 0x0040   array (同时有 VT_PTR)
   VT_CONSTANT  = 0x0100   const
   VT_VOLATILE  = 0x0200   volatile
   VT_EXTERN    = 0x1000   extern
   VT_STATIC    = 0x2000   static
   VT_TYPEDEF   = 0x4000   typedef
```

例如，`const unsigned int *` 的类型编码为：

```
VT_PTR | VT_CONSTANT | VT_UNSIGNED | VT_INT = 0x0040 | 0x0100 | 0x0010 | 3 = 0x0153
ref -> CType { t = VT_UNSIGNED | VT_INT = 0x0013, ref = NULL }
```

### 1.6.4 SValue：虚拟栈元素

`SValue` 表示虚拟栈上的一个值。它记录了值的类型、存放位置、以及关联的符号。

```c
typedef struct SValue {
    CType type;           // 值的类型
    unsigned short r;     // 存放位置 (寄存器编号 / VT_CONST / VT_LOCAL / VT_CMP / VT_JMP)
    unsigned short r2;    // 第二个寄存器 (用于 long long 等双寄存器值)
    union {
        struct { int jtrue, jfalse; };  // 条件跳转的正向引用链 (VT_JMP/VT_JMPI)
        CValue c;                        // 常量值 (VT_CONST)
    };
    union {
        struct { unsigned short cmp_op, cmp_r; }; // 比较操作信息 (VT_CMP)
        struct Sym *sym;                            // 关联的符号 (VT_SYM | VT_CONST)
    };
} SValue;
```

`r` 字段的编码决定了值的含义：

| `r` 值 | 含义 |
|:--------|:-----|
| 0-15 | 值在物理寄存器中（寄存器编号） |
| `VT_CONST` (0x30) | 值是常量（在 `c` 字段中） |
| `VT_LOCAL` (0x32) | 值在栈帧中（偏移量在 `c.i` 中） |
| `VT_CMP` (0x33) | 值是条件比较的结果 |
| `VT_JMP` (0x34) | 值来自条件跳转（真分支） |
| `VT_JMPI` (0x35) | 值来自条件跳转（假分支） |

虚拟栈（`vstack` 数组）和栈顶指针（`vtop`）是 TinyCC 代码生成的核心机制。每次解析一个操作数时，一个新的 `SValue` 被压入栈；每次应用一个运算符时，栈顶的一个或两个值被消费，结果被压回栈。

### 1.6.5 Section：ELF 节

`Section` 表示目标文件中的一个**节**（section），如 `.text`（代码）、`.data`（已初始化数据）、`.rodata`（只读数据）、`.bss`（未初始化数据）等。

```c
typedef struct Section {
    unsigned long data_offset;    // 当前写入偏移
    unsigned char *data;          // 节的数据缓冲区
    unsigned long data_allocated; // 已分配的缓冲区大小
    TCCState *s1;                 // 所属的编译器状态

    // ELF 节头字段
    int sh_name;           // 节名在字符串表中的索引
    int sh_num;            // 节编号
    int sh_type;           // 节类型 (SHT_PROGBITS, SHT_SYMTAB, ...)
    int sh_flags;          // 节标志 (SHF_ALLOC, SHF_WRITE, SHF_EXECINSTR, ...)
    unsigned long sh_size; // 节大小

    struct Section *link;  // 关联的节 (如符号表关联字符串表)
    struct Section *reloc; // 对应的重定位节
    char name[1];          // 节名 (变长, 如 ".text")
} Section;
```

`TCCState` 中维护了一个预定义的节集合：

```c
// TCCState 中的预定义节
Section *text_section;      // .text   - 可执行代码
Section *data_section;      // .data   - 已初始化可写数据
Section *rodata_section;    // .rodata - 只读数据 (字符串常量等)
Section *bss_section;       // .bss    - 未初始化数据
Section *symtab_section;    // .symtab - 符号表
Section *cur_text_section;  // 当前正在写入的代码段
Section *got;               // .got    - 全局偏移表 (动态链接)
Section *plt;               // .plt    - 过程链接表 (动态链接)
```

代码生成器向 `cur_text_section` 写入机器码字节，语义分析器向 `data_section` 写入全局变量的初始值，字符串常量被放入 `rodata_section`。

---

## 1.7 编译一个简单程序的完整流程

本节通过一个具体的 "hello world" 程序，追踪它在 TinyCC 内部经历的完整编译过程。

### 1.7.1 示例程序

```c
// hello.c
#include <stdio.h>

int main(void)
{
    printf("Hello, TinyCC!\n");
    return 0;
}
```

### 1.7.2 第一步：初始化

当我们在命令行执行 `tcc hello.c -o hello` 时，`tcc.c` 的 `main()` 函数首先被调用：

```c
// tcc.c main() 核心流程（简化）
int main(int argc, char **argv)
{
    TCCState *s;
    s = tcc_new();                    // 1. 创建编译器状态
    tcc_parse_args(s, &argc, &argv);  // 2. 解析命令行参数
    set_environment(s);               // 3. 设置环境变量 (C_INCLUDE_PATH 等)
    tcc_set_output_type(s, TCC_OUTPUT_EXE); // 4. 设置输出类型

    // 5. 编译所有输入文件
    while (/* 每个输入文件 */) {
        tcc_add_file(s, f->name);     // 编译单个文件
    }

    // 6. 输出可执行文件
    tcc_output_file(s, s->outfile);

    tcc_delete(s);                    // 7. 清理
    return ret;
}
```

`tcc_new()` 调用链：

```
tcc_new()                          // libtcc.c
  ├── tcc_mallocz(sizeof(TCCState)) // 分配并清零 TCCState
  ├── tccelf_new(s)                 // 初始化 ELF 相关
  └── tccpp_new(s)                  // 初始化预处理器
```

### 1.7.3 第二步：文件处理入口

`tcc_add_file()` 最终调用 `tcc_add_file_internal()`，该函数根据文件扩展名决定处理方式：

```c
// libtcc.c（简化逻辑）
ST_FUNC int tcc_add_file_internal(TCCState *s1, const char *filename, int flags)
{
    // 检测文件类型
    if (是 C 源码 (.c)) {
        // 预处理 + 编译
        tcc_compile(s1);      // 调用预处理器和编译器
    } else if (是汇编 (.S/.s)) {
        // 汇编器
    } else if (是目标文件 (.o)) {
        // 直接加载目标文件
        tcc_load_object_file(s1, fd, 0);
    } else if (是库 (.a/.so/.dll)) {
        // 加载库
        tcc_add_library(s1, filename);
    }
}
```

### 1.7.4 第三步：预处理

对于 C 源文件，`tccpp.c` 的预处理器首先被激活：

```c
// tccpp.c: preprocess_start() 设置预处理器
void preprocess_start(TCCState *s1, int filetype)
{
    // 初始化内置宏
    //   __TINYC__, __STDC__, __linux__, __x86_64__ 等
    //   定义 __SIZE_TYPE__, __INT_MAX__ 等类型相关宏

    // 处理命令行 -D/-U 选项
    // 处理命令行 -include 选项

    // 打开第一个文件
    tcc_open_bf(s1, filename, IO_BUF_SIZE);
}
```

当预处理器遇到 `#include <stdio.h>` 时：

1. 在 `sysinclude_paths` 中搜索 `stdio.h`。
2. 找到后，创建新的 `BufferedFile`，压入 `include_stack`。
3. 继续处理新文件的内容。
4. `stdio.h` 中可能包含更多的 `#include`，递归处理。
5. 遇到 `#define` 时，在 `define_stack` 中注册宏定义。
6. 遇到 `#ifdef`/`#endif` 时，维护 `ifdef_stack`。

### 1.7.5 第四步：词法分析

预处理完成后，源码被转换为 token 流。`next()` 函数（`tccpp.c`）是词法分析的核心入口：

```c
// tccpp.c: next() 的工作流程（概念性描述）
void next(void)
{
    // 1. 从输入缓冲区读取下一个非空白字符
    // 2. 根据首字符判断 token 类型:
    //    - 字母或 _   -> 标识符或关键字
    //    - 数字        -> 数字常量
    //    - "           -> 字符串常量
    //    - #           -> 预处理指令
    //    - 运算符符号  -> 运算符 token
    // 3. 将结果存入全局变量 tok 和 tokc
    // 4. 处理宏展开 (如果当前 token 是宏名)
}
```

对于 `printf("Hello, TinyCC!\n")`，词法分析产生的 token 序列是：

```
TOK_IDENT  "printf"
'('
TOK_STR    "Hello, TinyCC!\n"
')'
';'
```

### 1.7.6 第五步：语法分析 + 代码生成

`tccgen.c` 的 `decl()` 函数开始解析。对于 `#include <stdio.h>` 中声明的函数和 `main` 函数的定义：

```
decl(VT_CONST) 被调用
  │
  ├── 解析 #include 中的外部声明
  │   遇到 "int printf(const char *, ...);" 等
  │   → sym_push2(&global_stack, TOK_PRINTF, ...) 注册符号
  │   → 在 symtab_section 中添加 ELF 符号
  │
  ├── 遇到 "int main(void) {"
  │   ├── 识别 return type: int
  │   ├── 识别 function name: main
  │   ├── 注册函数符号到 global_stack
  │   ├── 创建新作用域 (local_scope++)
  │   └── 调用 block() 解析函数体
  │       │
  │       ├── printf("Hello, TinyCC!\n");
  │       │   ├── 解析 printf 为函数调用
  │       │   ├── 参数 "Hello, TinyCC!\n" 被放入 rodata_section
  │       │   ├── 生成: lea rdi, [字符串地址]   (x86-64 第一个参数)
  │       │   ├── 生成: call printf@plt         (调用 printf)
  │       │   └── 生成: (结果在 eax 中, 但被丢弃)
  │       │
  │       └── return 0;
  │           ├── 将 0 加载到 eax (返回值寄存器)
  │           └── 生成: ret
  │
  └── 解析完毕, 退出
```

具体的机器码生成过程（以 x86-64 为例）：

```c
// x86_64-gen.c 中的关键函数（概念性描述）

// 将立即数加载到寄存器
static void load(int r, SValue *v) {
    if (v->r == VT_CONST) {
        // mov $imm, %reg
        // 或 lea addr(%rip), %reg (对于全局变量)
    }
}

// 函数调用
static void gfunc_call(int nb_args) {
    // 按照 System V AMD64 ABI:
    // 参数依次放入 rdi, rsi, rdx, rcx, r8, r9 (整数参数)
    // 浮点参数放入 xmm0-xmm7
    // 多余参数压栈
    // 调用目标地址
}
```

### 1.7.7 第六步：输出 ELF 文件

`tccelf.c` 的 `tcc_output_file()` 将所有编译结果输出为 ELF 可执行文件：

```
tcc_output_file(s, "hello")
  │
  ├── 1. 解析所有未解析的符号
  │   查找 printf 等外部符号是否在链接的库中
  │
  ├── 2. 处理重定位
  │   将 .text 中的 call 指令目标地址从符号引用
  │   转换为 PLT 条目地址
  │
  ├── 3. 生成 ELF 头
  │   e_ident, e_type (ET_EXEC), e_machine (EM_X86_64), ...
  │
  ├── 4. 生成程序头表 (Program Header Table)
  │   描述内存段的加载方式:
  │   PT_LOAD (text, 只读+可执行)
  │   PT_LOAD (data, 可读+可写)
  │   PT_DYNAMIC (动态链接信息)
  │   PT_INTERP (/lib64/ld-linux-x86-64.so.2)
  │
  ├── 5. 写入各节内容
  │   .text, .rodata, .data, .bss
  │   .symtab, .strtab, .shstrtab
  │   .rela.text, .plt, .got, .got.plt
  │   .dynamic, .dynsym, .dynstr
  │
  └── 6. 生成节头表 (Section Header Table)
```

### 1.7.8 完整流程总结

```
tcc hello.c -o hello
    │
    ├── main() [tcc.c]
    │   ├── tcc_new()                 创建编译器状态
    │   ├── tcc_parse_args()          解析 "-o hello" 等参数
    │   └── tcc_add_file("hello.c")   开始编译
    │
    ├── tcc_add_file_internal() [libtcc.c]
    │   ├── 识别为 C 源文件
    │   └── tcc_compile()
    │
    ├── preprocess_start() [tccpp.c]
    │   ├── 初始化内置宏 (__TINYC__ 等)
    │   └── 打开 hello.c
    │
    ├── tccgen_compile() [tccgen.c]
    │   ├── next() -> #include <stdio.h>
    │   │   └── 预处理器: 加载 stdio.h, 注册宏和声明
    │   ├── decl(VT_CONST)
    │   │   └── 遇到 int main(void)
    │   │       ├── sym_push() 注册 main 符号
    │   │       └── block() 解析函数体
    │   │           ├── printf("Hello, TinyCC!\n");
    │   │           │   ├── 字符串放入 .rodata
    │   │           │   └── 生成 call printf@plt
    │   │           └── return 0;
    │   │               └── 生成 mov $0, %eax; ret
    │   └── gen_inline_functions() 编译延迟的内联函数
    │
    ├── tcc_output_file("hello") [tccelf.c]
    │   ├── 符号解析和重定位处理
    │   ├── 生成 ELF 头、程序头表
    │   ├── 写入 .text, .rodata, .plt, .got 等
    │   └── 生成节头表
    │
    └── tcc_delete() 清理所有内存
```

整个过程在一台现代计算机上耗时通常不到 10 毫秒。

---

## 1.8 构建和测试 tcc

### 1.8.1 获取源码

TinyCC 源码可以通过多种方式获取：

```bash
# 从 Git 仓库克隆
git clone https://repo.or.cz/tinycc.git

# 或者下载发布版本的 tarball
wget https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.28.tar.bz2
tar xjf tcc-0.9.28.tar.bz2
```

### 1.8.2 配置

```bash
cd tinycc
./configure
```

`configure` 脚本检测系统的编译器、头文件位置、库路径等，生成 `config.mak` 文件。常用选项：

```bash
# 查看所有配置选项
./configure --help

# 常用选项
./configure --prefix=/usr/local        # 安装路径 (默认)
./configure --cc=gcc                   # 用于编译 tcc 的编译器
./configure --enable-cross             # 构建交叉编译器
./configure --targetarm-...            # 设置交叉编译目标
./configure --extra-cflags="-O2"       # 额外的编译选项
./configure --disable-static           # 构建动态库版 libtcc
```

`configure` 会生成 `config.mak`，其中包含：

```makefile
# config.mak 示例 (x86-64 Linux)
CC=gcc
AR=ar
CONFIG_TCCDIR="/usr/local/lib/tcc"
CONFIG_SYSROOT=""
CONFIG_TCC_CRTPREFIX="/usr/lib"
CONFIG_TCC_ELFINTERP="/lib64/ld-linux-x86-64.so.2"
# ...
```

### 1.8.3 编译

```bash
make
```

这个命令执行以下操作：

1. **编译 libtcc.a**：将 `libtcc.c`（以及它 include 的 `tccpp.c`、`tccgen.c` 等）编译为静态库。
2. **编译 tcc**：将 `tcc.c` 链接 `libtcc.a` 生成 `tcc` 可执行文件。
3. **编译 libtcc1.a**：将 `lib/` 中的运行时支持代码编译为目标文件并打包为静态库。

在 ONE_SOURCE 模式（默认）下，`libtcc.c` 通过 `#include` 将所有核心 `.c` 文件组合为一个编译单元，这简化了构建过程并允许编译器进行更好的优化（虽然 tcc 本身不做优化）。

### 1.8.4 测试

```bash
make test
```

这会运行 `tests/` 目录下的测试套件。主要测试包括：

- **tcctest.c**：覆盖面广泛的 C 语言特性测试，包括边界条件和微妙的行为。
- **libtcc_test.c**：测试 libtcc API 的正确性。
- **boundtest.c**：边界检查功能测试。
- **abitest.c**：ABI（应用程序二进制接口）兼容性测试。
- **asmtest.S**：汇编器测试。

测试通过后，输出类似：

```
total: 8 passed, 0 failed
```

### 1.8.5 安装

```bash
make install
```

这会将以下文件安装到系统中：

```
/usr/local/bin/tcc              # 编译器可执行文件
/usr/local/lib/tcc/             # tcc 运行时目录
/usr/local/lib/tcc/libtcc1.a    # 运行时支持库
/usr/local/lib/tcc/include/     # 自带头文件
/usr/local/lib/libtcc.a         # libtcc 静态库
/usr/local/include/libtcc.h     # libtcc 公共头文件
/usr/local/man/man1/tcc.1       # 手册页
```

### 1.8.6 验证安装

```bash
# 检查版本
tcc -v

# 编译并运行一个简单程序
echo '#include <stdio.h>
int main() { printf("hello\n"); return 0; }' > /tmp/hello.c
tcc -run /tmp/hello.c

# 或者编译为可执行文件
tcc /tmp/hello.c -o /tmp/hello
/tmp/hello
```

### 1.8.7 用 tcc 编译 tcc（自举测试）

自举（bootstrapping）是编译器正确性的终极测试。TinyCC 可以编译自身：

```bash
# 用系统编译器 (gcc) 先构建 tcc
make clean
./configure
make

# 用刚构建的 tcc 重新编译自身
./tcc -o tcc2 tcc.c

# 验证新的 tcc 能正常工作
./tcc2 -run tests/ex1.c
```

如果 `tcc2` 能正确编译和运行程序，说明 tcc 生成的代码是正确的——它能够正确地编译一个与自身功能等价的编译器。

---

## 1.9 本章小结与练习

### 1.9.1 本章小结

本章介绍了编译器的基本概念和 TinyCC 的总体架构。核心要点：

1. **编译器**是将高级语言翻译为低级代码的程序，与解释器（逐行执行）和 JIT 编译器（运行时编译）有本质区别。

2. **编译的经典阶段**包括词法分析、语法分析、语义分析、中间代码生成、优化和目标代码生成。不同编译器的阶段划分和组织方式差异很大。

3. **TinyCC** 由 Fabrice Bellard 于 2001 年创建，以极小的代码量（约 7 万行）、极快的编译速度（10 倍于 gcc -O0）和 ANSI C99 合规性为目标。当前版本 0.9.28 支持 i386、x86_64、ARM、AArch64、RISC-V 64 等架构。

4. **单遍编译**是 TinyCC 最重要的架构特征：在一次扫描源码的过程中，边解析边生成代码，不构建 AST，不做优化。这带来了极致的编译速度，代价是代码质量不如多遍编译器。

5. **核心数据结构**包括 `TCCState`（编译器全局状态）、`Sym`（符号表条目）、`CType`（类型编码）、`SValue`（虚拟栈元素）和 `Section`（ELF 节）。它们是理解后续章节的基础。

6. **源码文件地图**展示了每个 `.c` 和 `.h` 文件的职责，为后续深入阅读提供导航。

7. **构建和测试**只需 `./configure && make && make test && make install` 四个命令。tcc 支持自举（编译自身）。

### 1.9.2 练习

**练习 1：构建与验证**（基础）

从源码构建 TinyCC，运行测试套件，并使用 tcc 编译运行 `examples/hello.c`。记录编译耗时并与 GCC 对比。

（详见 `exercises/ex1_build.md`）

**练习 2：编译阶段追踪**（进阶）

使用 tcc 的 `-E`、`-S`、`-c` 选项，分别查看一个 C 程序在预处理、汇编、目标文件阶段的输出。分析每个阶段输出的内容和含义。

（详见 `exercises/ex2_trace.md`）

**练习 3：修改 tcc 源码**（高阶）

修改 `tcc.c` 中的 `main()` 函数，在编译开始时输出一条自定义消息。重新构建 tcc 并验证修改生效。

（详见 `exercises/ex3_modify.md`）

### 1.9.3 推荐阅读

1. Aho, A. V., Lam, M. S., Sethi, R., & Ullman, J. D. (2006). *Compilers: Principles, Techniques, and Tools* (2nd ed.). Pearson.（"龙书"，编译器理论的经典教材。）
2. Bellard, F. (2002). "TCC: Tiny C Compiler." https://bellard.org/tcc/
3. TinyCC 官方文档：源码中的 `tcc-doc.texi`。
4. Fabrice Bellard 的个人主页：https://bellard.org/ （包含 QEMU、FFmpeg 等项目的链接。）
