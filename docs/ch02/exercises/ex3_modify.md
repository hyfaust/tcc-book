# 练习 2.3：为 tcc 添加新关键字

## 目标

通过修改 tcc 源码，为 C 语言添加一个新的关键字。这个练习将帮助你理解：

1. `tcctok.h` 中 `DEF` 宏的工作机制
2. 关键字 token 的分配流程
3. 预处理器和解析器如何识别关键字
4. token 编号系统的设计

## 背景

tcc 的关键字系统基于 `tcctok.h` 文件中的 `DEF(id, str)` 宏。每个 `DEF` 条目自动获得一个从 `TOK_IDENT`（256）开始递增的 token 编号。关键字和普通标识符共享同一个编号空间，区分它们的方法是检查编号是否在 `[TOK_IDENT, TOK_UIDENT)` 范围内。

`tccpp_new()` 函数在初始化时遍历 `tcc_keywords` 字符串表，将每个关键字注册到哈希表中。当词法分析器扫描到一个标识符时，它在哈希表中查找，如果匹配到一个 `TokenSym` 条目，就使用该条目的 token 编号——这个编号可能是关键字的编号，也可能是之前注册的标识符的编号。

## 任务

为 tcc 添加一个新的关键字 `__assert`，它类似于 `static_assert` 但用于运行时断言。

### 步骤 1：在 tcctok.h 中添加关键字定义

在 `tcctok.h` 的关键字部分（C 语言控制流关键字之后，预处理器关键字之前）添加：

```c
DEF(TOK_ASSERT, "__assert")
```

**注意**：添加的位置很重要！它必须在 `TOK_ASM3` 之后、`TOK_EXTERN` 之前（或者在其他合适的位置），因为 token 编号是按顺序分配的。

> **警告**：添加关键字会改变后续所有关键字的 token 编号。这意味着如果你有依赖特定编号的代码（硬编码的数字常量），它们会出错。在实际工程中，这是一个重要的兼容性考虑。

### 步骤 2：验证关键字注册

编译 tcc 后，使用 `tcc -E` 预处理以下代码来验证关键字是否被识别：

```c
__assert(sizeof(int) == 4, "int must be 4 bytes");
```

如果关键字被正确识别，它应该在预处理输出中保持原样（因为 `__assert` 不是预处理器指令）。

### 步骤 3：在解析器中处理新关键字（可选，进阶）

在 `tccgen.c` 的解析器中添加对 `__assert` 的处理。例如：

```c
case TOK_ASSERT:
    next();  /* 跳过 __assert */
    skip('(');
    {
        /* 解析断言表达式 */
        int saved_expr_type = parse_flags;
        /* ... 表达式解析 ... */
    }
    skip(',');
    /* 解析消息字符串 */
    /* ... */
    skip(')');
    break;
```

### 步骤 4：编写测试代码

```c
int main(void)
{
    __assert(sizeof(int) >= 4, "int must be at least 4 bytes");
    return 0;
}
```

## 验证清单

- [ ] `tcctok.h` 中添加了 `DEF(TOK_ASSERT, "__assert")`
- [ ] tcc 编译成功
- [ ] `tcc -E test.c` 输出中 `__assert` 被识别为关键字
- [ ] （可选）解析器正确处理 `__assert` 语句

## 思考题

1. 如果你想添加一个**上下文相关关键字**（只在特定上下文中是关键字，其他时候是普通标识符），应该如何设计？（提示：查看 `__attribute__` 的处理方式。）

2. 为什么 tcc 使用枚举（`enum tcc_token`）而不是 `#define` 来定义关键字编号？枚举方式有什么优缺点？

3. 如果你在 `tcctok.h` 的**末尾**（汇编指令之后）添加关键字，而不是在中间添加，会有什么不同？这对兼容性有什么影响？

4. 考虑以下问题：如果用户代码中已经有一个名为 `__assert` 的宏定义（`#define __assert(...)`），你的新关键字会如何与它交互？提示：思考 `next()` 函数中宏展开和关键字识别的先后顺序。

## 提示

### 关键源文件

| 文件 | 作用 |
|------|------|
| `tcctok.h` | 关键字和标识符定义 |
| `tccpp.c` | 词法分析器（`next_nomacro()`、`next()`、`tccpp_new()`） |
| `tcc.h` | Token 编号定义、数据结构 |
| `tccgen.c` | 语法解析器（处理关键字的语义） |

### 调试技巧

1. 在 `next_nomacro()` 函数末尾添加调试输出：
   ```c
   printf("token = %d (%s)\n", tok, get_tok_str(tok, &tokc));
   ```

2. 使用 `tcc -E -P` 查看预处理输出，验证关键字是否被正确识别。

3. 使用 `get_tok_str()` 函数将 token 编号转换为可读字符串。

### 完整的关键字添加示例

以下是一个完整的示例，展示如何在 `tcctok.h` 的 C 关键字区域末尾添加新关键字：

```tcctok.h
/* ... 现有的关键字 ... */
DEF(TOK_ASM1, "asm")
DEF(TOK_ASM2, "__asm")
DEF(TOK_ASM3, "__asm__")

/* 新增关键字 */
DEF(TOK_ASSERT, "__assert")

DEF(TOK_EXTERN, "extern")
/* ... */
```

添加后，`TOK_ASSERT` 将自动获得一个递增的编号（例如，如果 `TOK_ASM3` 是 270，则 `TOK_ASSERT` 是 271）。后续的 `TOK_EXTERN` 及其后的所有关键字编号都会相应后移。

## 延伸阅读

- GCC 关键字扩展文档：https://gcc.gnu.org/onlinedocs/gcc/Keyword-Index.html
- C11 标准中的 `_Static_assert`：ISO/IEC 9899:2011, 6.7.10
- tcc 源码中 `parse_define()` 函数（`tccpp.c`）展示了 `#define` 的完整解析流程
