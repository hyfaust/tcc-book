# 第3章 预处理器

预处理器是 C 编译器中最古老、最"原始"的子系统之一。它工作在词法分析器之上，对源文本进行词法级的变换——宏展开、文件包含、条件编译——然后将变换后的 token 流交给真正的语法分析器。TinyCC 的预处理器实现在 `tccpp.c` 中，约 4000 行 C 代码，是整个编译器中体量最大的单个文件。本章将逐函数、逐数据结构地拆解这一实现。

---

## 3.1 预处理器的理论与标准

### 3.1.1 C 标准的翻译阶段

ISO/IEC 9899（C11 §5.1.1.2）规定了源文件从字符序列到可执行程序的八个翻译阶段（translation phases）。预处理器负责其中前四个阶段：

| 阶段 | 说明 | TinyCC 实现位置 |
|------|------|-----------------|
| 1 | 物理源文件字符映射：将源文件字符集映射到基本源字符集。处理三字符组（trigraph）。 | `tcc_open()` / `handle_bs()` |
| 2 | 行接续：将反斜杠+换行（`\` `\n`）合并为逻辑行。 | `handle_bs()` 在 `tccpp.c` 中 |
| 3 | 词法分析：将字符流分解为预处理 token（pp-token）。 | `next_nomacro()`、`parse_number()`、`parse_string()` |
| 4 | 预处理指令执行：展开宏、处理 `#include`、`#if` 等。 | `preprocess()`、`macro_subst()` |
| 5 | 字符字面量与字符串字面量的字符集转换。 | `parse_escape_string()` |
| 6 | 相邻字符串字面量拼接。 | `parse_string()` |
| 7 | 语法分析与语义分析。 | `tccgen.c` |
| 8 | 链接。 | `tccelf.c`、`tcclink.c` |

TinyCC 的关键设计选择是**将阶段 1-6 全部集成在词法分析器 `tccpp.c` 中**，而不是像 GCC/Clang 那样分成独立的 libcpp 模块。这体现了 TinyCC "单一文件、单一职责"的极简哲学。

### 3.1.2 预处理 token vs 语法 token

C 标准区分两类 token：

- **预处理 token（pp-token）**：词法分析阶段 3 的产物，包括 `pp-number`、`pp-string` 等尚未完全解析的形式。
- **语法 token**：阶段 7 使用的最终 token，如整数常量 `TOK_CINT`、浮点常量 `TOK_CFLOAT`。

在 TinyCC 中，这一区分体现在 `TOK_PPNUM`（预处理数字）与 `TOK_CINT`/`TOK_CFLOAT`（已解析的数值常量）之间。预处理器内部只处理 pp-token，只有当 token 流交给语法分析器时才通过 `next()` → `convert` 标签进行转换：

```c
/* next() 末尾的转换逻辑 */
convert:
    if (t == TOK_PPNUM) {
        if (parse_flags & PARSE_FLAG_TOK_NUM)
            parse_number(tokc.str.data);
    } else if (t == TOK_PPSTR) {
        if (parse_flags & PARSE_FLAG_TOK_STR)
            parse_string(tokc.str.data, tokc.str.size - 1);
    }
```

### 3.1.3 TinyCC 预处理器的整体架构

TinyCC 预处理器的核心数据流如下：

```
源文件字符流
    │
    ▼
next_nomacro()  ── 词法分析，产生单个 pp-token
    │
    ▼
preprocess()    ── 分发 # 指令（#define, #include, #if 等）
    │
    ▼
next()          ── 宏展开，产生最终 token 流
    │
    ▼
tcc_preprocess() ── -E 模式输出
    │
    ▼
tccgen.c        ── 语法分析与代码生成
```

`next_nomacro()` 是"不展开宏"的词法分析器；`next()` 是"带宏展开"的词法分析器。预处理器指令通过 `preprocess()` 分发，宏展开通过 `macro_subst()` 递归完成。

---

## 3.2 预处理指令入口 preprocess()

`preprocess()` 函数（`tccpp.c:1792`）是所有 `#` 指令的总入口。每当词法分析器在行首遇到 `#` 时，就调用此函数。它是一个大型的 `switch` 分发表：

```c
ST_FUNC void preprocess(int is_bof)
{
    TCCState *s1 = tcc_state;
    int c, n, saved_parse_flags;
    char buf[1024], *q;
    Sym *s;

    saved_parse_flags = parse_flags;
    parse_flags = PARSE_FLAG_PREPROCESS
        | PARSE_FLAG_TOK_NUM
        | PARSE_FLAG_TOK_STR
        | PARSE_FLAG_LINEFEED
        | (parse_flags & PARSE_FLAG_ASM_FILE);

    next_nomacro();
redo:
    switch(tok) {
    case TOK_DEFINE:    /* #define */
    case TOK_UNDEF:     /* #undef */
    case TOK_INCLUDE:   /* #include */
    case TOK_INCLUDE_NEXT: /* #include_next */
    case TOK_IFNDEF:    /* #ifndef */
    case TOK_IF:        /* #if */
    case TOK_IFDEF:     /* #ifdef */
    case TOK_ELSE:      /* #else */
    case TOK_ELIF:      /* #elif */
    case TOK_ENDIF:     /* #endif */
    case TOK_LINE:      /* #line */
    case TOK_ERROR:     /* #error */
    case TOK_WARNING:   /* #warning */
    case TOK_PRAGMA:    /* #pragma */
    ...
    }
}
```

### 3.2.1 parse_flags 的作用

进入 `preprocess()` 时，函数会设置一组 `parse_flags` 标志位，控制后续词法分析的行为：

- `PARSE_FLAG_PREPROCESS`：表示当前处于预处理模式，标识符不做宏展开（由 `preprocess()` 自己控制）。
- `PARSE_FLAG_TOK_NUM`：允许将 pp-number 转换为数值常量。
- `PARSE_FLAG_TOK_STR`：允许解析字符串转义序列。
- `PARSE_FLAG_LINEFEED`：保留换行 token（`TOK_LINEFEED`），用于检测指令结束。
- `PARSE_FLAG_ASM_FILE`：汇编模式，保留 `.` 作为标识符字符。

指令处理完毕后，`parse_flags` 恢复为调用前的值，确保不影响后续的正常词法分析。

### 3.2.2 指令分发表详解

| 指令 | Token | 处理函数 | 说明 |
|------|-------|----------|------|
| `#define` | `TOK_DEFINE` | `parse_define()` | 定义宏 |
| `#undef` | `TOK_UNDEF` | `define_undef()` | 取消宏定义 |
| `#include` | `TOK_INCLUDE` | `parse_include()` | 包含头文件 |
| `#include_next` | `TOK_INCLUDE_NEXT` | `parse_include()` | 从下一个搜索路径包含 |
| `#ifdef` | `TOK_IFDEF` | `define_find()` | 测试宏是否已定义 |
| `#ifndef` | `TOK_IFNDEF` | `define_find()` | 测试宏是否未定义 |
| `#if` | `TOK_IF` | `expr_preprocess()` | 条件表达式求值 |
| `#elif` | `TOK_ELIF` | `expr_preprocess()` | else-if 分支 |
| `#else` | `TOK_ELSE` | — | else 分支 |
| `#endif` | `TOK_ENDIF` | — | 结束条件编译块 |
| `#line` | `TOK_LINE` | — | 设置行号和文件名 |
| `#error` | `TOK_ERROR` | — | 输出错误信息并终止 |
| `#warning` | `TOK_WARNING` | — | 输出警告信息 |
| `#pragma` | `TOK_PRAGMA` | `pragma_parse()` | 编译器特定指令 |

### 3.2.3 `is_bof` 参数

`preprocess()` 接收一个 `is_bof`（begin of file）参数。这个参数只有一个用途：当 `#ifndef` 出现在文件的第一行时，TinyCC 会记录这个宏名（`file->ifndef_macro`），用于 `CachedInclude` 优化（详见 3.9 节）。这是 GCC 等编译器广泛采用的 include guard 优化策略。

---

## 3.3 宏定义 #define

### 3.3.1 parse_define() 总体流程

`parse_define()`（`tccpp.c:1519`）负责解析 `#define` 之后的内容。其工作可分为三个阶段：

1. **解析宏名**：读取下一个 token 作为宏名（必须是标识符，不能是 `defined`）。
2. **解析参数列表**：如果宏名后紧跟 `(`，则解析为函数宏；否则为对象宏。
3. **收集宏体**：将行内剩余 token 收集到 `TokenString` 中，直到行尾。

```c
ST_FUNC void parse_define(void)
{
    Sym *s, *first, **ps;
    int v, t, varg, is_vaargs, t0;
    int saved_parse_flags = parse_flags;
    TokenString str;

    v = tok;                          /* 宏名 */
    if (v < TOK_IDENT || v == TOK_DEFINED)
        tcc_error("invalid macro name '%s'", get_tok_str(tok, &tokc));
    first = NULL;
    t = MACRO_OBJ;                    /* 默认为对象宏 */

    parse_flags = ((parse_flags & ~PARSE_FLAG_ASM_FILE)
                    | PARSE_FLAG_SPACES);
    next_nomacro();
    parse_flags &= ~PARSE_FLAG_SPACES;
    is_vaargs = 0;

    if (tok == '(') {                 /* 函数宏 */
        ...
    }

    /* 收集宏体 */
    ...
    define_push(v, t, str.str, first);
}
```

### 3.3.2 对象宏与函数宏的区分

对象宏（object-like macro）和函数宏（function-like macro）的区分依据是：宏名之后的下一个非空白 token 是否为 `(`。注意 `(` 必须**紧随**宏名，中间不能有空白。这是 C 标准的明确要求。

TinyCC 通过先调用 `next_nomacro()`（在设置了 `PARSE_FLAG_SPACES` 的状态下）读取下一个 token 来实现这一检查：

```c
parse_flags = ((parse_flags & ~PARSE_FLAG_ASM_FILE) | PARSE_FLAG_SPACES);
next_nomacro();
parse_flags &= ~PARSE_FLAG_SPACES;

if (tok == '(') {
    /* 函数宏：解析参数列表 */
    ...
    t = MACRO_FUNC;
}
```

`PARSE_FLAG_SPACES` 的作用是让空白字符也被返回为 token，从而确保 `(` 确实紧随宏名。如果中间有空白，`tok` 会先被读为空格 token 而不是 `(`。

### 3.3.3 函数宏参数解析

函数宏的参数解析在一个循环中完成：

```c
if (tok == '(') {
    int dotid = set_idnum('.', 0);   /* 汇编模式下 '.' 不作为 ID 字符 */
    next_nomacro();
    ps = &first;
    if (tok != ')') for (;;) {
        varg = tok;
        next_nomacro();
        is_vaargs = 0;
        if (varg == TOK_DOTS) {       /* C23: #define f(...) */
            varg = TOK___VA_ARGS__;
            is_vaargs = 1;
        } else if (tok == TOK_DOTS && gnu_ext) {  /* GNU: #define f(a, ...) */
            is_vaargs = 1;
            next_nomacro();
        }
        if (varg < TOK_IDENT)
            tcc_error("bad macro parameter list");
        s = sym_push2(&define_stack, varg | SYM_FIELD, is_vaargs, 0);
        *ps = s;
        ps = &s->next;
        if (tok == ')') break;
        if (tok != ',' || is_vaargs)
            tcc_error("bad macro parameter list");
        next_nomacro();
    }
    ...
    t = MACRO_FUNC;
    set_idnum('.', dotid);
}
```

参数存储为 `Sym` 链表，每个参数的 `v` 字段是参数名的 token ID，`type.t` 字段标记是否为可变参数（`is_vaargs`）。

### 3.3.4 可变参数宏 __VA_ARGS__

C99 引入了可变参数宏（variadic macros），语法为 `#define f(a, b, ...)`，其中 `...` 对应 `__VA_ARGS__`。TinyCC 的支持体现在两个地方：

1. **解析阶段**：`TOK_DOTS`（`...`）被识别并映射为 `TOK___VA_ARGS__`。
2. **展开阶段**：在 `macro_subst_tok()` 中，可变参数匹配所有剩余实参（逗号后的所有内容）；在 `macro_arg_subst()` 中，`__VA_ARGS__` 的空值触发前导逗号删除。

### 3.3.5 宏体收集

宏体的收集使用 `TokenString`（一个动态增长的 int 数组）：

```c
parse_flags |= PARSE_FLAG_ACCEPT_STRAYS
             | PARSE_FLAG_SPACES
             | PARSE_FLAG_LINEFEED;
tok_str_new(&str);
t0 = 0;
while (tok != TOK_LINEFEED && tok != TOK_EOF) {
    if (is_space(tok)) {
        str.need_spc |= 1;
    } else {
        if (TOK_TWOSHARPS == tok) {
            if (0 == t0) goto bad_twosharp;
            tok = TOK_PPJOIN;
            t |= MACRO_JOIN;
        }
        tok_str_add2_spc(&str, tok, &tokc);
        t0 = tok;
    }
    next_nomacro();
}
parse_flags = saved_parse_flags;
tok_str_add(&str, 0);    /* 以 0 结尾 */
if (t0 == TOK_PPJOIN)
    tcc_error("'##' cannot appear at either end of macro");
define_push(v, t, str.str, first);
```

关键细节：

- `TOK_TWOSHARPS`（`##`）在宏体中被替换为 `TOK_PPJOIN`，这是内部表示。
- 宏体中遇到 `##` 时，设置 `MACRO_JOIN` 标志到 `t` 中，标记此宏包含拼接操作。
- 宏体以 `0`（NULL terminator）结尾，以 `TOK_EOF` 标记参数引用的结束。
- `##` 不能出现在宏体的开头或结尾（`bad_twosharp` 错误）。

---

## 3.4 宏存储与查找

### 3.4.1 数据结构概览

TinyCC 的宏定义存储涉及三个核心数据结构：

```
table_ident[]          Sym (define_stack)       TokenString
┌──────────────┐       ┌──────────────┐       ┌──────────────┐
│ TokenSym[0]  │       │ v = TOK_ID   │       │ int *str     │
│  sym_define ─┼──┐    │ type.t = ... │       │ len          │
│              │  │    │ d ───────────┼───▶   │ [token list] │
├──────────────┤  │    │ next ────────┼──▶    │ 0 (end)      │
│ TokenSym[1]  │  │    │ (first arg)  │       └──────────────┘
│  sym_define  │  │    └──────────────┘
│              │  │
├──────────────┤  │
│     ...      │  │
└──────────────┘  │
                  │
    define_find(v)┘
```

- **`table_ident[]`**：标识符表，每个 `TokenSym` 有一个 `sym_define` 指针，指向当前生效的宏定义。
- **`define_stack`**：全局链表，按定义顺序存储所有宏定义 `Sym`。支持嵌套作用域（如 `#pragma push_macro`）。
- **`Sym.d`**：指向 `TokenString`（即 `int *`），存储宏体的 token 序列。
- **`Sym.next`**：函数宏的参数链表。

### 3.4.2 define_push()

`define_push()`（`tccpp.c:1248`）将一个新宏定义压入定义栈：

```c
ST_INLN void define_push(int v, int macro_type, int *str, Sym *first_arg)
{
    Sym *s, *o;

    o = define_find(v);                          /* 查找旧定义 */
    s = sym_push2(&define_stack, v, macro_type, 0);  /* 压入新定义 */
    s->d = str;                                  /* 宏体 */
    s->next = first_arg;                         /* 参数链表 */
    table_ident[v - TOK_IDENT]->sym_define = s;  /* 更新查找表 */

    if (o && !macro_is_equal(o->d, s->d))
        tcc_warning("%s redefined", get_tok_str(v, NULL));  /* 警告重定义 */
}
```

注意 `sym_push2()` 使用 `define_stack` 作为栈顶，这意味着宏定义是按嵌套顺序存储的。当遇到 `#undef` 时，宏只是从查找表中移除，但 `define_stack` 链表中仍然保留记录（用于 `#pragma pop_macro` 恢复）。

### 3.4.3 define_find()

`define_find()`（`tccpp.c:1278`）是宏查找的核心函数，极其简洁：

```c
ST_INLN Sym *define_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_define;
}
```

它直接通过 `table_ident[]` 数组索引，O(1) 时间完成查找。`sym_define` 指针指向 `define_stack` 链表中的 `Sym` 节点。

### 3.4.4 define_undef()

`define_undef()`（`tccpp.c:1267`）取消宏定义：

```c
ST_FUNC void define_undef(Sym *s)
{
    int v = s->v;
    if (v >= TOK_IDENT && v < tok_ident)
        table_ident[v - TOK_IDENT]->sym_define = NULL;
}
```

它只是将 `table_ident[]` 中的 `sym_define` 指针置为 NULL，并不释放 `Sym` 节点。这是因为 `Sym` 节点仍在 `define_stack` 链表中，可能被 `#pragma pop_macro` 使用。

### 3.4.5 free_defines()

`free_defines()`（`tccpp.c:1283`）释放从当前栈顶到指定边界 `b` 之间的所有宏定义：

```c
ST_FUNC void free_defines(Sym *b)
{
    while (define_stack != b) {
        Sym *top = define_stack;
        define_stack = top->prev;
        tok_str_free_str(top->d);       /* 释放宏体 */
        define_undef(top);              /* 从查找表移除 */
        sym_free(top);                  /* 释放 Sym 节点 */
    }
}
```

这在文件包含结束时调用，用于清理该文件中定义的所有宏。

---

## 3.5 宏展开三阶段详解

宏展开是预处理器中最复杂的部分。TinyCC 将其分为三个阶段：

1. **`macro_subst_tok()`**：函数宏参数收集与初步替换。
2. **`macro_arg_subst()`**：参数替换、字符串化、拼接。
3. **`macro_subst()`**：递归展开，防止无限递归。

### 3.5.1 Stage 1: macro_subst_tok() — 函数宏参数收集

`macro_subst_tok()`（`tccpp.c:3237`）是宏展开的入口。当 `next()` 或 `macro_subst()` 遇到一个已定义的标识符时，调用此函数。

#### 核心流程

```c
static int macro_subst_tok(
    TokenString *tok_str,
    Sym **nested_list,
    Sym *s)
{
    int t;
    int v = s->v;

    if (s->d) {  /* 有宏体 */
        if (s->type.t & MACRO_FUNC) {
            /* 函数宏：需要收集参数 */
            ...
        }
        /* 对象宏或函数宏参数收集完毕后 */
        /* 处理 ## 拼接 */
        jstr = mstr;
        if (s->type.t & MACRO_JOIN)
            jstr = macro_twosharps(mstr);

        /* 递归展开 */
        sa = sym_push2(nested_list, v, 0, 0);
        ret = macro_subst(tok_str, nested_list, jstr);
        ...
    } else {
        /* 内置宏：__LINE__, __FILE__ 等 */
        ...
    }
}
```

#### 函数宏的 '(' 前瞻

函数宏展开的关键问题是：宏名后面是否紧跟 `(`。TinyCC 使用 `next_argstream()` 进行前瞻：

```c
t = next_argstream(nested_list, &str);
if (t != '(') {
    /* 不是函数调用，恢复原始 token */
    parse_flags = saved_parse_flags;
    tok_str_add2_spc(tok_str, v, 0);
    /* 恢复空白 */
    for (i = 0; i < str.len; i++)
        tok_str_add(tok_str, str.str[i]);
    return 0;
}
```

`next_argstream()` 会沿着宏栈向上查找，直到到达文件层的 `peek_file()`。如果下一个非空白 token 不是 `(`，则此次"展开"退化为普通标识符输出。

#### 参数收集的括号匹配

一旦确认是函数调用，进入参数收集循环：

```c
args = NULL;
sa = s->next;  /* 参数链表头 */
i = 2;         /* 跳过 '(' */
for(;;) {
    do {
        t = next_argstream(nested_list, NULL);
    } while (t == ' ' || --i);

    if (!sa) {
        if (t == ')') break;         /* f() 情况 */
        tcc_error("too many args");
    }
empty_arg:
    tok_str_new(&str);
    parlevel = 0;
    while (parlevel > 0 || (t != ')' && (t != ',' || sa->type.t))) {
        if (t == '(') parlevel++;
        if (t == ')') parlevel--;
        if (t == ' ')
            str.need_spc |= 1;
        else
            tok_str_add2_spc(&str, t, &tokc);
        t = next_argstream(nested_list, NULL);
    }
    tok_str_add(&str, TOK_EOF);
    sa1 = sym_push2(&args, sa->v & ~SYM_FIELD, sa->type.t, 0);
    sa1->d = str.str;
    sa = sa->next;
    if (t == ')') {
        if (!sa) break;
        if (sa->type.t && gnu_ext) goto empty_arg;  /* GNU 扩展：空可变参数 */
        tcc_error("too few args");
    }
    i = 1;
}
```

关键设计点：

- `parlevel` 跟踪括号嵌套深度，确保 `f(a, (b, c))` 正确解析为两个参数。
- `sa->type.t` 非零表示可变参数，逗号不再作为分隔符。
- GNU 扩展允许省略可变参数：`f(a)` 中 `__VA_ARGS__` 为空。

### 3.5.2 Stage 2: macro_arg_subst() — 参数替换

`macro_arg_subst()`（`tccpp.c:2986`）将宏体中的形参替换为实参值。这是 `#` 字符串化和 `##` 前后标记处理的核心场所。

#### 参数替换主循环

```c
static int *macro_arg_subst(Sym **nested_list, const int *macro_str, Sym *args)
{
    TokenString str;
    tok_str_new(&str);
    t0 = t1 = 0;
    while(1) {
        TOK_GET(&t, &macro_str, &cval);
        if (!t) break;

        if (t == '#') {
            /* 字符串化操作 */
            ...
        } else if (t >= TOK_IDENT) {
            s = sym_find2(args, t);
            if (s) {
                /* 检查前后是否有 ## */
                if (t2 == TOK_PPJOIN || t1 == TOK_PPJOIN) {
                    /* 不展开，直接插入原始 token */
                    ...
                } else {
                    /* 正常替换：先展开参数，再插入 */
                    macro_subst(&str2, nested_list, st);
                    ...
                }
            }
        }
    }
}
```

#### # 字符串化

当宏体中出现 `#` 后跟参数名时，执行字符串化操作：

```c
if (t == '#') {
    do t = *macro_str++; while (t == ' ');
    s = sym_find2(args, t);
    if (s) {
        cstr_reset(&tokcstr);
        cstr_ccat(&tokcstr, '\"');
        st = s->d;
        while (*st != TOK_EOF) {
            TOK_GET(&t, &st, &cval);
            /* 将每个 token 的文本拼接 */
            ...
        }
        cstr_ccat(&tokcstr, '\"');
        cstr_ccat(&tokcstr, '\0');
        /* 生成 TOK_PPSTR token */
        tok_str_add2(&str, TOK_PPSTR, &cval);
    }
}
```

字符串化的规则（C11 §6.10.3.2）：
- 参数的每个 token 之间用单个空格分隔。
- 字符串字面量中的 `"` 和 `\` 需要转义。
- 前导和尾随空白被删除。

#### ## 与空 __VA_ARGS__ 的逗号删除

当 `##` 出现在宏体中且 `__VA_ARGS__` 为空时，GNU 扩展会删除前导逗号：

```c
if (t1 == TOK_PPJOIN && t0 == ',' && gnu_ext && s->type.t) {
    int c = str.str[str.len - 1];
    while (str.str[--str.len] != ',')
        ;
    if (*st == TOK_EOF) {
        /* __VA_ARGS__ 为空：删除 ',' 和 '##' */
    } else {
        /* __VA_ARGS__ 非空：删除 '##'，保留变量 */
        str.len++;
        goto add_var;
    }
}
```

例如 `#define dbg(fmt, ...) printf(fmt, ##__VA_ARGS__)` 中，当 `__VA_ARGS__` 为空时，`##` 前的逗号被删除。

### 3.5.3 Stage 3: macro_subst() — 递归展开

`macro_subst()`（`tccpp.c:3408`）是宏展开的最外层循环，负责递归展开并防止无限递归。

```c
static int macro_subst(
    TokenString *tok_str,
    Sym **nested_list,
    const int *macro_str)
{
    Sym *s;
    int t, nosubst = 0;
    CValue cval;

    while (1) {
        TOK_GET(&t, &macro_str, &cval);
        if (t == 0 || t == TOK_EOF) break;

        if (t >= TOK_IDENT) {
            s = define_find(t);
            if (s == NULL || nosubst) goto no_subst;
            /* 嵌套检查：防止无限递归 */
            if (sym_find2(*nested_list, t)) {
                t |= SYM_FIELD;  /* 标记为不展开 */
                goto no_subst;
            }
            str = tok_str_alloc();
            str->str = (int*)macro_str;
            begin_macro(str, 2);
            nosubst = macro_subst_tok(tok_str, nested_list, s);
            ...
        } else {
no_subst:
            tok_str_add2_spc(tok_str, t, &cval);
            if (nosubst && t != '(')
                nosubst = 0;
            if (t == TOK_DEFINED && pp_expr)
                nosubst = 1;
        }
    }
}
```

#### nested_list 自递归防止

`nested_list` 是一个 `Sym` 链表，记录当前展开路径中已经进入过的宏。当 `macro_subst_tok()` 开始展开宏 `X` 时，它将 `X` 压入 `nested_list`：

```c
sa = sym_push2(nested_list, v, 0, 0);
ret = macro_subst(tok_str, nested_list, jstr);
if (sa == *nested_list)
    *nested_list = sa->prev, sym_free(sa);
```

后续在 `macro_subst()` 中遇到 `X` 时，`sym_find2(*nested_list, t)` 返回非 NULL，从而跳过展开。展开完成后，`X` 从 `nested_list` 中弹出。

这确保了 C 标准要求的行为：**宏在其自身的展开过程中不被再次展开**，但在后续的展开中可以被展开。

#### nosubst 标志

`nosubst` 标志用于处理 `#defined` 操作符：在 `#if defined(X)` 中，`X` 不应被展开。当 `macro_subst()` 遇到 `TOK_DEFINED` 且处于 `pp_expr`（预处理表达式）模式时，设置 `nosubst = 1`，使后续标识符不做宏展开。

---

## 3.6 Token 拼接 ##

### 3.6.1 macro_twosharps() 的设计

`macro_twosharps()`（`tccpp.c:3106`）处理宏体中所有 `##` 操作符。它的输入是经过 `macro_arg_subst()` 替换后的 token 序列，输出是拼接后的新 token 序列。

```c
static inline int *macro_twosharps(const int *ptr0)
{
    int t1, t2, n, l;
    CValue cv1, cv2;
    TokenString macro_str1;
    const int *ptr;

    tok_str_new(&macro_str1);
    cstr_reset(&tokcstr);
    for (ptr = ptr0;;) {
        TOK_GET(&t1, &ptr, &cv1);
        if (t1 == 0) break;

        for (;;) {
            n = 0;
            while ((t2 = ptr[n]) == ' ')
                ++n;
            if (t2 != TOK_PPJOIN) break;
            ptr += n;
            while ((t2 = *++ptr) == ' ' || t2 == TOK_PPJOIN)
                ;
            TOK_GET(&t2, &ptr, &cv2);
            if (t2 == TOK_PLCHLDR) continue;
            if (t1 != TOK_PLCHLDR) {
                cstr_cat(&tokcstr, get_tok_str(t1, &cv1), -1);
                t1 = TOK_PLCHLDR;
            }
            cstr_cat(&tokcstr, get_tok_str(t2, &cv2), -1);
        }
        if (tokcstr.size) {
            /* 拼接结果需要重新词法分析 */
            cstr_ccat(&tokcstr, 0);
            tcc_open_bf(tcc_state, ":paste:", tokcstr.size);
            memcpy(file->buffer, tokcstr.data, tokcstr.size);
            tok_flags = 0;
            for (n = 0;; n = l) {
                next_nomacro();
                tok_str_add2(&macro_str1, tok, &tokc);
                if (*file->buf_ptr == 0) break;
                tok_str_add(&macro_str1, ' ');
                l = file->buf_ptr - file->buffer;
                tcc_warning("pasting ... does not give a valid token");
            }
            tcc_close();
            cstr_reset(&tokcstr);
        }
        if (t1 != TOK_PLCHLDR)
            tok_str_add2(&macro_str1, t1, &cv1);
    }
    tok_str_add(&macro_str1, 0);
    return macro_str1.str;
}
```

### 3.6.2 拼接的执行流程

1. **收集连续的 `##` 操作数**：将 `a ## b ## c` 中所有操作数的文本拼接到 `tokcstr` 中。
2. **创建临时文件 `:paste:`**：使用 `tcc_open_bf()` 创建一个虚拟的内存文件，内容为拼接后的字符串。
3. **重新词法分析**：调用 `next_nomacro()` 对拼接结果进行词法分析，生成新的 token。
4. **警告多 token 结果**：如果拼接后产生多个 token，输出警告（如 `pasting "i" and "n" does not give a valid preprocessing token`）。
5. **清理临时文件**：调用 `tcc_close()` 关闭虚拟文件。

### 3.6.3 TOK_PLCHLDR 占位符

`TOK_PLCHLDR`（placeholder）是 `##` 操作中的特殊标记。当参数被 `##` 包围且展开为空时，使用占位符代替：

```c
if (*st == TOK_EOF)
    tok_str_add(&str, TOK_PLCHLDR);
```

在 `macro_twosharps()` 中，占位符被跳过（`if (t2 == TOK_PLCHLDR) continue`），确保拼接只发生在非空操作数之间。

### 3.6.4 拼接示例

考虑 `#define CONCAT(a, b) a ## b` 展开 `CONCAT(hello, world)`：

1. 参数替换后宏体为：`hello ## world`。
2. `macro_twosharps()` 将 `"hello"` 和 `"world"` 拼接为 `"helloworld"`。
3. 创建临时文件 `:paste:`，内容为 `helloworld`。
4. `next_nomacro()` 将其词法分析为单个标识符 token `helloworld`。
5. 输出 `helloworld`。

---

## 3.7 字符串化 #

### 3.7.1 字符串化的实现

字符串化（stringification）在 `macro_arg_subst()` 中实现。当宏体中出现 `#` 后跟参数名时，该参数的值被转换为字符串字面量。

实现逻辑：

1. 检测到 `#` token。
2. 读取下一个 token（跳过空格），在参数链表中查找。
3. 遍历参数的 token 序列，将每个 token 的文本表示拼接。
4. 用双引号包裹，生成 `TOK_PPSTR` token。

```c
if (t == '#') {
    do t = *macro_str++; while (t == ' ');
    s = sym_find2(args, t);
    if (s) {
        cstr_reset(&tokcstr);
        cstr_ccat(&tokcstr, '\"');
        st = s->d;
        while (*st != TOK_EOF) {
            TOK_GET(&t, &st, &cval);
            s = get_tok_str(t, &cval);
            while (*s) {
                if (t == TOK_PPSTR && *s != '\'')
                    add_char(&tokcstr, *s);   /* 转义内部引号 */
                else
                    cstr_ccat(&tokcstr, *s);
                ++s;
            }
        }
        cstr_ccat(&tokcstr, '\"');
        cstr_ccat(&tokcstr, '\0');
        cval.str.size = tokcstr.size;
        cval.str.data = tokcstr.data;
        tok_str_add2(&str, TOK_PPSTR, &cval);
    }
}
```

### 3.7.2 字符串化规则

C 标准（C11 §6.10.3.2）规定的字符串化规则：

1. 参数中每个 pp-token 之间插入一个空格。
2. 字符串字面量中的 `\` 和 `"` 需要额外转义（`\\"` 和 `\\"`）。
3. 前导和尾随空白被删除。
4. 空参数展开为空字符串 `""`。

TinyCC 的实现中，`add_char()` 函数处理特殊字符的转义，`get_tok_str()` 返回 token 的标准文本表示。

### 3.7.3 字符串化示例

```c
#define STR(x) #x
STR(hello world)    /* 展开为 "hello world" */
STR("hello")        /* 展开为 "\"hello\"" */
STR()               /* 展开为 "" */
```

---

## 3.8 条件编译

### 3.8.1 条件编译指令族

TinyCC 支持完整的 C 标准条件编译指令族：

| 指令 | 功能 | 关键函数 |
|------|------|----------|
| `#ifdef MACRO` | 测试宏是否已定义 | `define_find()` |
| `#ifndef MACRO` | 测试宏是否未定义 | `define_find()` |
| `#if expr` | 常量表达式求值 | `expr_preprocess()` |
| `#elif expr` | else-if 分支 | `expr_preprocess()` |
| `#else` | else 分支 | — |
| `#endif` | 结束条件块 | — |

### 3.8.2 ifdef_stack

条件编译的嵌套状态通过 `ifdef_stack` 管理：

```c
/* TCCState 中的定义 */
int ifdef_stack[IFDEF_STACK_SIZE];    /* 最大 64 层嵌套 */
int *ifdef_stack_ptr;                  /* 当前栈顶指针 */
```

每个条件块在栈中占一个 int 值，编码如下：

- bit 0：当前条件是否为真（0 = 假，1 = 真）。
- bit 1：是否已经进入过某个分支（用于检测 `#else` 之后的重复 `#else`）。

### 3.8.3 #if 的处理

`#if` 指令的处理流程：

```c
case TOK_IF:
    c = expr_preprocess(s1);   /* 求值条件表达式 */
    goto do_if;

do_if:
    if (s1->ifdef_stack_ptr >= s1->ifdef_stack + IFDEF_STACK_SIZE)
        tcc_error("memory full (ifdef)");
    *s1->ifdef_stack_ptr++ = c;
    goto test_skip;

test_skip:
    if (!(c & 1)) {            /* 条件为假 */
        skip_to_eol(1);
        preprocess_skip();     /* 跳过整个 false 分支 */
        is_bof = 0;
        goto redo;             /* 重新处理 #else/#elif/#endif */
    }
    break;
```

### 3.8.4 expr_preprocess() 与 defined()

`expr_preprocess()`（`tccpp.c:1435`）负责将 `#if` 后的预处理表达式求值为整数常量。

```c
static int expr_preprocess(TCCState *s1)
{
    TokenString *str;
    str = tok_str_alloc();
    pp_expr = 1;

    while (1) {
        next();  /* 带宏展开 */
        if (tok == TOK_DEFINED) {
            /* defined(MACRO) 或 defined MACRO */
            parse_flags &= ~PARSE_FLAG_PREPROCESS;  /* 临时禁止宏展开 */
            next();
            t = tok;
            if (t == '(') next();
            parse_flags |= PARSE_FLAG_PREPROCESS;
            c = define_find(tok) ? 1 : 0;
            if (t == '(') { next(); /* ')' */ }
            tok = TOK_CLLONG; tokc.i = c;
        } else if (tok == TOK___HAS_INCLUDE ||
                   tok == TOK___HAS_INCLUDE_NEXT) {
            /* __has_include() 支持 */
            ...
        } else if (tok >= TOK_IDENT) {
            /* 未定义宏替换为 0 */
            c = 0;
            tok = TOK_CLLONG; tokc.i = c;
        }
        tok_str_add_tok(str);
    }

    /* 使用 expr_const() 对 token 流求值 */
    begin_macro(str, 1);
    next();
    c = expr_const();
    ...
    return c != 0;
}
```

关键设计点：

- `defined` 操作符需要**临时禁止宏展开**，因为 `defined(X)` 中的 `X` 不应被展开。
- 未定义的标识符被替换为 `0`（C 标准要求）。
- `__has_include()` 是 C23 特性，TinyCC 已提前支持。

### 3.8.5 preprocess_skip() — 跳过 false 分支

当条件为假时，需要跳过整个分支直到匹配的 `#else`、`#elif` 或 `#endif`。`preprocess_skip()`（`tccpp.c:874`）直接在字符级别扫描：

```c
static void preprocess_skip(void)
{
    int a, start_of_line, c;
    uint8_t *p;

    p = file->buf_ptr;
    a = 0;
redo_start:
    start_of_line = 1;
    for(;;) {
        c = *p;
        switch(c) {
        case '\n':    /* 换行 */
            file->line_num++;
            p++;
            goto redo_start;
        case '\\':    /* 行接续 */
            ...
        case '\"':    /* 跳过字符串 */
        case '\'':
            p = parse_pp_string(p, c, NULL);
            break;
        case '/':     /* 跳过注释 */
            ...
        case '#':
            p++;
            if (start_of_line) {
                file->buf_ptr = p;
                next_nomacro();
                if (a == 0 &&
                    (tok == TOK_ELSE || tok == TOK_ELIF || tok == TOK_ENDIF))
                    goto the_end;
                if (tok == TOK_IF || tok == TOK_IFDEF || tok == TOK_IFNDEF)
                    a++;           /* 嵌套 #if */
                else if (tok == TOK_ENDIF)
                    a--;           /* 匹配的 #endif */
            }
            break;
        default:
            p++;
            break;
        }
        start_of_line = 0;
    }
the_end:
    file->buf_ptr = p;
}
```

注意 `a` 变量跟踪嵌套的 `#if`/`#endif` 对。只有当 `a == 0` 时遇到的 `#else`/`#elif`/`#endif` 才是当前层的。

### 3.8.6 #else 与 #elif

```c
case TOK_ELSE:
    next_nomacro();
    if (s1->ifdef_stack_ptr[-1] & 2)
        tcc_error("#else after #else");
    c = (s1->ifdef_stack_ptr[-1] ^= 3);  /* 切换 bit 0，设置 bit 1 */
    goto test_else;

case TOK_ELIF:
    c = s1->ifdef_stack_ptr[-1];
    if (c > 1)
        tcc_error("#elif after #else");
    if (c == 1) {           /* 前面的分支已为真 */
        skip_to_eol(0);
        c = 0;              /* 跳过此 #elif */
    } else {
        c = expr_preprocess(s1);
        s1->ifdef_stack_ptr[-1] = c;
    }
```

`^= 3` 的位操作巧妙地实现了：如果当前 bit 0 为 1（前面的分支为真），则翻转为 0（当前分支为假）；同时设置 bit 1（已进入过分支）。

---

## 3.9 文件包含 #include

### 3.9.1 parse_include() 总体流程

`parse_include()`（`tccpp.c:1314`）处理 `#include` 和 `#include_next` 指令。

```c
static int parse_include(TCCState *s1, int do_next, int test)
{
    int c, i;
    char name[1024], buf[1024], *p;
    CachedInclude *e;

    /* 1. 解析文件名 */
    c = skip_spaces();
    if (c == '<' || c == '\"') {
        /* 标准形式：#include <file.h> 或 #include "file.h" */
        ...
    } else {
        /* 计算形式：#include MACRO_EXPANDED */
        parse_flags = PARSE_FLAG_PREPROCESS | ...;
        for (;;) {
            next();  /* 带宏展开 */
            ...
        }
    }

    /* 2. 搜索文件 */
    i = do_next ? file->include_next_index : -1;
    for (;;) {
        ++i;
        if (i == 0) {
            /* 绝对路径 */
            if (!IS_ABSPATH(name)) continue;
        } else if (i == 1) {
            /* "file.h" 形式：先搜索当前文件目录 */
            if (c != '\"') continue;
            ...
        } else {
            /* 搜索 include_paths[] 和 sysinclude_paths[] */
            ...
        }

        /* 3. 检查缓存 */
        e = search_cached_include(s1, buf, 0);
        if (e && (define_find(e->ifndef_macro) || e->once))
            return 1;  /* 已包含，跳过 */

        /* 4. 打开文件 */
        if (tcc_open(s1, buf) >= 0) break;
    }

    /* 5. 压入 include_stack */
    if (s1->include_stack_ptr >= s1->include_stack + INCLUDE_STACK_SIZE)
        tcc_error("#include recursion too deep");
    *s1->include_stack_ptr++ = file->prev;
    ...
}
```

### 3.9.2 搜索顺序

`#include <file.h>` 和 `#include "file.h"` 的搜索顺序不同：

**`#include <file.h>`**：
1. 绝对路径（如果 `name` 以 `/` 开头）。
2. 系统包含路径（`-isystem` 指定）。
3. 标准包含路径（`-I` 指定）。

**`#include "file.h"`**：
1. 绝对路径。
2. 当前文件所在目录。
3. 系统包含路径。
4. 标准包含路径。

**`#include_next`**（GNU 扩展）：
从当前文件的下一个搜索路径开始搜索，用于实现"覆盖式"头文件。

### 3.9.3 CachedInclude 优化

TinyCC 实现了 GCC 风格的 include guard 优化。当文件以 `#ifndef MACRO` 开头、以 `#endif` 结尾时，如果 `MACRO` 已定义，则跳过整个文件。

数据结构：

```c
typedef struct CachedInclude {
    int ifndef_macro;     /* #ifndef 中的宏 token ID */
    int once;             /* #pragma once 标志 */
    int hash_next;
    char filename[1];
} CachedInclude;
```

检测机制（在 `preprocess()` 中）：

1. 遇到 `#ifndef MACRO` 且 `is_bof` 为真时，记录 `file->ifndef_macro = tok`。
2. 遇到匹配的 `#endif` 且 `ifdef_stack_ptr` 回到文件起始位置时，记录 `file->ifndef_macro_saved`。
3. 下次包含同一文件时，检查 `ifndef_macro` 对应的宏是否已定义。

```c
case TOK_ENDIF:
    ...
    if (file->ifndef_macro &&
        s1->ifdef_stack_ptr == file->ifdef_stack_ptr) {
        file->ifndef_macro_saved = file->ifndef_macro;
        file->ifndef_macro = 0;
        tok_flags |= TOK_FLAG_ENDIF;
    }
    break;
```

### 3.9.4 include_stack 管理

```c
/* TCCState 中的定义 */
BufferedFile *include_stack[INCLUDE_STACK_SIZE];  /* 最大 32 层 */
BufferedFile **include_stack_ptr;
```

每次 `#include` 成功打开文件后，将当前文件压入栈：

```c
*s1->include_stack_ptr++ = file->prev;
```

当被包含的文件读取完毕（遇到 EOF）时，`tcc_close()` 关闭文件并恢复 `file` 指针。

### 3.9.5 #pragma once

`#pragma once` 的实现极其简洁：

```c
} else if (tok == TOK_once) {
    search_cached_include(s1, file->true_filename, 1)->once = 1;
}
```

它在 `CachedInclude` 中设置 `once` 标志。下次尝试包含同一文件时，`parse_include()` 检查此标志并跳过。

---

## 3.10 #pragma 指令

### 3.10.1 pragma_parse() 总览

`pragma_parse()`（`tccpp.c:1649`）处理所有 `#pragma` 指令。TinyCC 支持以下 pragma：

| pragma | 功能 |
|--------|------|
| `#pragma once` | 防止重复包含 |
| `#pragma pack(N)` | 设置结构体对齐 |
| `#pragma pack(push)` | 压入对齐设置 |
| `#pragma pack(pop)` | 弹出对齐设置 |
| `#pragma push_macro("M")` | 保存宏定义 |
| `#pragma pop_macro("M")` | 恢复宏定义 |
| `#pragma comment(lib, "name")` | 链接库（Windows） |
| `#pragma comment(option, "opts")` | 编译选项 |

### 3.10.2 #pragma pack

`#pragma pack` 控制结构体成员的对齐方式。TinyCC 使用一个栈（`pack_stack`）来管理嵌套的 pack 设置：

```c
} else if (tok == TOK_pack) {
    next();
    skip('(');
    if (tok == TOK_ASM_pop) {
        next();
        if (s1->pack_stack_ptr <= s1->pack_stack)
            tcc_error("out of pack stack");
        s1->pack_stack_ptr--;
    } else {
        int val = 0;
        if (tok != ')') {
            if (tok == TOK_ASM_push) {
                next();
                if (s1->pack_stack_ptr >= s1->pack_stack + PACK_STACK_SIZE - 1)
                    tcc_error("out of pack stack");
                val = *s1->pack_stack_ptr++;
                if (tok != ',') goto pack_set;
                next();
            }
            if (tok != TOK_CINT) goto pragma_err;
            val = tokc.i;
            if (val < 1 || val > 16 || (val & (val - 1)) != 0)
                goto pragma_err;
            next();
        }
    pack_set:
        *s1->pack_stack_ptr = val;
    }
}
```

pack 栈的设计：

```c
/* TCCState 中 */
int pack_stack[PACK_STACK_SIZE];
int *pack_stack_ptr;
```

- `pack(1)`：设置对齐为 1 字节。
- `pack()`：重置为默认对齐。
- `pack(push)`：压入当前值。
- `pack(push, N)`：压入当前值并设置为 N。
- `pack(pop)`：恢复上一个值。

对齐值必须是 1 到 16 之间的 2 的幂。

### 3.10.3 #pragma push_macro / pop_macro

`push_macro` 和 `pop_macro` 允许临时修改宏定义并恢复：

```c
if (tok == TOK_push_macro || tok == TOK_pop_macro) {
    int t = tok, v;
    Sym *s;

    if (next(), tok != '(') goto pragma_err;
    if (next(), tok != TOK_STR) goto pragma_err;
    v = tok_alloc(tokc.str.data, tokc.str.size - 1)->tok;
    if (next(), tok != ')') goto pragma_err;

    if (t == TOK_push_macro) {
        while (NULL == (s = define_find(v)))
            define_push(v, MACRO_OBJ, NULL, NULL);  /* 压入空定义 */
        s->type.ref = s;    /* 标记 push 边界 */
    } else {
        /* pop_macro: 恢复到 push 边界 */
        for (s = define_stack; s; s = s->prev)
            if (s->v == v && s->type.ref == s) {
                s->type.ref = NULL;
                break;
            }
        if (s)
            table_ident[v - TOK_IDENT]->sym_define = s->d ? s : NULL;
    }
}
```

`type.ref` 字段用作 push/pop 的边界标记。`push_macro` 时设置 `type.ref = s`，`pop_macro` 时查找该边界并恢复 `table_ident` 中的指针。

### 3.10.4 #pragma comment

`#pragma comment(lib, "name")` 将库名添加到链接列表：

```c
} else if (tok == TOK_comment) {
    char *p; int t;
    next(); skip('(');
    t = tok;
    next(); skip(',');
    if (tok != TOK_STR) goto pragma_err;
    p = tcc_strdup(tokc.str.data);
    next();
    if (tok != ')') goto pragma_err;
    if (t == TOK_lib) {
        dynarray_add(&s1->pragma_libs, &s1->nb_pragma_libs, p);
    } else if (t == TOK_option) {
        tcc_set_options(s1, p);
        tcc_free(p);
    }
}
```

---

## 3.11 内置宏

### 3.11.1 预定义宏的注册

在 `tccpp_new()` 中，TinyCC 注册了五个特殊宏作为"虚拟定义"：

```c
ST_FUNC void tccpp_new(TCCState *s)
{
    ...
    define_push(TOK___LINE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___FILE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___DATE__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___TIME__, MACRO_OBJ, NULL, NULL);
    define_push(TOK___COUNTER__, MACRO_OBJ, NULL, NULL);
}
```

注意这些定义的 `d` 字段为 `NULL`——它们没有宏体。展开逻辑在 `macro_subst_tok()` 的 `else` 分支中特殊处理。

### 3.11.2 内置宏的展开

```c
} else {
    CValue cval;
    char buf[32], *cstrval = buf;

    if (v == TOK___LINE__ || v == TOK___COUNTER__) {
        t = v == TOK___LINE__ ? file->line_num : pp_counter++;
        snprintf(buf, sizeof(buf), "%d", t);
        t = TOK_PPNUM;
        goto add_cstr1;

    } else if (v == TOK___FILE__) {
        cstrval = file->filename;
        goto add_cstr;

    } else if (v == TOK___DATE__ || v == TOK___TIME__) {
        time_t ti;
        struct tm *tm;
        time(&ti);
        tm = localtime(&ti);
        if (v == TOK___DATE__) {
            static char const ab_month_name[12][4] = {
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
            };
            snprintf(buf, sizeof(buf), "%s %2d %d",
                ab_month_name[tm->tm_mon], tm->tm_mday,
                tm->tm_year + 1900);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                tm->tm_hour, tm->tm_min, tm->tm_sec);
        }
    add_cstr:
        t = TOK_STR;
    add_cstr1:
        cval.str.size = strlen(cstrval) + 1;
        cval.str.data = cstrval;
        tok_str_add2_spc(tok_str, t, &cval);
    }
    return 0;
}
```

### 3.11.3 内置宏一览

| 宏 | 展开值 | 类型 | 说明 |
|----|--------|------|------|
| `__LINE__` | `file->line_num` | `TOK_PPNUM` | 当前行号 |
| `__FILE__` | `file->filename` | `TOK_STR` | 当前文件名 |
| `__DATE__` | `"Jun 10 2026"` | `TOK_STR` | 编译日期 |
| `__TIME__` | `"14:30:00"` | `TOK_STR` | 编译时间 |
| `__COUNTER__` | `0, 1, 2, ...` | `TOK_PPNUM` | 每次展开递增 |
| `__STDC__` | `1` | — | 在 `tcc_predefs()` 中定义 |
| `__STDC_VERSION__` | `201112L` 等 | — | C 标准版本 |
| `__STDC_HOSTED__` | `0` 或 `1` | — | 是否为 hosted 实现 |
| `__TINYC__` | `9xx` | — | TinyCC 版本号 |
| `__SIZEOF_POINTER__` | `4` 或 `8` | — | 指针大小 |
| `__SIZEOF_LONG__` | `4` 或 `8` | — | long 大小 |

### 3.11.4 平台相关宏

在 `tcc_predefs()` 中，TinyCC 根据目标平台定义一组宏：

```c
static const char * const target_os_defs =
#ifdef TCC_TARGET_PE
    "_WIN32\0"
# if PTR_SIZE == 8
    "_WIN64\0"
# endif
#else
# if defined TCC_TARGET_MACHO
    "__APPLE__\0"
# elif TARGETOS_FreeBSD
    "__FreeBSD__ 12\0"
# else
    "__linux__\0"
    "__linux\0"
# endif
    "__unix__\0"
    "__unix\0"
#endif
;
```

这些宏通过 `putdef()` 转换为 `#define` 字符串，注入到预处理输入流的开头。

---

## 3.12 -E 模式

### 3.12.1 tcc_preprocess() 函数

`tcc_preprocess()`（`tccpp.c:3891`）实现了 `tcc -E` 命令行选项，将预处理后的 token 流输出到标准输出（或指定文件）。

```c
ST_FUNC int tcc_preprocess(TCCState *s1)
{
    BufferedFile **iptr;
    int token_seen, spcs, level;
    const char *p;
    char white[400];

    parse_flags = PARSE_FLAG_PREPROCESS
                | (parse_flags & PARSE_FLAG_ASM_FILE)
                | PARSE_FLAG_LINEFEED
                | PARSE_FLAG_SPACES
                | PARSE_FLAG_ACCEPT_STRAYS;

    token_seen = TOK_LINEFEED, spcs = 0, level = 0;
    if (file->prev)
        pp_line(s1, file->prev, level++);
    pp_line(s1, file, level);

    for (;;) {
        iptr = s1->include_stack_ptr;
        next();
        if (tok == TOK_EOF) break;

        /* 输出 #line 指令（如需要） */
        level = s1->include_stack_ptr - iptr;
        if (level) {
            if (level > 0) pp_line(s1, *iptr, 0);
            pp_line(s1, file, level);
        }

        /* 调试输出 */
        if (s1->dflag & 7) {
            pp_debug_defines(s1);
            if (s1->dflag & 4) continue;
        }

        /* 处理空白和换行 */
        if (is_space(tok)) {
            if (spcs < sizeof white - 1) white[spcs++] = tok;
            continue;
        } else if (tok == TOK_LINEFEED) {
            spcs = 0;
            if (token_seen == TOK_LINEFEED) continue;
            ++file->line_ref;
        } else if (token_seen == TOK_LINEFEED) {
            pp_line(s1, file, 0);
        } else if (spcs == 0 && pp_need_space(token_seen, tok)) {
            white[spcs++] = ' ';
        }

        /* 输出 token */
        white[spcs] = 0;
        fputs(white, s1->ppfp), spcs = 0;
        fputs(p = get_tok_str(tok, &tokc), s1->ppfp);
        token_seen = pp_check_he0xE(tok, p);
    }
    return 0;
}
```

### 3.12.2 输出格式控制

`-E` 模式支持多种输出格式标志：

- `-P`：不输出 `#line` 指令。
- `-P1`：输出 `#line` 而非 `#`。
- `-dD`：输出宏定义信息。
- `-dM`：只输出宏定义（不处理源文件）。

`pp_line()` 函数根据 `s1->Pflag` 的值选择输出格式：

```c
static void pp_line(TCCState *s1, BufferedFile *f, int level)
{
    int d = f->line_num - f->line_ref;
    if (s1->Pflag == LINE_MACRO_OUTPUT_FORMAT_NONE) {
        ;   /* 不输出 */
    } else if (level == 0 && f->line_ref && d < 8) {
        while (d > 0)
            fputs("\n", s1->ppfp), --d;   /* 用空行代替 */
    } else if (s1->Pflag == LINE_MACRO_OUTPUT_FORMAT_STD) {
        fprintf(s1->ppfp, "#line %d \"%s\"\n", f->line_num, f->filename);
    } else {
        fprintf(s1->ppfp, "# %d \"%s\"%s\n", f->line_num, f->filename,
            level > 0 ? " 1" : level < 0 ? " 2" : "");
    }
    f->line_ref = f->line_num;
}
```

### 3.12.3 pp_need_space() — 空白插入

`pp_need_space()` 决定两个相邻 token 之间是否需要插入空格，以避免产生新的合并 token：

```c
static int pp_need_space(int a, int b)
{
    return 'E' == a ? '+' == b || '-' == b
        : '+' == a ? TOK_INC == b || '+' == b
        : '-' == a ? TOK_DEC == b || '-' == b
        : a >= TOK_IDENT || a == TOK_PPNUM ? b >= TOK_IDENT || b == TOK_PPNUM
        : 0;
}
```

例如：`1e` 后跟 `+` 需要空格（否则变成 `1e+`），`a` 后跟 `b` 需要空格（否则变成 `ab`）。

---

## 3.13 本章小结与练习

### 本章小结

本章深入分析了 TinyCC 预处理器的完整实现。核心要点：

1. **架构选择**：TinyCC 将预处理器集成在词法分析器 `tccpp.c` 中，而不是独立模块。
2. **两级词法分析**：`next_nomacro()` 不展开宏，`next()` 展开宏。预处理器指令通过 `preprocess()` 分发。
3. **宏存储**：`table_ident[]` 提供 O(1) 查找，`define_stack` 链表支持作用域嵌套。
4. **宏展开三阶段**：`macro_subst_tok()`（参数收集）→ `macro_arg_subst()`（参数替换）→ `macro_subst()`（递归展开）。
5. **## 拼接**：创建虚拟文件 `:paste:`，重新词法分析。
6. **条件编译**：`ifdef_stack` 管理嵌套状态，`preprocess_skip()` 在字符级别跳过 false 分支。
7. **文件包含**：`CachedInclude` 优化 include guard 模式。
8. **内置宏**：`__LINE__` 等特殊宏在 `macro_subst_tok()` 中直接生成值，不经过宏体替换。

### 练习

1. **宏展开追踪**（见 `exercises/ex1_expand.md`）：手动追踪复杂宏的展开过程。
2. **文件包含搜索**（见 `exercises/ex2_include.md`）：分析 `#include` 的搜索路径。
3. **pragma pack 实验**（见 `exercises/ex3_pragma.md`）：观察不同对齐设置对结构体布局的影响。

---

## 参考文献

1. ISO/IEC 9899:2011 (C11), §5.1.1.2 Translation phases, §6.10 Preprocessing directives.
2. ISO/IEC 9899:2018 (C18), same sections (no changes to preprocessing).
3. GCC Manual, Chapter 10: C Preprocessor Internals.
4. TinyCC source code: `tccpp.c`, `tcc.h`, `tcctok.h`.
