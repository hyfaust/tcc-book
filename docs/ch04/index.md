# 第四章 语法分析器与类型系统

> "编译器的心脏不在于它能生成多高效的代码，而在于它如何理解程序的结构。"

本章深入剖析 TinyCC 的语法分析器与类型系统。我们将看到，tcc 采用了一种极其精简的设计：没有抽象语法树（AST），解析与代码生成交织进行，类型信息压缩在单个整数的位域中。这种设计使得编译器在保持 C 语言完整支持的同时，代码量仅为 GCC 的百分之一。

## 4.1 语法分析理论

在深入 tcc 的实现之前，我们先回顾语法分析的基本理论。语法分析器（parser）的任务是将词法分析器产出的记号流（token stream）组织成符合语言文法的结构。

### 4.1.1 上下文无关文法

C 语言的语法可以用上下文无关文法（Context-Free Grammar, CFG）来描述。一个 CFG 是四元组 $G = (V, \Sigma, R, S)$，其中：

- $V$ 是非终结符集合（如 `expression`, `statement`, `declaration`）
- $\Sigma$ 是终结符集合（即词法记号：标识符、关键字、运算符等）
- $R$ 是产生式规则集合
- $S$ 是起始符号

以 C 语言的表达式为例，其文法规则可以写成：

```
expression     -> assignment_expr (',' assignment_expr)*
assignment_expr -> conditional_expr
                 | unary_expr ('=' | '+=' | '-=' | ...) assignment_expr
conditional_expr -> logor_expr ('?' expression ':' conditional_expr)?
logor_expr     -> logand_expr ('||' logand_expr)*
logand_expr    -> or_expr ('&&' or_expr)*
or_expr        -> xor_expr ('|' xor_expr)*
xor_expr       -> and_expr ('^' and_expr)*
and_expr       -> eq_expr ('&' eq_expr)*
eq_expr        -> rel_expr (('==' | '!=') rel_expr)*
rel_expr       -> shift_expr (('<' | '>' | '<=' | '>=') shift_expr)*
shift_expr     -> add_expr (('<<' | '>>') add_expr)*
add_expr       -> mul_expr (('+' | '-') mul_expr)*
mul_expr       -> unary_expr (('*' | '/' | '%') unary_expr)*
unary_expr     -> postfix_expr
               | ('++' | '--') unary_expr
               | ('&' | '*' | '+' | '-' | '~' | '!') unary_expr
               | 'sizeof' unary_expr
               | '(' type_name ')' unary_expr
postfix_expr   -> primary_expr
                 | postfix_expr '[' expression ']'
                 | postfix_expr '(' argument_list? ')'
                 | postfix_expr '.' identifier
                 | postfix_expr '->' identifier
                 | postfix_expr '++'
                 | postfix_expr '--'
```

这个层次结构精确地编码了 C 语言的运算符优先级和结合性。每下降一层，优先级升高一级。

### 4.1.2 递归下降分析

递归下降（recursive descent）是最直观的语法分析方法：为文法中的每个非终结符编写一个函数，函数体直接对应产生式的右部。遇到非终结符时递归调用对应函数；遇到终结符时匹配当前记号。

递归下降的优点是实现简单、错误信息精确、调试方便。其局限在于：

1. **左递归问题**：直接左递归（如 `expr -> expr '+' term`）会导致无限递归。解决方法是改写为右递归或使用循环。
2. **回溯问题**：当同一非终结符有多个产生式共享前缀时，需要前瞻（lookahead）或回溯（backtracking）来决定走哪条路。

tcc 的旧版表达式解析器采用了典型的递归下降方式，通过 `expr_prod()` -> `expr_sum()` -> `expr_shift()` -> ... -> `expr_lor()` 的调用链来体现优先级层次（见 `tccgen.c` 第 6389-6495 行）：

```c
/* 旧版递归下降解析器（非 precedence_parser 模式） */
static void expr_prod(void)
{
    int t;
    unary();
    while ((t = tok) == '*' || t == '/' || t == '%') {
        next();
        unary();
        gen_op(t);
    }
}

static void expr_sum(void)
{
    int t;
    expr_prod();
    while ((t = tok) == '+' || t == '-') {
        next();
        expr_prod();
        gen_op(t);
    }
}
/* ... expr_shift -> expr_cmp -> expr_cmpeq -> expr_and ->
       expr_xor -> expr_or -> expr_land -> expr_lor */
```

每个函数负责一个优先级层次：先调用更高优先级的函数解析操作数，然后循环处理当前优先级的运算符。这正是将左递归改写为循环的经典手法。

### 4.1.3 优先级爬升

优先级爬升（precedence climbing）是一种更紧凑的表达式解析算法。它用单个函数配合优先级参数来替代一连串递归函数。核心思想是：给定当前优先级 `p`，解析所有优先级 >= `p` 的运算符。

tcc 在定义了 `precedence_parser` 宏之后，采用了优先级爬升算法。其核心实现如下（`tccgen.c` 第 6501-6556 行）：

```c
static int precedence(int tok)
{
    switch (tok) {
        case TOK_LOR: return 1;
        case TOK_LAND: return 2;
        case '|': return 3;
        case '^': return 4;
        case '&': return 5;
        case TOK_EQ: case TOK_NE: return 6;
        case TOK_ULT: case TOK_UGE: return 7;
        case TOK_SHL: case TOK_SAR: return 8;
        case '+': case '-': return 9;
        case '*': case '/': case '%': return 10;
        default:
            if (tok >= TOK_ULE && tok <= TOK_GT)
                return 7;  /* 关系运算符 */
            return 0;      /* 非二元运算符 */
    }
}
```

优先级映射表如下：

| 优先级 | 运算符 | 含义 |
|--------|--------|------|
| 1 | `\|\|` | 逻辑或 |
| 2 | `&&` | 逻辑与 |
| 3 | `\|` | 按位或 |
| 4 | `^` | 按位异或 |
| 5 | `&` | 按位与 |
| 6 | `==`, `!=` | 等性比较 |
| 7 | `<`, `>`, `<=`, `>=` | 关系比较 |
| 8 | `<<`, `>>` | 移位 |
| 9 | `+`, `-` | 加减 |
| 10 | `*`, `/`, `%` | 乘除模 |

`expr_infix()` 函数实现了优先级爬升的核心逻辑：

```c
static void expr_infix(int p)
{
    int t = tok, p2;
    while ((p2 = precedence(t)) >= p) {
        if (t == TOK_LOR || t == TOK_LAND) {
            expr_landor(t);       /* 短路求值特殊处理 */
        } else {
            next();
            unary();              /* 解析右操作数 */
            if (precedence(tok) > p2)
                expr_infix(p2 + 1); /* 右结合递归 */
            gen_op(t);            /* 生成运算指令 */
        }
        t = tok;
    }
}
```

这个算法的关键观察：当右操作数后面紧跟的运算符优先级高于当前运算符时，需要递归进入更高优先级。这保证了 `1 + 2 * 3` 被正确解析为 `1 + (2 * 3)`。对于左结合运算符（如 `+`），循环自然处理；对于右结合运算符（如赋值），递归时传入 `p2 + 1` 而非 `p2`。

为了加速查表，tcc 在初始化时将 `precedence()` 的结果缓存到一个 256 字节的数组中：

```c
static unsigned char prec[256];
static void init_prec(void)
{
    int i;
    for (i = 0; i < 256; i++)
        prec[i] = precedence(i);
}
#define precedence(i) ((unsigned)i < 256 ? prec[i] : 0)
```

这样，对于 ASCII 范围内的运算符记号（`+`, `-`, `*`, `<` 等），查表只需一次数组访问。超出 256 的记号（如 `TOK_LAND`、`TOK_EQ` 等）走 switch 路径。

### 4.1.4 LL(1) 与回溯

严格来说，C 语言的文法不是 LL(1) 的。最典型的歧义场景是声明与表达式的区分：

```c
(x) - y    /* 表达式：x 减 y */
(int)x     /* 类型转换 */
```

两者都以 `(` 开头，需要前瞻多个记号才能区分。tcc 的解决策略是：

1. 在 `unary()` 中遇到 `(` 时，先尝试将其解析为类型名（调用 `parse_btype()`）。
2. 如果 `parse_btype()` 成功，则按类型转换或复合字面量处理。
3. 如果失败，则回退为普通表达式。

这种"尝试-回退"的方式在 `tccgen.c` 的 `unary()` 函数中体现得很清楚：

```c
case '(':
    next();
    /* 尝试解析为类型转换 */
    if (parse_btype(&type, &ad, 0)) {
        type_decl(&type, &ad, &n, TYPE_ABSTRACT);
        skip(')');
        /* 检查是否为 C99 复合字面量 */
        if (tok == '{') {
            /* 处理复合字面量 */
        } else {
            /* 类型转换 */
            unary();
            gen_cast(&type);
        }
    } else if (tok == '{') {
        /* GCC 语句表达式 */
    } else {
        /* 普通括号表达式 */
        gexpr();
        skip(')');
    }
```

同样，声明与语句的区分也存在歧义：

```c
x * y;   /* 表达式语句：x 乘以 y */
T *p;    /* 声明：T 类型的指针 p */
```

tcc 在 `decl()` 函数中先调用 `parse_btype()` 尝试解析类型说明符。如果成功，则进入声明解析路径；否则将当前行作为表达式语句处理。

### 4.1.5 错误恢复

tcc 的错误恢复策略非常简单粗暴：调用 `tcc_error()` 直接终止编译，不尝试恢复。这与 GCC 和 Clang 的"尽力继续"策略形成鲜明对比。tcc 的哲学是：快速报告第一个错误，让用户修复后重新编译。对于一个追求编译速度的编译器来说，这是一个合理的权衡。

`skip()` 函数是最常用的错误检测手段：

```c
ST_FUNC void skip(int c)
{
    if (tok != c) {
        char tmp[40];
        pstrcpy(tmp, sizeof tmp, get_tok_str(c, &tokc));
        tcc_error("'%s' expected (got '%s')", tmp, get_tok_str(tok, &tokc));
    }
    next();
}
```

它在期望某个特定记号时调用，如果当前记号不匹配就报错退出。

## 4.2 tcc 的单遍解析架构

### 4.2.1 无 AST 的设计

传统编译器通常分为多个阶段：

```
源代码 -> 词法分析 -> 语法分析 -> AST -> 语义分析 -> IR -> 优化 -> 目标代码
```

tcc 跳过了 AST 和 IR 阶段，直接在解析的同时生成目标代码：

```
源代码 -> 词法分析 -> 解析 + 代码生成 -> 目标代码
```

这种单遍（single-pass）架构意味着：

1. **没有中间表示**：不构建 AST，不生成中间代码。
2. **即时代码生成**：每解析完一个表达式或语句，立即生成对应的机器指令。
3. **符号驱动**：类型信息完全存储在符号表中，通过 `Sym` 结构的 `type` 字段传递。

### 4.2.2 值栈（vstack）机制

既然没有 AST 来存储中间结果，tcc 如何处理复合表达式？答案是**值栈**（`vstack`）。每个 `SValue` 结构包含：

```c
typedef struct SValue {
    CType type;          /* 类型信息 */
    unsigned short r;    /* 值的位置：寄存器号、VT_CONST、VT_LOCAL 等 */
    unsigned short r2;   /* 第二个寄存器（用于 long long 等双字类型） */
    union {
        struct { int jtrue, jfalse; };  /* 前向跳转链 */
        CValue c;                        /* 常量值 */
    };
    union {
        struct { unsigned short cmp_op, cmp_r; }; /* VT_CMP 比较操作 */
        struct Sym *sym;                           /* 关联的符号 */
    };
} SValue;
```

解析表达式 `a + b * c` 时：

1. `unary()` 解析 `a`，将一个 `SValue` 压入 `vstack`。
2. 遇到 `+`，调用 `expr_infix(9)`。
3. `unary()` 解析 `b`，压栈。
4. 遇到 `*`（优先级 10 > 9），递归进入 `expr_infix(10)`。
5. `unary()` 解析 `c`，压栈。
6. `*` 处理完毕，`gen_op('*')` 从栈顶弹出两个值，生成乘法指令，将结果压回。
7. 回到 `+`，`gen_op('+')` 生成加法指令。

`vtop` 指针始终指向栈顶的有效值。整个表达式求值过程就是在值栈上的操作。

### 4.2.3 交织解析与代码生成

以 `if` 语句为例，看解析与代码生成如何交织（`tccgen.c` 第 7183 行）：

```c
if (t == TOK_IF) {
    new_scope_s(&o);
    skip('(');
    gexpr_decl();                    /* 解析条件表达式 */
    a = gvtst(1, 0);                /* 生成条件跳转，a 记录跳转补丁位置 */
    skip(')');
    block(0);                        /* 解析 then 分支 */
    if (tok == TOK_ELSE) {
        d = gjmp(0);                /* 生成无条件跳转（跳过 else） */
        gsym(a);                     /* 补丁 then 跳转目标到此处 */
        next();
        block(0);                    /* 解析 else 分支 */
        gsym(d);                     /* 补丁 else 跳转目标到此处 */
    } else {
        gsym(a);                     /* 补丁条件跳转目标到此处 */
    }
    prev_scope_s(&o);
}
```

注意这里没有"先构建 AST 再遍历生成代码"的过程。解析 `if` 关键字的同时，条件跳转指令已经被生成到代码段中。`a = gvtst(1, 0)` 返回一个待补丁的跳转位置；当 then 分支的代码生成完毕后，`gsym(a)` 将跳转目标补丁为当前位置。

这种"前向引用 + 延迟补丁"的技术贯穿整个编译器。

### 4.2.4 优势与局限

**优势：**

1. **极快的编译速度**：单遍扫描，无需构建和遍历中间数据结构。
2. **极低的内存消耗**：不需要存储 AST 节点，只需维护符号表和值栈。
3. **代码简洁**：整个编译器核心（`tccgen.c`）不到 9000 行。

**局限：**

1. **无法进行全局优化**：没有 IR 意味着无法做公共子表达式消除、循环优化等。
2. **前向引用受限**：变量必须在使用前声明（符合 C89 要求）。函数的前向声明通过 `external_global_sym` 创建外部符号来处理。
3. **代码质量受限**：生成的代码质量远不如 GCC `-O2`，但足以"足够快地编译出能跑的程序"。
4. **某些语义分析受限**：例如无法在看到整个函数体后再决定是否内联。

### 4.2.5 编译入口

整个编译过程从 `tccgen_compile()` 开始（`tccgen.c` 第 306 行）：

```c
ST_FUNC int tccgen_compile(TCCState *s1)
{
    funcname = "";
    func_ind = -1;
    anon_sym = SYM_FIRST_ANOM;
    nocode_wanted = DATA_ONLY_WANTED;  /* 函数外不生成代码 */
    /* ... 初始化调试、覆盖率等 ... */

    parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_TOK_STR;
    next();                            /* 读取第一个记号 */
    decl(VT_CONST);                    /* 顶层声明解析循环 */
    gen_inline_functions(s1);          /* 生成所有被引用的内联函数 */
    check_vstack();                    /* 检查值栈平衡 */
    return 0;
}
```

`decl(VT_CONST)` 是顶层解析的入口。它循环调用 `parse_btype()` + `type_decl()` 来解析声明，遇到 `{` 时调用 `gen_function()` 生成函数体。整个翻译单元（translation unit）就在这个循环中被处理完毕。

## 4.3 类型系统设计

C 语言的类型系统是出了名的复杂：基本类型、派生类型（指针、数组、函数）、结构体/联合体、枚举、类型限定符、存储类说明符……tcc 如何在单个整数中编码这一切？

### 4.3.1 CType 结构

tcc 的类型表示只有两个字段（`tcc.h` 第 530 行）：

```c
typedef struct CType {
    int t;            /* 类型编码位域 */
    struct Sym *ref;  /* 附加信息指针 */
} CType;
```

`t` 是一个 32 位整数，通过位域编码基本类型、类型修饰符和存储类。`ref` 是一个指向 `Sym` 结构的指针，用于存储结构体/联合体的成员列表、函数的参数列表、数组的元素个数等无法用位域表达的信息。

这种设计的精妙之处在于：绝大多数类型操作只需位运算。例如判断一个类型是否为 `const`：

```c
if (type->t & VT_CONSTANT) { /* 是 const */ }
```

判断是否为指针：

```c
if ((type->t & VT_BTYPE) == VT_PTR) { /* 是指针 */ }
```

### 4.3.2 基本类型编码（VT_BTYPE）

`t` 的最低 4 位（bit 0-3）编码基本类型，由 `VT_BTYPE`（值为 `0x000f`）掩码提取：

```c
#define VT_BTYPE       0x000f   /* 基本类型掩码 */
#define VT_VOID             0   /* void */
#define VT_BYTE             1   /* signed char */
#define VT_SHORT            2   /* short */
#define VT_INT              3   /* int */
#define VT_LLONG            4   /* long long (64 位整数) */
#define VT_PTR              5   /* 指针（所有指针类型共享此值） */
#define VT_FUNC             6   /* 函数 */
#define VT_STRUCT           7   /* struct/union */
#define VT_FLOAT            8   /* IEEE 单精度浮点 */
#define VT_DOUBLE           9   /* IEEE 双精度浮点 */
#define VT_LDOUBLE         10   /* IEEE 长双精度浮点 */
#define VT_BOOL            11   /* _Bool (C99) */
#define VT_QLONG           13   /* 128 位整数（仅 x86-64 ABI） */
#define VT_QFLOAT          14   /* 128 位浮点（仅 x86-64 ABI） */
```

注意几个设计决策：

1. **所有指针共享 `VT_PTR`**：`int *` 和 `char *` 的 `VT_BTYPE` 都是 `VT_PTR`，区别在于 `ref` 指向的 `Sym` 中存储的被指向类型。
2. **数组也是指针**：数组类型同时设置 `VT_ARRAY` 和 `VT_PTR` 位。`ref->c` 存储数组长度，`ref->type` 存储元素类型。
3. **struct/union/enum 共享 `VT_STRUCT`**：通过高位进一步区分。

### 4.3.3 类型修饰符位域

基本类型之上，通过设置 `t` 中更高的位来添加修饰符：

```c
#define VT_UNSIGNED    0x0010   /* bit 4:  unsigned */
#define VT_DEFSIGN     0x0020   /* bit 5:  显式 signed/unsigned */
#define VT_ARRAY       0x0040   /* bit 6:  数组 */
#define VT_BITFIELD    0x0080   /* bit 7:  位域 */
#define VT_CONSTANT    0x0100   /* bit 8:  const */
#define VT_VOLATILE    0x0200   /* bit 9:  volatile */
#define VT_VLA         0x0400   /* bit 10: 变长数组（VLA） */
#define VT_LONG        0x0800   /* bit 11: long 修饰符 */
```

`VT_DEFSIGN` 标记是否显式指定了 `signed` 或 `unsigned`。这在类型检查中用于区分 `char`（由实现定义符号性）和 `signed char`（显式有符号）。

`VT_LONG` 是一个辅助位，用于区分 `long int` 和 `long long int`。具体来说：

- `long int`：`VT_INT | VT_LONG`
- `long long int`：`VT_LLONG | VT_LONG`
- `long double`：`VT_LDOUBLE | VT_LONG`（实际上 `parse_btype` 中直接设置为 `VT_LDOUBLE`）

### 4.3.4 存储类编码

存储类说明符占据 `t` 的 bit 12-16：

```c
#define VT_EXTERN  0x00001000   /* bit 12: extern */
#define VT_STATIC  0x00002000   /* bit 13: static */
#define VT_TYPEDEF 0x00004000   /* bit 14: typedef */
#define VT_INLINE  0x00008000   /* bit 15: inline */
#define VT_TLS     0x00010000   /* bit 16: _Thread_local */

#define VT_STORAGE (VT_EXTERN | VT_STATIC | VT_TYPEDEF | VT_INLINE | VT_TLS)
```

`VT_STORAGE` 掩码可以一次性提取所有存储类信息。在类型推导中，经常需要剥离存储类：

```c
type->t & ~VT_STORAGE   /* 去掉存储类，保留纯类型信息 */
```

### 4.3.5 结构体/联合体/枚举编码

结构体、联合体和枚举通过 `t` 的高位（bit 20 起）进一步区分：

```c
#define VT_STRUCT_SHIFT 20
/* VT_STRUCT 的高位编码 */
#define VT_UNION    (1 << VT_STRUCT_SHIFT | VT_STRUCT)   /* 联合体 */
#define VT_ENUM     (2 << VT_STRUCT_SHIFT)               /* 枚举类型 */
#define VT_ENUM_VAL (3 << VT_STRUCT_SHIFT)               /* 枚举常量值 */
```

枚举常量值（`VT_ENUM_VAL`）比较特殊：它的 `Sym.ref` 指向所属枚举类型，`Sym.enum_val` 存储常量的整数值。

### 4.3.6 位域编码

位域信息存储在 `t` 的高位中（bit 20-31），通过 `VT_BITFIELD` 位标识：

```c
#define VT_BITFIELD    0x0080
#define VT_STRUCT_SHIFT 20
#define VT_STRUCT_MASK (((1U << (6+6)) - 1) << VT_STRUCT_SHIFT | VT_BITFIELD)
```

位域编码包含两个 6 位字段：位域在存储单元中的偏移量和位域的宽度。这使得位域的访问可以在编译时计算出移位和掩码操作。

### 4.3.7 完整类型示例

让我们看几个复杂类型在 tcc 中如何表示：

**示例 1：`const char *p`**

```
p 的类型:
  t = VT_PTR
  ref -> Sym:
    type.t = VT_BYTE | VT_CONSTANT
    type.ref = NULL

编码: VT_PTR, 指向 const signed char
```

**示例 2：`int (*callback)(double, ...)`**

```
callback 的类型:
  t = VT_PTR
  ref -> Sym:                    (*指针指向的函数*)
    type.t = VT_FUNC
    type.ref -> Sym:             (*函数的参数列表头部*)
      type.t = VT_INT           (*返回类型*)
      f.func_type = FUNC_ELLIPSIS (*变参函数*)
      f.func_call = FUNC_CDECL
      next -> Sym:              (*第一个参数*)
        type.t = VT_DOUBLE
        v = SYM_FIELD
        next -> NULL
```

**示例 3：`struct point { int x, y; }`**

```
struct point 的类型:
  t = VT_STRUCT
  ref -> Sym:                    (*struct 定义*)
    v = 'point' | SYM_STRUCT    (*结构体标签*)
    type.t = VT_STRUCT
    next -> Sym:                 (*成员 y*)
      v = 'y' | SYM_FIELD
      type.t = VT_INT
      c = 4                     (*偏移量*)
      next -> Sym:              (*成员 x*)
        v = 'x' | SYM_FIELD
        type.t = VT_INT
        c = 0                   (*偏移量*)
        next -> NULL
```

注意成员列表是逆序存储的（`y` 在 `x` 前面），这是由 `sym_push` 的栈式插入方式决定的。

**示例 4：`int arr[10]`**

```
arr 的类型:
  t = VT_ARRAY | VT_PTR
  ref -> Sym:
    type.t = VT_INT             (*元素类型*)
    c = 10                      (*数组长度*)
```

数组类型同时设置了 `VT_ARRAY` 和 `VT_PTR`。`ref->c` 存储元素个数。这种编码使得数组到指针的退化（array-to-pointer decay）只需清除 `VT_ARRAY` 位：

```c
type->t &= ~VT_ARRAY;  /* 数组退化为指针 */
```

**示例 5：`volatile unsigned long long *restrict p`**

```
p 的类型:
  t = VT_PTR | VT_VOLATILE      (*指针本身是 volatile 的——不对，这里要小心*)

实际上 restrict 被 tcc 忽略（直接 next() 跳过），
而 volatile 修饰的是被指向的类型，所以：

p 的类型:
  t = VT_PTR
  ref -> Sym:
    type.t = VT_LLONG | VT_UNSIGNED | VT_VOLATILE
```

在 `type_decl()` 中，`*` 之后的限定符（const、volatile、restrict）修饰的是指针所指向的类型：

```c
while (tok == '*') {
    qualifiers = 0;
redo:
    next();
    switch(tok) {
    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
        qualifiers |= VT_CONSTANT;
        goto redo;
    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
        qualifiers |= VT_VOLATILE;
        goto redo;
    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
        goto redo;  /* restrict 被忽略 */
    }
    mk_pointer(type);
    type->t |= qualifiers;  /* 限定符加到指针类型上 */
}
```

### 4.3.8 mk_pointer：构造指针类型

`mk_pointer()` 是一个关键的辅助函数，它将任意类型转换为指向该类型的指针：

```c
ST_FUNC void mk_pointer(CType *type)
{
    Sym *s;
    s = sym_push(SYM_FIELD, type, 0, -1);
    type->t = VT_PTR;
    type->ref = s;
}
```

它创建一个新的匿名 `Sym` 来保存原类型，然后将 `type` 修改为 `VT_PTR`，`ref` 指向这个新 `Sym`。这个操作是可嵌套的——对指针类型再次调用 `mk_pointer` 就得到指向指针的指针（`VT_PTR` 的 `ref->type` 也是 `VT_PTR`）。

### 4.3.9 类型兼容性检查

`tcc` 通过 `is_compatible_types()` 和 `is_compatible_unqualified_types()` 进行类型兼容性检查。核心逻辑是：

1. 如果 `VT_BTYPE` 不同，不兼容（特殊处理指针和整数的兼容性）。
2. 对于指针，递归检查被指向的类型。
3. 对于函数，检查返回类型、参数数量和类型、调用约定。
4. 对于结构体，比较 `ref` 指针——同一结构体定义共享同一个 `Sym`，所以只需比较指针即可。

## 4.4 符号表系统

符号表是编译器的核心数据结构之一。tcc 的符号表设计精巧而高效，通过链表栈和记号表的双向链接实现了快速查找和作用域管理。

### 4.4.1 Sym 结构详解

`Sym` 结构是符号表的基本单元（`tcc.h` 第 558 行）：

```c
typedef struct Sym {
    int v;                    /* 符号记号（token identifier） */
    unsigned short r;         /* 关联的寄存器或位置类型 */
    struct SymAttr a;         /* 符号属性 */
    union {
        struct {
            int c;            /* 关联数值或 ELF 符号索引 */
            union {
                int sym_scope;    /* 局部变量的作用域层级 */
                int jnext;        /* 下一个跳转标签 */
                int jind;         /* 标签位置 */
                struct FuncAttr f; /* 函数属性 */
                int auxtype;      /* 位域访问类型 */
            };
        };
        long long enum_val;   /* 枚举常量值（当 IS_ENUM_VAL 时） */
        int *d;               /* 宏定义的记号流 */
        struct Sym *cleanup_func;
    };

    CType type;               /* 类型信息 */
    union {
        struct Sym *next;     /* 下一个相关符号（用于结构体成员和匿名符号） */
        int *e;               /* 宏展开后的记号流 */
        int asm_label;        /* 关联的汇编标签 */
        /* ... 其他用途 ... */
    };
    struct Sym *prev;         /* 栈中前一个符号 */
    union {
        struct Sym *prev_tok; /* 同名的前一个符号（作用域链） */
        /* ... 其他用途 ... */
    };
} Sym;
```

各字段详解：

**`v`（符号记号）**：这是符号的"名字"，实际上是一个记号编号。每个标识符在词法分析阶段被分配一个唯一的编号（从 `TOK_IDENT` 开始递增）。`v` 的高位用于标记特殊含义：

```c
#define SYM_STRUCT     0x40000000  /* 结构体/联合体/枚举符号空间 */
#define SYM_FIELD      0x20000000  /* 结构体成员符号空间 */
#define SYM_FIRST_ANOM 0x10000000  /* 第一个匿名符号的编号 */
```

`SYM_STRUCT` 和 `SYM_FIELD` 使得同一个标识符名可以同时用于变量和结构体标签（C 语言的结构体标签和变量名在不同的命名空间中）。

**`r`（寄存器/位置）**：指示符号值的存放位置。对于局部变量，通常是 `VT_LOCAL | VT_LVAL`（栈上的左值）；对于全局变量，是 `VT_CONST | VT_SYM`（符号引用的常量地址）。

**`a`（符号属性）**：`SymAttr` 位域结构：

```c
struct SymAttr {
    unsigned short
    aligned     : 5,  /* 对齐（log2+1，0 表示未指定） */
    packed      : 1,  /* __attribute__((packed)) */
    weak        : 1,  /* __attribute__((weak)) */
    visibility  : 2,  /* 符号可见性 */
    dllexport   : 1,  /* PE DLL 导出 */
    nodecorate  : 1,  /* PE 无修饰 */
    dllimport   : 1,  /* PE DLL 导入 */
    addrtaken   : 1,  /* 地址被取过 */
    nodebug     : 1,  /* 无调试信息 */
    xxxx        : 2;  /* 保留 */
};
```

**`c`（数值/索引）**：根据上下文有不同含义：
- 对于局部变量：栈偏移量
- 对于全局变量：ELF 符号表中的索引
- 对于数组：元素个数（当作为 `type.ref` 时）
- 对于函数参数：累计参数大小

**`f`（函数属性）**：`FuncAttr` 位域：

```c
struct FuncAttr {
    unsigned
    func_call   : 3,  /* 调用约定（cdecl, stdcall, fastcall 等） */
    func_type   : 2,  /* FUNC_NEW/FUNC_OLD/FUNC_ELLIPSIS */
    func_noreturn : 1, /* __attribute__((noreturn)) */
    func_ctor   : 1,  /* __attribute__((constructor)) */
    func_dtor   : 1,  /* __attribute__((destructor)) */
    func_args   : 8,  /* PE __stdcall 参数字节数 */
    func_alwinl : 1,  /* __attribute__((always_inline)) */
    xxxx        : 15;
};
```

**`type`（类型信息）**：`CType` 结构，如前所述。

**`next`（相关符号链）**：用于链接结构体的成员列表、函数的参数列表。`next` 链接的是语义上相关的符号，而非作用域上的邻居。

**`prev`（栈前驱）**：链接到同一符号栈中的前一个符号。这是符号栈的核心链接方式——`global_stack` 和 `local_stack` 都是通过 `prev` 字段形成的链表。

**`prev_tok`（同名前驱）**：链接到同名符号的上一个定义。这是实现名称遮蔽（name shadowing）的关键。

### 4.4.2 三个符号栈

tcc 维护三个全局符号栈（`tccgen.c` 第 36 行）：

```c
ST_DATA Sym *global_stack;    /* 全局符号栈 */
ST_DATA Sym *local_stack;     /* 局部符号栈 */
ST_DATA Sym *define_stack;    /* 预处理器宏定义栈 */
```

此外还有两个标签栈：

```c
ST_DATA Sym *global_label_stack;  /* 全局标签栈 */
ST_DATA Sym *local_label_stack;   /* 局部标签栈 */
```

**global_stack**：存储全局变量和函数声明。在整个编译过程中持续存在，不随函数结束而清除。

**local_stack**：存储当前函数的局部变量和参数。函数编译开始时为空，编译结束时全部弹出。`local_stack` 非空也作为"当前是否在函数体内"的标志——`sym_push()` 根据它决定将符号压入哪个栈：

```c
ST_FUNC Sym *sym_push(int v, CType *type, int r, int c)
{
    Sym *s, **ps;
    if (local_stack)
        ps = &local_stack;
    else
        ps = &global_stack;
    /* ... */
}
```

**define_stack**：存储预处理器宏定义。每个宏定义对应一个 `Sym`，其 `d` 字段指向宏的替换记号流。

### 4.4.3 符号查找：记号表与双向链接

tcc 的符号查找不是遍历符号栈，而是通过记号表（`TokenSym`）直接定位。每个标识符在词法分析阶段就被分配了一个 `TokenSym` 结构：

```c
typedef struct TokenSym {
    struct TokenSym *hash_next;  /* 哈希链 */
    struct Sym *sym_define;      /* 指向宏定义 */
    struct Sym *sym_label;       /* 指向标签 */
    struct Sym *sym_struct;      /* 指向结构体定义 */
    struct Sym *sym_identifier;  /* 指向变量/函数定义 */
    int tok;                     /* 记号编号 */
    int len;                     /* 名称长度 */
    char str[1];                 /* 名称字符串（柔性数组） */
} TokenSym;
```

四个 `Sym*` 指针分别指向该标识符在四个命名空间中的当前定义。查找一个标识符只需一次数组访问：

```c
ST_INLN Sym *sym_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_identifier;
}
```

`table_ident` 是一个全局数组，以记号编号为索引。`v - TOK_IDENT` 就是数组下标。

### 4.4.4 sym_push 与 sym_pop：栈操作

**`sym_push()`** 将一个新符号压入当前栈（`tccgen.c` 第 715 行）：

```c
ST_FUNC Sym *sym_push(int v, CType *type, int r, int c)
{
    Sym *s, **ps;
    if (local_stack)
        ps = &local_stack;
    else
        ps = &global_stack;
    s = sym_push2(ps, v, type->t, c);
    s->type.ref = type->ref;
    s->r = r;
    /* 非匿名符号才记录到记号表 */
    if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
        sym_link(s, 1);  /* 将 s 链接到 table_ident */
        /* 检查同作用域重定义 */
        if (s->prev_tok && sym_scope_ex(s->prev_tok) == local_scope)
            tcc_error("redeclaration of '%s'", get_tok_str(s->v, NULL));
    }
    return s;
}
```

关键步骤：
1. `sym_push2()` 分配新 `Sym`，压入栈顶。
2. `sym_link(s, 1)` 将符号链接到 `table_ident` 中对应的 `sym_identifier`（或 `sym_struct`）。

**`sym_link()`** 的实现揭示了名称遮蔽的机制：

```c
static inline void sym_link(Sym *s, int yes)
{
    TokenSym *ts = table_ident[(s->v & ~SYM_STRUCT) - TOK_IDENT];
    Sym **ps;
    if (s->v & SYM_STRUCT)
        ps = &ts->sym_struct;
    else
        ps = &ts->sym_identifier;
    if (yes) {
        s->prev_tok = *ps, *ps = s;  /* 新符号成为栈顶 */
        s->sym_scope = local_scope;
    } else {
        *ps = s->prev_tok;           /* 恢复旧定义 */
    }
}
```

当 `sym_link(s, 1)` 时，新符号 `s` 被插入到 `sym_identifier` 链的头部，`s->prev_tok` 保存旧的头部。这样，`sym_find()` 总是找到最近的定义——这就是名称遮蔽的实现。

当 `sym_link(s, 0)` 时（符号被弹出），旧的定义通过 `prev_tok` 恢复。

**`sym_pop()`** 弹出符号直到指定的边界（`tccgen.c` 第 762 行）：

```c
ST_FUNC void sym_pop(Sym **ptop, Sym *b, int keep)
{
    Sym *s, *ss;
    int v;
    s = *ptop;
    while(s != b) {
        ss = s->prev;
        v = s->v;
        /* 从记号表中移除符号 */
        if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
            sym_link(s, 0);
        if (!keep)
            sym_free(s);
        s = ss;
    }
    if (!keep)
        *ptop = b;
}
```

`keep` 参数在处理语句表达式（statement expression）和 `for` 循环中的 VLA 时使用：这些场景下符号需要从查找表中移除（`sym_link(s, 0)`），但不能从栈中释放（因为还有引用）。

### 4.4.5 作用域管理

tcc 的作用域管理由 `struct scope` 结构和 `local_scope` 计数器共同完成（`tccgen.c` 第 121 行）：

```c
static struct scope {
    struct scope *prev;              /* 外层作用域 */
    struct { int loc, locorig, num; } vla;  /* VLA 管理 */
    struct { Sym *s; int n; } cl;    /* cleanup 函数链 */
    int *bsym;                       /* break 跳转链 */
    int *csym;                       /* continue 跳转链 */
    Sym *lstk;                       /* 进入作用域时的 local_stack 栈顶 */
    Sym *llstk;                      /* 进入作用域时的 local_label_stack 栈顶 */
} *cur_scope, *loop_scope, *root_scope;

static int local_scope;  /* 当前作用域深度，函数外为 0 */
```

**`new_scope()`** 进入一个新的复合语句作用域（`tccgen.c` 第 7073 行）：

```c
static void new_scope(struct scope *o)
{
    *o = *cur_scope;      /* 继承外层作用域的 bsym、csym 等 */
    o->prev = cur_scope;
    cur_scope = o;
    cur_scope->vla.num = 0;
    o->lstk = local_stack;        /* 记录当前栈顶 */
    o->llstk = local_label_stack; /* 记录当前标签栈顶 */
    ++local_scope;
}
```

**`prev_scope()`** 离开作用域（`tccgen.c` 第 7087 行）：

```c
static void prev_scope(struct scope *o, int is_expr)
{
    vla_leave(o->prev);
    if (o->cl.s != o->prev->cl.s)
        block_cleanup(o->prev);   /* 调用 __attribute__((cleanup)) */
    label_pop(&local_label_stack, o->llstk, is_expr);
    sym_pop(&local_stack, o->lstk, is_expr);  /* 弹出局部符号 */
    cur_scope = o->prev;
    --local_scope;
}
```

`sym_pop(&local_stack, o->lstk, is_expr)` 弹出本作用域内声明的所有局部符号。由于 `sym_pop` 内部调用 `sym_link(s, 0)`，这些符号从记号表中移除，外层同名符号（如果有）自动恢复可见性——名称遮蔽就这样自动解除了。

**`new_scope_s()` / `prev_scope_s()`** 是简化版本，用于 `if`、`while`、`switch` 等不会引入新变量声明（但可能有结构体/枚举定义）的语句：

```c
static void new_scope_s(struct scope *o)
{
    o->lstk = local_stack;
    ++local_scope;
}

static void prev_scope_s(struct scope *o)
{
    sym_pop(&local_stack, o->lstk, 0);
    --local_scope;
}
```

### 4.4.6 符号内存管理

tcc 使用池化分配器管理 `Sym` 对象，避免频繁的 `malloc`/`free` 调用（`tccgen.c` 第 670 行）：

```c
static Sym *sym_free_first;  /* 空闲链表头 */
static void **sym_pools;     /* 内存池数组 */
static int nb_sym_pools;     /* 内存池数量 */

static inline Sym *sym_malloc(void)
{
    Sym *sym = sym_free_first;
    if (!sym)
        sym = __sym_malloc();  /* 分配新的内存池 */
    sym_free_first = sym->next;
    return sym;
}
```

`SYM_POOL_NB` 个 `Sym` 对象被一次性分配在一个内存池中，然后串成空闲链表。分配时从链表头取出，释放时放回链表头。这种方式既快速又避免了内存碎片。

## 4.5 表达式解析

表达式解析是编译器中最频繁执行的代码路径。tcc 的表达式解析器将源代码中的表达式转化为值栈上的操作和目标代码中的指令。

### 4.5.1 优先级爬升的完整流程

以表达式 `a + b * c == d && e || f` 为例，追踪 `expr_infix` 的执行过程。

入口调用：`expr_lor()` -> `unary()` 解析 `a`，然后 `expr_infix(1)`（优先级 1 开始）。

```
expr_infix(1):
  t = '+', p2 = 9, 9 >= 1: 进入循环
    非 &&/||，next()，unary() 解析 b
    precedence('*') = 10 > 9，递归 expr_infix(10)
      expr_infix(10):
        t = '*', p2 = 10, 10 >= 10: 进入循环
          next()，unary() 解析 c
          precedence('==') = 6, 6 > 10? 否，不递归
          gen_op('*')  -> b*c 压栈
        t = '==', p2 = 6, 6 >= 10? 否，退出循环
      返回
    gen_op('+')  -> a+b*c 压栈
  t = '==', p2 = 6, 6 >= 1: 继续循环
    非 &&/||，next()，unary() 解析 d
    precedence('&&') = 2, 2 > 6? 否，不递归
    gen_op('==')  -> a+b*c==d 压栈
  t = '&&', p2 = 2, 2 >= 1: 继续循环
    是 &&，进入 expr_landor(TOK_LAND)  -> 短路求值处理
  ...
```

### 4.5.2 短路求值

`&&` 和 `||` 的处理由 `expr_landor()` 完成（`tccgen.c` 第 6565 行），它实现了 C 语言规定的短路求值语义：

```c
static void expr_landor(int op)
{
    int t = 0, cc = 1, f = 0, i = op == TOK_LAND, c;
    for(;;) {
        c = f ? i : condition_3way();  /* 尝试静态求值 */
        if (c < 0)
            save_regs(1), cc = 0;      /* 无法静态求值，保存寄存器 */
        else if (c != i)
            nocode_wanted++, f = 1;    /* 短路：结果已确定 */
        if (tok != op)
            break;
        if (c < 0)
            t = gvtst(i, t);           /* 生成条件跳转 */
        else
            vpop();
        next();
        expr_landor_next(op);
    }
    if (cc || f) {
        vpop();
        vpushi(i ^ f);                 /* 布尔结果 */
        gsym(t);                       /* 补丁跳转目标 */
        nocode_wanted -= f;
    } else {
        gvtst_set(i, t);              /* 保留跳转链供后续使用 */
    }
}
```

对于 `a && b && c`：

1. 解析 `a`，`condition_3way()` 尝试判断是否为常量。
2. 如果 `a` 的值在编译时未知（`c < 0`），调用 `save_regs(1)` 确保当前值已存入寄存器，然后 `gvtst(1, 0)` 生成"如果为假则跳转"的指令。跳转目标暂时未知，记为 `t`。
3. 解析 `b`，同样处理。
4. 解析 `c`。
5. 所有子表达式处理完后，`gsym(t)` 将跳转链补丁为当前代码位置。

对于 `a || b`：

1. 解析 `a`，生成"如果为真则跳转"的指令（跳过 `b` 的求值）。
2. 解析 `b`。
3. 结果在跳转链上合并。

`condition_3way()` 函数尝试在编译时确定条件的真假：

```c
static int condition_3way(void)
{
    int c = -1;
    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST &&
        (!(vtop->r & VT_SYM) || !vtop->sym->a.weak)) {
        vdup();
        gen_cast_s(VT_BOOL);
        c = vtop->c.i;
        vpop();
    }
    return c;  /* -1: 未知, 0: 假, 1: 真 */
}
```

如果条件是编译时常量，可以直接决定短路，避免生成无用的跳转指令。

### 4.5.3 unary()：一元表达式解析

`unary()` 是表达式解析的核心函数（`tccgen.c` 第 5591 行），处理所有"原子"表达式和后缀操作。它是一个约 700 行的 switch-case 语句。

**字面量处理：**

```c
case TOK_CINT:
case TOK_CCHAR:
    t = VT_INT;
    type.t = t;
    vsetc(&type, VT_CONST, &tokc);  /* 将常量压入值栈 */
    next();
    break;
case TOK_CLLONG:
    t = VT_LLONG;
    goto push_tokc;
case TOK_CFLOAT:
    t = VT_FLOAT;
    goto push_tokc;
/* ... */
```

字面量直接作为 `VT_CONST` 压入值栈，不生成任何代码。

**字符串字面量：**

```c
case TOK_STR:
    t = char_type.t;
    type.t = t;
    mk_pointer(&type);
    type.t |= VT_ARRAY;
    memset(&ad, 0, sizeof(AttributeDef));
    ad.section = rodata_section;
    decl_initializer_alloc(&type, &ad, VT_CONST, 2, 0, 0);
    break;
```

字符串字面量被分配到 `.rodata` 段，生成一个指向该段的指针常量。

**标识符处理：**

```c
default:
tok_identifier:
    if (tok < TOK_UIDENT)
        tcc_error("expression expected before '%s'", get_tok_str(tok, &tokc));
    t = tok;
    next();
    s = sym_find(t);
    if (!s || IS_ASM_SYM(s)) {
        const char *name = get_tok_str(t, NULL);
        if (tok != '(')
            tcc_error("'%s' undeclared", name);
        /* 隐式函数声明：兼容 K&R C */
        if (!func_old)
            tcc_warning_c(warn_implicit_function_declaration)(
                "implicit declaration of function '%s'", name);
        s = external_global_sym(t, &func_old_type);
    }

    r = s->r;
    if ((r & VT_VALMASK) < VT_CONST)
        r = (r & ~VT_VALMASK) | VT_LOCAL;  /* 寄存器变量 */

    vset(&s->type, r, s->c);
    vtop->sym = s;

    if (r & VT_SYM) {
        vtop->c.i = 0;
    } else if (r == VT_CONST && IS_ENUM_VAL(s->type.t)) {
        vtop->c.i = s->enum_val;  /* 枚举常量 */
    }
    break;
```

标识符查找的关键路径：
1. `sym_find(t)` 在记号表中查找。
2. 如果未找到且下一个记号是 `(`，按隐式函数声明处理（`int()` 类型）。
3. 否则报错"未声明"。
4. 将符号的类型和位置信息压入值栈。

**sizeof 和 alignof：**

```c
case TOK_SIZEOF:
case TOK_ALIGNOF1: case TOK_ALIGNOF2: case TOK_ALIGNOF3:
    t = tok;
    next();
    if (tok == '(')
        tok = TOK_SOTYPE;      /* 特殊标记，区分 sizeof(type) 和 sizeof expr */
    expr_type(&type, unary);   /* 在 nocode_wanted 模式下求类型 */
    if (t == TOK_SIZEOF) {
        vpush_type_size(&type, &align);
        gen_cast_s(VT_SIZE_T);
    } else {
        type_size(&type, &align);
        vpushs(align);
    }
    break;
```

`expr_type()` 在 `nocode_wanted` 模式下调用 `unary()`，这样可以获取表达式的类型而不生成任何代码。

**一元运算符：**

```c
case '*':     /* 解引用 */
    next(); unary(); indir(); break;
case '&':     /* 取地址 */
    next(); unary();
    if ((vtop->type.t & VT_BTYPE) != VT_FUNC &&
        !(vtop->type.t & (VT_ARRAY | VT_VLA)))
        test_lvalue();
    if (vtop->sym)
        vtop->sym->a.addrtaken = 1;
    mk_pointer(&vtop->type);
    gaddrof();
    break;
case '!':     /* 逻辑非 */
    next(); unary(); gen_test_zero(TOK_EQ); break;
case '~':     /* 按位取反 */
    next(); unary(); vpushi(-1); gen_op('^'); break;
case '-':     /* 一元负号 */
    next(); unary();
    if (is_float(vtop->type.t))
        gen_opif(TOK_NEG);
    else {
        vpushi(0); vswap(); gen_op('-');
    }
    break;
case TOK_INC:  /* 前缀 ++ */
case TOK_DEC:  /* 前缀 -- */
    t = tok; next(); unary(); inc(0, t); break;
```

### 4.5.4 后缀操作：数组下标、成员访问、函数调用

`unary()` 的后半部分处理所有后缀操作（`tccgen.c` 第 6238 行）：

```c
/* 后缀操作循环 */
while (1) {
    if (tok == TOK_INC || tok == TOK_DEC) {
        inc(1, tok);  /* 后缀 ++/-- */
        next();
    } else if (tok == '.' || tok == TOK_ARROW) {
        /* 成员访问 */
        if (tok == TOK_ARROW)
            indir();              /* -> 先解引用 */
        qualifiers = vtop->type.t & (VT_CONSTANT | VT_VOLATILE);
        test_lvalue();
        next();
        s = find_field(&vtop->type, tok, &cumofs);  /* 查找成员 */
        gaddrof();                /* 取地址 */
        vtop->type = char_pointer_type;  /* 转为 char* */
        vpushi(cumofs);           /* 压入偏移量 */
        gen_op('+');              /* 地址 + 偏移量 */
        vtop->type = s->type;    /* 改为成员类型 */
        vtop->type.t |= qualifiers;
        if (!(vtop->type.t & VT_ARRAY))
            vtop->r |= VT_LVAL;
        next();
    } else if (tok == '[') {
        /* 数组下标 */
        next(); gexpr();
        gen_op('+');   /* 指针 + 偏移量 */
        indir();       /* 解引用 */
        skip(']');
    } else if (tok == '(') {
        /* 函数调用 */
        /* ... 见下文 ... */
    } else break;
}
```

成员访问（`.` 和 `->`）的实现巧妙地利用了指针算术：先取结构体地址（`gaddrof`），转为 `char *`，加上成员偏移量，再转为成员类型。这避免了为结构体成员访问生成专门的指令。

数组下标 `a[i]` 被等价转换为 `*(a + i)`。

### 4.5.5 函数调用的代码生成

函数调用是 `unary()` 中最复杂的后缀操作（`tccgen.c` 第 6286 行）。核心流程：

```c
} else if (tok == '(') {
    SValue ret;
    Sym *sa;
    int nb_args, ret_nregs, ret_align, regsize, variadic;

    /* 检查是否为函数类型（或函数指针） */
    if ((vtop->type.t & VT_BTYPE) != VT_FUNC) {
        if ((vtop->type.t & (VT_BTYPE | VT_ARRAY)) == VT_PTR) {
            vtop->type = *pointed_type(&vtop->type);
            if ((vtop->type.t & VT_BTYPE) != VT_FUNC)
                goto error_func;
        } else {
            error_func:
            expect("function pointer");
        }
    }

    s = vtop->type.ref;     /* 函数符号（含参数列表） */
    next();
    sa = s->next;            /* 第一个参数 */
    nb_args = 0;

    /* 处理结构体返回值的隐式参数 */
    if ((s->type.t & VT_BTYPE) == VT_STRUCT) {
        variadic = (s->f.func_type == FUNC_ELLIPSIS);
        ret_nregs = gfunc_sret(&s->type, variadic, &ret.type,
                               &ret_align, &regsize);
        if (ret_nregs <= 0) {
            /* 通过隐式指针返回结构体 */
            size = type_size(&s->type, &align);
            loc = (loc - size) & -align;
            ret.type = s->type;
            ret.r = VT_LOCAL | VT_LVAL;
            vseti(VT_LOCAL, loc);
            ret.c = vtop->c;
            nb_args++;
        }
    }

    /* 解析实参 */
    if (tok != ')') {
        for(;;) {
            expr_eq();
            gfunc_param_typed(s, sa);  /* 类型检查和转换 */
            nb_args++;
            if (sa) sa = sa->next;
            if (tok == ')') break;
            skip(',');
        }
    }
    skip(')');

    /* 生成调用指令 */
    gfunc_call(nb_args);

    /* 处理返回值 */
    if (ret_nregs == 0) {
        vset(&ret.type, ret.r, ret.c.i);
        vtop->sym = NULL;
    } else {
        /* 从返回寄存器中取值 */
        vset(&ret.type, VT_CONST, 0);
        PUT_R_RET(vtop, ret.type.t);
    }
}
```

函数调用的关键步骤：

1. **解析函数表达式**：可能是标识符（直接调用）或指针（间接调用）。
2. **处理结构体返回**：如果函数返回结构体，需要在栈上分配空间并传递隐式指针。
3. **解析实参**：逐个解析参数表达式，调用 `gfunc_param_typed()` 进行类型检查和隐式转换（如 `float` 提升为 `double`）。
4. **生成调用**：`gfunc_call()` 将参数加载到正确的寄存器或栈位置，然后生成 `call` 指令。
5. **处理返回值**：将返回寄存器的值压入值栈。

### 4.5.6 赋值与逗号表达式

`expr_eq()` 处理赋值（`tccgen.c` 第 6733 行）：

```c
static void expr_eq(void)
{
    int t;
    expr_cond();
    if ((t = tok) == '=' || TOK_ASSIGN(t)) {
        test_lvalue();
        next();
        if (t == '=') {
            expr_eq();              /* 右递归：右结合 */
        } else {
            vdup();                 /* 复制左值 */
            expr_eq();
            gen_op(TOK_ASSIGN_OP(t)); /* 复合赋值：如 += 变成 + */
        }
        vstore();                   /* 存储到左值 */
    }
}
```

赋值是右结合的（`a = b = c` 等价于 `a = (b = c)`），所以右边用 `expr_eq()` 递归而非 `expr_cond()`。

`gexpr()` 处理逗号表达式：

```c
ST_FUNC void gexpr(void)
{
    expr_eq();
    if (tok == ',') {
        do {
            vpop();      /* 丢弃前一个表达式的值 */
            next();
            expr_eq();
        } while (tok == ',');
    }
}
```

逗号运算符的实现极其简洁：左侧表达式求值后直接丢弃（`vpop`），只保留最后一个表达式的值。

## 4.6 语句解析：block()

`block()` 函数是语句解析的核心（`tccgen.c` 第 7172 行），它通过一个大型的 `if-else if` 链处理所有 C 语句类型。我们逐一分析。

### 4.6.1 if/else 语句

```c
if (t == TOK_IF) {
    new_scope_s(&o);
    skip('(');
    gexpr_decl();                /* 解析条件（支持 C2y 声明） */
    a = gvtst(1, 0);            /* 条件为假时跳转，a = 待补丁位置 */
    skip(')');
    block(0);                    /* then 分支 */
    if (tok == TOK_ELSE) {
        d = gjmp(0);            /* 跳过 else 分支，d = 待补丁位置 */
        gsym(a);                 /* 补丁：假跳转目标 = 此处 */
        next();
        block(0);                /* else 分支 */
        gsym(d);                 /* 补丁：跳过 else 的跳转目标 = 此处 */
    } else {
        gsym(a);                 /* 补丁：假跳转目标 = 此处 */
    }
    prev_scope_s(&o);
}
```

生成的代码结构：

```
    [条件求值]
    jz .L_then_end      ; a: 条件为假跳转
    [then 分支代码]
    jmp .L_if_end        ; d: 跳过 else
.L_then_end:            ; gsym(a) 补丁
    [else 分支代码]
.L_if_end:              ; gsym(d) 补丁
```

`gvtst(inv, t)` 是一个关键的辅助函数。当 `inv=1` 时，它在条件为假时跳转；当 `inv=0` 时，在条件为真时跳转。它能智能地利用比较指令的条件码（`VT_CMP`）和已有的跳转链（`VT_JMP`/`VT_JMPI`），避免冗余的比较和跳转。

### 4.6.2 while 循环

```c
} else if (t == TOK_WHILE) {
    new_scope_s(&o);
    d = gind();                  /* 循环起始位置（回边目标） */
    skip('(');
    gexpr();                     /* 条件表达式 */
    a = gvtst(1, 0);            /* 条件为假时跳出循环 */
    skip(')');
    b = 0;                       /* break 链初始化 */
    lblock(&a, &b);              /* 循环体，设置 bsym/csym */
    gjmp_addr(d);                /* 无条件跳回循环顶部 */
    gsym_addr(b, d);             /* break 跳转目标 = 循环之后 */
    gsym(a);                     /* 条件假跳转目标 = 循环之后 */
    prev_scope_s(&o);
}
```

`lblock()` 设置循环的 `bsym`（break 跳转链）和 `csym`（continue 跳转链）：

```c
static void lblock(int *bsym, int *csym)
{
    struct scope *lo = loop_scope, *co = cur_scope;
    int *b = co->bsym, *c = co->csym;
    if (csym) {
        co->csym = csym;
        loop_scope = co;
    }
    co->bsym = bsym;
    block(0);
    co->bsym = b;
    if (csym) {
        co->csym = c;
        loop_scope = lo;
    }
}
```

while 循环中 `csym` 为 NULL（continue 也跳到循环顶部，由 `gjmp_addr(d)` 处理），而 `do-while` 和 `for` 循环需要显式的 `csym`。

### 4.6.3 do-while 循环

```c
} else if (t == TOK_DO) {
    new_scope_s(&o);
    a = b = 0;
    d = gind();                  /* 循环体起始位置 */
    lblock(&a, &b);              /* 循环体 */
    gsym(b);                     /* continue 目标 = 条件求值处 */
    skip(TOK_WHILE);
    skip('(');
    gexpr();                     /* 条件表达式 */
    c = gvtst(0, 0);            /* 条件为真时跳回循环顶部 */
    skip(')');
    skip(';');
    gsym_addr(c, d);             /* 补丁：真跳转目标 = 循环体起始 */
    gsym(a);                     /* break 目标 = 循环之后 */
    prev_scope_s(&o);
}
```

注意 do-while 的 `gvtst(0, 0)`（`inv=0`）：条件为真时跳回，与 while 的 `gvtst(1, 0)`（条件为假时跳出）相反。

### 4.6.4 for 循环

```c
} else if (t == TOK_FOR) {
    new_scope(&o);               /* 注意：用 new_scope 而非 new_scope_s */

    skip('(');
    if (tok != ';') {
        /* C99: for 循环初始化可以是声明 */
        if (!decl(VT_JMP)) {
            gexpr();             /* 普通初始化表达式 */
            vpop();
        }
    }
    skip(';');
    a = b = 0;
    c = d = gind();              /* 条件检查位置 */
    if (tok != ';') {
        gexpr();
        a = gvtst(1, 0);        /* 条件为假时跳出 */
    }
    skip(';');
    if (tok != ')') {
        e = gjmp(0);            /* 跳到循环体 */
        d = gind();              /* 自增表达式位置 */
        gexpr();
        vpop();
        gjmp_addr(c);            /* 跳回条件检查 */
        gsym(e);                 /* 补丁：跳到循环体 */
    }
    skip(')');
    lblock(&a, &b);              /* 循环体 */
    gjmp_addr(d);                /* 跳到自增表达式（或条件检查） */
    gsym_addr(b, d);             /* continue 目标 = 自增表达式 */
    gsym(a);                     /* break 目标 */
    prev_scope(&o, 0);
}
```

for 循环使用 `new_scope`（而非 `new_scope_s`），因为 C99 允许在初始化部分声明变量。`decl(VT_JMP)` 尝试解析声明；如果成功（返回非零），声明的变量在 for 循环的作用域内有效，离开 `prev_scope` 时自动弹出。

生成的代码结构：

```
    [初始化]
L_cond:                         ; c: 条件检查位置
    [条件求值]
    jz .L_end                   ; a: 条件为假跳转
    jmp .L_body                 ; e: 跳到循环体
L_incr:                         ; d: 自增表达式位置
    [自增表达式]
    jmp L_cond                  ; 跳回条件检查
L_body:                         ; gsym(e) 补丁
    [循环体]
    jmp L_incr                  ; 跳到自增
L_end:                          ; gsym(a) 补丁
```

### 4.6.5 switch 语句

switch 语句的实现是 `block()` 中最复杂的部分。tcc 使用排序 + 二分查找来生成 case 分支的跳转表。

```c
} else if (t == TOK_SWITCH) {
    struct switch_t *sw;
    sw = tcc_mallocz(sizeof *sw);
    sw->bsym = &a;
    sw->scope = cur_scope;
    sw->prev = cur_switch;
    cur_switch = sw;             /* 链入 switch 栈 */

    new_scope_s(&o);
    skip('(');
    gexpr_decl();                /* switch 表达式 */
    skip(')');
    sw->sv = *vtop--;           /* 保存 switch 值 */
    a = 0;
    b = gjmp(0);                /* 跳到第一个 case */
    lblock(&a, NULL);            /* switch 体（收集 case 标签） */
    a = gjmp(a);                 /* 隐式 break */

    gsym(b);                     /* 跳到 case 查找 */
    prev_scope_s(&o);

    /* 对 case 值排序 */
    case_sort(sw);

    /* 生成二分查找代码 */
    vpushv(&sw->sv);
    gv(RC_INT);                  /* 将 switch 值加载到寄存器 */
    d = gcase(sw->p, sw->n, 0); /* 二分查找 */
    vpop();

    if (sw->def_sym)
        gsym_addr(d, sw->def_sym);  /* 有 default：跳到 default */
    else
        gsym(d);                     /* 无 default：跳出 switch */

    gsym(a);                     /* break 目标 */
    end_switch();
}
```

**case 收集**：每个 `case` 标签在解析时创建一个 `struct case_t`，添加到 `cur_switch->p` 数组中：

```c
} else if (t == TOK_CASE) {
    struct case_t *cr;
    cr = tcc_malloc(sizeof(struct case_t));
    dynarray_add(&cur_switch->p, &cur_switch->n, cr);
    cr->v1 = cr->v2 = value64(expr_const64(), t);
    if (tok == TOK_DOTS && gnu_ext) {
        next();
        cr->v2 = value64(expr_const64(), t);  /* GCC case 范围扩展 */
    }
    cr->ind = gind();            /* case 代码的起始位置 */
    skip(':');
}
```

**case 排序**（`case_sort()`）：按 case 值排序，检测重复值，合并相邻的连续 case：

```c
static void case_sort(struct switch_t *sw)
{
    qsort(sw->p, sw->n, sizeof *sw->p, case_cmp_qs);
    /* 合并相邻的连续 case（如 case 1: case 2: case 3:） */
    p = sw->p;
    while (p < sw->p + sw->n - 1) {
        if (p[0]->v2 + 1 == p[1]->v1 && p[0]->ind == p[1]->ind) {
            p[1]->v1 = p[0]->v1;  /* 合并为范围 */
            /* ... */
        }
    }
}
```

**二分查找代码生成**（`gcase()`）：递归地生成二分查找逻辑（`tccgen.c` 第 6933 行）：

```c
static int gcase(struct case_t **base, int len, int dsym)
{
    while (len) {
        l2 = len > 8 ? len/2 : 0;
        p = base[l2];
        if (l2 == 0 && p->v1 == p->v2) {
            /* 单个 case 值：直接比较 */
            gen_op(TOK_EQ);
            gsym_addr(gvtst(0, 0), p->ind);
        } else {
            /* case 范围：v1 <= x <= v2 */
            gen_op(TOK_GT);      /* > v2 则跳过 */
            /* ... */
            gen_op(TOK_GE);      /* >= v1 则跳入 */
            gsym_addr(gvtst(0, 0), p->ind);
            dsym = gcase(base, l2, dsym);  /* 递归处理左半部分 */
        }
        ++l2, base += l2, len -= l2;
    }
    return gjmp(dsym);
}
```

当 case 数量超过 8 个时使用二分查找，否则使用线性查找。这比生成跳转表简单得多，适合 tcc 追求简洁的设计哲学。

### 4.6.6 return 语句

```c
} else if (t == TOK_RETURN) {
    b = (func_vt.t & VT_BTYPE) != VT_VOID;
    if (tok != ';') {
        gexpr();
        if (b) {
            gen_assign_cast(&func_vt);  /* 转换为函数返回类型 */
        } else {
            if (vtop->type.t != VT_VOID)
                tcc_warning("void function returns a value");
            vtop--;
        }
    } else if (b && func_old && (func_vt.t & VT_BTYPE) == VT_INT) {
        vpushi(0);              /* 老式函数默认返回 0 */
    } else if (b) {
        tcc_warning("'return' with no value");
        b = 0;
    }
    leave_scope(root_scope);    /* 执行所有 cleanup */
    if (b)
        gfunc_return(&func_vt); /* 生成返回指令 */
    skip(';');
    if (tok != '}' || local_scope != 1)
        rsym = gjmp(rsym);      /* 跳到函数末尾（如果不是最后一条语句） */
    CODE_OFF();                 /* 关闭代码生成（不可达代码） */
}
```

`gfunc_return()` 将值栈顶的值加载到返回寄存器中。对于结构体返回值，处理方式因 ABI 而异。

`rsym` 是返回跳转链：所有 `return` 语句都跳到同一个位置（函数末尾），由 `gsym(rsym)` 在 `gen_function()` 中统一补丁。

### 4.6.7 break 与 continue

```c
} else if (t == TOK_BREAK) {
    if (!cur_scope->bsym)
        tcc_error("cannot break");
    if (cur_switch && cur_scope->bsym == cur_switch->bsym)
        leave_scope(cur_switch->scope);
    else
        leave_scope(loop_scope);
    *cur_scope->bsym = gjmp(*cur_scope->bsym);  /* 加入 break 跳转链 */
    skip(';');

} else if (t == TOK_CONTINUE) {
    if (!cur_scope->csym)
        tcc_error("cannot continue");
    leave_scope(loop_scope);
    *cur_scope->csym = gjmp(*cur_scope->csym);  /* 加入 continue 跳转链 */
    skip(';');
}
```

`bsym` 和 `csym` 是跳转链的头指针。每次 `break`/`continue` 生成一个前向跳转，跳转目标通过链表传递给外层的循环/switch 结构体，最终由 `gsym()` 统一补丁。

`leave_scope()` 负责在跳出前执行必要的清理工作（如 VLA 的栈恢复和 `__attribute__((cleanup))` 函数调用）。

### 4.6.8 goto 语句与标签

```c
} else if (t == TOK_GOTO) {
    vla_restore(cur_scope->vla.locorig);
    if (tok == '*' && gnu_ext) {
        /* 计算 goto（GCC 扩展） */
        next(); gexpr();
        ggoto();
    } else if (tok >= TOK_UIDENT) {
        s = label_find(tok);
        if (!s)
            s = label_push(&global_label_stack, tok, LABEL_FORWARD);
        else if (s->r == LABEL_DECLARED)
            s->r = LABEL_FORWARD;

        if (s->r & LABEL_FORWARD) {
            /* 前向引用：加入跳转链 */
            s->jnext = gjmp(s->jnext);
        } else {
            /* 后向引用：直接跳到已知位置 */
            gjmp_addr(s->jind);
        }
        next();
    }
    skip(';');
```

标签的定义：

```c
if (tok == ':' && t >= TOK_UIDENT) {
    next();
    s = label_find(t);
    if (s) {
        if (s->r == LABEL_DEFINED)
            tcc_error("duplicate label '%s'", get_tok_str(s->v, NULL));
        if (s->r == LABEL_FORWARD) {
            /* 补丁所有前向 goto */
            gsym(s->jnext);
            s->r = LABEL_DEFINED;
        }
    } else {
        s = label_push(&global_label_stack, t, LABEL_DEFINED);
    }
    s->jind = gind();  /* 记录标签位置 */
}
```

标签管理使用 `LABEL_DEFINED`、`LABEL_FORWARD`、`LABEL_DECLARED` 三种状态。前向 goto 创建一个跳转链（通过 `jnext` 字段），标签定义时统一补丁。这种前向引用 + 延迟补丁的模式在编译器中极为常见。

## 4.7 声明解析

声明解析是 C 语言编译器中最复杂的部分之一，因为 C 的声明语法天然地将类型信息嵌入在变量名周围。tcc 的声明解析分为三个层次：`decl()` -> `parse_btype()` + `type_decl()`。

### 4.7.1 decl()：顶层声明解析

`decl()` 是声明解析的入口（`tccgen.c` 第 8733 行），参数 `l` 指定声明的上下文：

- `VT_CONST`：全局作用域（文件作用域）
- `VT_LOCAL`：局部作用域（函数体内）
- `VT_JMP`：for 循环初始化（C99 声明）
- `VT_CMP`：旧式函数参数声明

```c
static int decl(int l)
{
    int v, has_init, r, oldint;
    CType type, btype;
    Sym *sym;
    AttributeDef ad, adbase;

    while (1) {
        oldint = 0;
        if (!parse_btype(&btype, &adbase, l == VT_LOCAL)) {
            if (l == VT_JMP) return 0;     /* for 循环中无声明 */
            if (tok == ';' && l != VT_CMP) {
                next(); continue;           /* 跳过空声明 */
            }
            if (l != VT_CONST) break;       /* 局部区域：不是声明 */
            if (tok >= TOK_UIDENT) {
                btype.t = VT_INT;           /* K&R 隐式 int */
                oldint = 1;
            } else {
                break;
            }
        }

        /* 解析声明中的每个变量 */
        while (1) {
            type = btype;
            ad = adbase;
            type_decl(&type, &ad, &v, ...); /* 解析声明符 */

            if ((type.t & VT_BTYPE) == VT_FUNC) {
                /* 函数声明或定义 */
                if (tok == '{') {
                    /* 函数定义 */
                    sym = external_sym(v, &type, 0, &ad);
                    if (sym->type.t & VT_INLINE) {
                        /* inline 函数：保存记号流，稍后按需生成 */
                        skip_or_save_block(&fn->func_str);
                    } else {
                        cur_text_section = ...;
                        gen_function(sym);
                    }
                    break;
                }
            }

            if (type.t & VT_TYPEDEF) {
                /* typedef */
                sym = sym_push(v, &type, 0, 0);
            } else if ((type.t & VT_EXTERN) || ...) {
                /* 外部声明 */
                type.t |= VT_EXTERN;
                external_sym(v, &type, r, &ad);
            } else {
                /* 变量定义（可能有初始化器） */
                decl_initializer_alloc(&type, &ad, r, has_init, v, l);
            }

            if (tok != ',') {
                skip(';');
                break;
            }
            next();  /* 逗号：继续下一个声明符 */
        }
    }
    return 0;
}
```

`decl()` 的主循环不断尝试解析声明，直到遇到非声明内容（如语句或文件结束）。每个声明可能包含多个用逗号分隔的声明符（如 `int a, *p, arr[10];`）。

### 4.7.2 parse_btype()：基本类型解析

`parse_btype()` 用一个 `while(1)` 循环配合 `switch` 语句来解析类型说明符和限定符（`tccgen.c` 第 4719 行）。这个循环之所以必要，是因为 C 语言允许类型说明符以任意顺序出现：

```c
unsigned long long int x;   /* 合法 */
long int unsigned long y;   /* 也合法（虽然不推荐） */
const static volatile int z; /* 限定符和存储类可以交错 */
```

`parse_btype()` 的核心逻辑：

```c
static int parse_btype(CType *type, AttributeDef *ad, int ignore_label)
{
    int t, u, bt, st, type_found, typespec_found;
    type_found = 0;
    t = VT_INT;           /* 默认类型 */
    bt = st = -1;         /* bt: 基本类型，st: short/long */
    type->ref = NULL;

    while(1) {
        switch(tok) {
        /* 基本类型 */
        case TOK_CHAR:
            u = VT_BYTE;
        basic_type:
            next();
        basic_type1:
            if (u == VT_SHORT || u == VT_LONG) {
                if (st != -1 || (bt != -1 && bt != VT_INT))
                    tcc_error("too many basic types");
                st = u;
            } else {
                if (bt != -1 || (st != -1 && u != VT_INT))
                    tcc_error("too many basic types");
                bt = u;
            }
            if (u != VT_INT)
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            typespec_found = 1;
            break;

        case TOK_LONG:
            if ((t & VT_BTYPE) == VT_DOUBLE) {
                /* long double */
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                /* long long */
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LLONG;
            } else {
                u = VT_LONG;
                goto basic_type;
            }
            next();
            break;

        case TOK_INT:    u = VT_INT;   goto basic_type;
        case TOK_SHORT:  u = VT_SHORT; goto basic_type;
        case TOK_VOID:   u = VT_VOID;  goto basic_type;
        case TOK_FLOAT:  u = VT_FLOAT; goto basic_type;
        case TOK_DOUBLE:
            if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else {
                u = VT_DOUBLE;
                goto basic_type;
            }
            next();
            break;

        /* 结构体/联合体/枚举 */
        case TOK_STRUCT:
            struct_decl(&type1, VT_STRUCT);
            goto basic_type2;
        case TOK_UNION:
            struct_decl(&type1, VT_UNION);
            goto basic_type2;
        case TOK_ENUM:
            struct_decl(&type1, VT_ENUM);
        basic_type2:
            u = type1.t;
            type->ref = type1.ref;
            goto basic_type1;

        /* 类型限定符 */
        case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
            type->t = t;
            parse_btype_qualify(type, VT_CONSTANT);
            t = type->t;
            next();
            break;
        case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
            type->t = t;
            parse_btype_qualify(type, VT_VOLATILE);
            t = type->t;
            next();
            break;

        /* 符号性 */
        case TOK_SIGNED1: case TOK_SIGNED2: case TOK_SIGNED3:
            t |= VT_DEFSIGN;
            next(); typespec_found = 1; break;
        case TOK_UNSIGNED:
            t |= VT_DEFSIGN | VT_UNSIGNED;
            next(); typespec_found = 1; break;

        /* 存储类 */
        case TOK_EXTERN:  g = VT_EXTERN;  goto storage;
        case TOK_STATIC:  g = VT_STATIC;  goto storage;
        case TOK_TYPEDEF: g = VT_TYPEDEF; goto storage;
        storage:
            t |= g;
            next(); break;

        case TOK_INLINE1: case TOK_INLINE2: case TOK_INLINE3:
            t |= VT_INLINE;
            next(); break;

        /* 属性 */
        case TOK_ATTRIBUTE1: case TOK_ATTRIBUTE2:
            parse_attribute(ad);
            break;

        default:
            goto the_end;
        }
        type_found = 1;
    }
the_end:
    type->t = t;
    return type_found;
}
```

`long` 的处理特别精巧：

- 第一次遇到 `long`：设 `st = VT_LONG`，`t` 包含 `VT_LONG` 位。
- 如果后面跟 `long`（即 `long long`）：清除 `VT_LONG` 位，设置 `VT_LLONG`。
- 如果后面跟 `double`（即 `long double`）：设置 `VT_LDOUBLE`。

`parse_btype_qualify()` 函数处理限定符的嵌套应用——当类型已经是数组时，限定符需要穿透到元素类型：

```c
static void parse_btype_qualify(CType *type, int qualifiers)
{
    while (type->t & VT_ARRAY) {
        type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        type = &type->ref->type;
    }
    type->t |= qualifiers;
}
```

### 4.7.3 type_decl()：声明符解析

`type_decl()` 解析 C 声明符（declarator）——即变量名周围的 `*`、`()`、`[]` 等（`tccgen.c` 第 5243 行）。

C 声明符的解析遵循"螺旋规则"（spiral rule），从变量名开始，先右后左地解析。tcc 的实现通过递归调用来处理嵌套：

```c
static CType *type_decl(CType *type, AttributeDef *ad, int *v, int td)
{
    CType *post, *ret;
    int qualifiers, storage;

    storage = type->t & VT_STORAGE;
    type->t &= ~VT_STORAGE;
    post = ret = type;

    /* 解析指针前缀 */
    while (tok == '*') {
        qualifiers = 0;
    redo:
        next();
        switch(tok) {
        case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
            qualifiers |= VT_CONSTANT; goto redo;
        case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
            qualifiers |= VT_VOLATILE; goto redo;
        case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
            goto redo;
        }
        mk_pointer(type);
        type->t |= qualifiers;
        if (ret == type)
            ret = pointed_type(type);  /* 记录最内层类型 */
    }

    /* 解析嵌套声明符或函数参数 */
    if (tok == '(') {
        if (!post_type(type, ad, 0, td)) {
            /* 不是函数参数列表，是嵌套括号 */
            parse_attribute(ad);
            post = type_decl(type, ad, v, td);  /* 递归 */
            skip(')');
        } else
            goto abstract;
    } else if (tok >= TOK_IDENT && (td & TYPE_DIRECT)) {
        *v = tok;     /* 变量名 */
        next();
    } else {
    abstract:
        if (!(td & TYPE_ABSTRACT))
            expect("identifier");
        *v = 0;       /* 抽象声明符（无名） */
    }

    /* 解析后缀：数组和函数参数 */
    post_type(post, ad, ...);
    parse_attribute(ad);
    type->t |= storage;
    return ret;
}
```

**解析 `int (*callback)(double)` 的过程：**

1. `parse_btype` 识别 `int`，`type->t = VT_INT`。
2. `type_decl` 进入，看到 `*`，调用 `mk_pointer`：`type->t = VT_PTR`，`ref->type = VT_INT`。
3. 看到 `(`，调用 `post_type` 返回 0（因为是 `(callback)` 而非参数列表）。
4. 递归调用 `type_decl`，看到 `callback`，`*v = 'callback'`。
5. 跳过 `)`。
6. 外层 `post_type` 处理 `(double)`：创建函数参数 Sym，`type->t = VT_FUNC`，`ref->next` 指向参数 `double`。
7. 最终返回最内层类型指针 `ret`。

### 4.7.4 post_type()：后缀类型解析

`post_type()` 处理函数参数列表和数组维度（`tccgen.c` 第 5026 行）：

```c
static int post_type(CType *type, AttributeDef *ad, int storage, int td)
{
    if (tok == '(') {
        /* 函数参数列表 */
        next();
        /* ... 解析参数列表 ... */
        /* 创建匿名 Sym 作为函数原型 */
        sr = sym_push2(ps, SYM_FIELD, 0, 0);
        /* ... 解析每个参数 ... */
        sr->type = *type;
        sr->f = ad->f;
        sr->next = first;       /* 参数链 */
        type->t = VT_FUNC;
        type->ref = sr;
        /* ... */
    } else if (tok == '[') {
        /* 数组维度 */
        next();
        if (tok != ']') {
            n = expr_const();   /* 全局：常量表达式 */
            /* 或 gexpr()       局部：可能有 VLA */
        }
        skip(']');
        post_type(type, ad, storage, ...);  /* 递归处理多维数组 */

        s = sym_push(SYM_FIELD, type, 0, n);
        type->t = VT_ARRAY | VT_PTR;
        type->ref = s;
    }
}
```

对于多维数组 `int a[3][5]`，`post_type` 递归处理：

1. 外层 `type_decl` 看到 `a[3]`：创建 `Sym{c=3, type=...}`，`type->t = VT_ARRAY|VT_PTR`。
2. 递归的 `post_type` 看到 `[5]`：在已有的数组类型上再包一层，创建 `Sym{c=5, type=VT_INT}`。
3. 最终结构：`VT_ARRAY|VT_PTR` -> `Sym{c=3}` -> `type = VT_ARRAY|VT_PTR` -> `Sym{c=5}` -> `type = VT_INT`。

### 4.7.5 gen_function()：函数定义的代码生成

当 `decl()` 遇到函数定义（`tok == '{'`）时，调用 `gen_function()` 生成函数体的代码（`tccgen.c` 第 8570 行）：

```c
static void gen_function(Sym *sym)
{
    struct scope f = { 0 };
    cur_scope = root_scope = &f;
    nocode_wanted = 0;                    /* 开启代码生成 */

    ind = cur_text_section->data_offset;
    funcname = get_tok_str(sym->v, NULL);
    func_ind = ind;
    func_vt = sym->type.ref->type;       /* 返回类型 */
    func_var = sym->type.ref->f.func_type == FUNC_ELLIPSIS;
    func_old = sym->type.ref->f.func_type == FUNC_OLD;

    put_extern_sym(sym, cur_text_section, ind, 0);

    tcc_debug_funcstart(tcc_state, sym);

    /* 初始化局部符号栈 */
    sym_push2(&local_stack, SYM_FIELD, 0, 0);  /* 哨兵 */
    local_scope = 1;
    sym_push_params(sym->type.ref);       /* 将参数压入局部栈 */

    local_scope = 0;
    rsym = 0;                             /* 返回跳转链初始化 */

    gfunc_prolog(sym);                    /* 生成函数序言（栈帧建立） */
    tcc_debug_prolog_epilog(tcc_state, 0);
    func_vla_arg(sym);                    /* 处理 VLA 参数 */
    block(0);                             /* 解析函数体 */
    gsym(rsym);                           /* 补丁所有 return 跳转 */
    nocode_wanted = 0;
    gfunc_epilog();                       /* 生成函数尾声（栈帧销毁） */

    tcc_debug_funcend(tcc_state, ind - func_ind);

    elfsym(sym)->st_size = ind - func_ind; /* 记录函数大小 */
    cur_text_section->data_offset = ind;

    sym_pop(&local_stack, NULL, 0);       /* 清理局部符号栈 */
    label_pop(&global_label_stack, NULL, 0);
    local_scope = 0;

    cur_text_section = NULL;
    funcname = "";
    nocode_wanted = DATA_ONLY_WANTED;     /* 恢复为全局模式 */
    check_vstack();                       /* 检查值栈平衡 */
    next();
}
```

函数编译的完整流程：

1. **初始化**：设置 `funcname`、`func_vt`（返回类型）、`func_var`（是否变参）等全局状态。
2. **参数入栈**：`sym_push_params()` 将函数参数从原型的 Sym 链表压入 `local_stack`。
3. **函数序言**：`gfunc_prolog()` 生成 `push %rbp; mov %rsp, %rbp; sub $N, %rsp` 等指令。
4. **解析函数体**：`block(0)` 递归解析所有语句和局部声明。
5. **补丁 return**：`gsym(rsym)` 将所有 `return` 语句的跳转目标补丁到此处。
6. **函数尾声**：`gfunc_epilog()` 生成 `leave; ret` 指令。
7. **清理**：弹出所有局部符号，恢复全局状态。

## 4.8 本章小结与练习

### 本章小结

本章深入分析了 TinyCC 的语法分析器和类型系统，揭示了以下关键设计决策：

1. **单遍架构**：tcc 不构建 AST，而是在解析的同时直接生成目标代码。这种设计牺牲了优化能力，但换来了极快的编译速度和极低的内存消耗。

2. **值栈机制**：`SValue` 栈（`vstack`）替代了 AST 作为表达式的中间表示。每个表达式操作都是对栈顶值的变换。

3. **位域类型编码**：`CType.t` 用一个 32 位整数编码了基本类型、类型修饰符、存储类等所有类型信息，使得类型操作可以用高效的位运算完成。

4. **记号表双向链接**：`TokenSym` 中的 `sym_identifier`/`sym_struct` 指针与 `Sym` 中的 `prev_tok` 链接形成双向结构，实现了 O(1) 的符号查找和自动的名称遮蔽/恢复。

5. **前向引用与延迟补丁**：跳转指令的目标在生成时未知，通过链表（`rsym`、`bsym`、`csym`、`jnext`）记录待补丁位置，在目标确定后统一补丁。

6. **优先级爬升**：表达式解析使用优先级爬升算法，通过单个 `expr_infix()` 函数替代了传统递归下降的一系列函数，代码更紧凑。

7. **声明解析的三层次**：`decl()` -> `parse_btype()`（类型说明符）+ `type_decl()`（声明符），清晰地分离了"什么类型"和"叫什么名字"两个问题。

### 练习

**练习 4.1**（类型位域编码）：给定以下类型声明，写出 tcc 中 `CType.t` 的十六进制值（假设 `int` 为 32 位，`long` 为 64 位系统）。提示：参考 `tcc.h` 中的 `VT_*` 宏定义。详见 `exercises/ex1_types.md`。

**练习 4.2**（复杂声明解析）：追踪 `static const char *(* const arr[3])(int, ...)` 的解析过程，画出 Sym 链表结构。详见 `exercises/ex2_parse.md`。

**练习 4.3**（符号表变化追踪）：给定一段包含嵌套作用域的 C 代码，追踪每一步 `sym_push`/`sym_pop`/`sym_link` 操作后 `local_stack` 和 `table_ident[x]->sym_identifier` 的状态。详见 `exercises/ex3_scope.md`。

---

> **进一步阅读**
>
> - Aho, Lam, Sethi, Ullman. *Compilers: Principles, Techniques, and Tools* (2nd ed.), Chapter 4: Syntax Analysis.
> - Fabrice Bellard. "TCC: Tiny C Compiler". https://bellard.org/tcc/
> - tcc 源码 `tccgen.c` 中的 `#define precedence_parser` 及相关代码。
> - cdecl.org — 交互式 C 声明解析器，有助于理解复杂的声明语法。
