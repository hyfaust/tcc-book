# 练习 4.1：类型位域编码

## 目标

掌握 tcc 中 `CType.t` 的位域编码方式，能够将任意 C 类型声明翻译为 tcc 内部的十六进制表示。

## 参考：VT_* 宏定义

以下是 `tcc.h` 中的关键宏定义（假设 `int` 为 32 位，64 位系统 `long` 为 8 字节）：

### 基本类型（bit 0-3，掩码 `VT_BTYPE = 0x000f`）

| 宏名 | 值 | 含义 |
|------|-----|------|
| `VT_VOID` | 0 | void |
| `VT_BYTE` | 1 | signed char |
| `VT_SHORT` | 2 | short |
| `VT_INT` | 3 | int |
| `VT_LLONG` | 4 | long long |
| `VT_PTR` | 5 | 指针 |
| `VT_FUNC` | 6 | 函数 |
| `VT_STRUCT` | 7 | struct/union |
| `VT_FLOAT` | 8 | float |
| `VT_DOUBLE` | 9 | double |
| `VT_LDOUBLE` | 10 | long double |
| `VT_BOOL` | 11 | _Bool |

### 类型修饰符（bit 4-11）

| 宏名 | 值 | 含义 |
|------|-----|------|
| `VT_UNSIGNED` | `0x0010` | unsigned |
| `VT_DEFSIGN` | `0x0020` | 显式 signed/unsigned |
| `VT_ARRAY` | `0x0040` | 数组 |
| `VT_BITFIELD` | `0x0080` | 位域 |
| `VT_CONSTANT` | `0x0100` | const |
| `VT_VOLATILE` | `0x0200` | volatile |
| `VT_VLA` | `0x0400` | 变长数组 |
| `VT_LONG` | `0x0800` | long 修饰符 |

### 存储类（bit 12-16）

| 宏名 | 值 | 含义 |
|------|-----|------|
| `VT_EXTERN` | `0x1000` | extern |
| `VT_STATIC` | `0x2000` | static |
| `VT_TYPEDEF` | `0x4000` | typedef |
| `VT_INLINE` | `0x8000` | inline |
| `VT_TLS` | `0x10000` | _Thread_local |

---

## 题目

### A 部分：基本类型编码

对于以下声明，写出变量 `v` 的 `CType.t` 的十六进制值。只需写出 `t` 的值，不需要考虑 `ref`。

1. `int x;`
2. `unsigned int x;`
3. `long x;`
4. `unsigned long long x;`
5. `char x;`
6. `signed char x;`
7. `unsigned char x;`
8. `short x;`
9. `float x;`
10. `double x;`
11. `long double x;`
12. `_Bool x;`

### B 部分：类型修饰符

13. `const int x;`
14. `volatile unsigned char x;`
15. `const volatile int *p;`（写出指针所指向的类型的 `t`）
16. `static int x;`
17. `extern int x;`
18. `typedef int my_type;`

### C 部分：复合类型

对于以下声明，写出变量的 `CType.t` 值。如果涉及 `ref`，用 `->` 表示指向的 `Sym` 的 `type.t`。

19. `int *p;` — 写出 `p` 的 `t` 和 `p->ref->type.t`
20. `const int *p;` — 写出 `p` 的 `t` 和 `p->ref->type.t`
21. `int *const p;` — 写出 `p` 的 `t` 和 `p->ref->type.t`
22. `int arr[10];` — 写出 `arr` 的 `t` 和 `arr->ref->type.t` 和 `arr->ref->c`
23. `char *argv[];` — 写出 `argv` 的 `t` 和 `argv->ref->type.t`（注意：`argv->ref->type.t` 本身也是一个 `VT_PTR`）

### D 部分：挑战题

24. 给定以下声明，写出完整的类型链（从变量到最内层类型）：

```c
const int * const * volatile pp;
```

25. 给定以下位域声明，写出各成员的 `t` 值（包含 `VT_BITFIELD` 和位域编码信息）：

```c
struct {
    unsigned int flag : 1;
    int value : 12;
};
```

---

## 参考答案

### A 部分

| # | 声明 | `t` 的十六进制 | 计算过程 |
|---|------|---------------|----------|
| 1 | `int x` | `0x0003` | `VT_INT` = 3 |
| 2 | `unsigned int x` | `0x0033` | `VT_INT \| VT_UNSIGNED \| VT_DEFSIGN` = 3 \| 0x10 \| 0x20 |
| 3 | `long x` (64-bit) | `0x0807` | `VT_INT \| VT_LONG` = 3 \| 0x800, 但注意 64 位系统 long 是 `VT_LLONG\|VT_LONG` = 4 \| 0x800 = `0x0804`。实际上 tcc 中 long 在 64 位 Linux 上编码为 `VT_LLONG\|VT_LONG` |

**注意**：3 题和 4 题的答案取决于目标平台。在 64 位 Linux 上 `LONG_SIZE == 8`，`long` 编码为 `VT_LLONG | VT_LONG` = `0x0804`。在 32 位系统上为 `VT_INT | VT_LONG` = `0x0803`。

详细答案请参考 `tcc.h` 中 `VT_*` 定义以及 `parse_btype()` 中对 `TOK_LONG` 的处理逻辑。

### B 部分提示

- `const int x;` → `VT_INT | VT_CONSTANT` = `0x0103`
- `static int x;` → `VT_INT | VT_STATIC` = `0x2003`
- `typedef int my_type;` → `VT_INT | VT_TYPEDEF` = `0x4003`

### C 部分提示

- `int *p;` → `t = VT_PTR` = `0x0005`，`ref->type.t = VT_INT` = `0x0003`
- `const int *p;` → `t = VT_PTR` = `0x0005`，`ref->type.t = VT_INT | VT_CONSTANT` = `0x0103`
- `int *const p;` → `t = VT_PTR | VT_CONSTANT` = `0x0105`，`ref->type.t = VT_INT` = `0x0003`
- `int arr[10];` → `t = VT_ARRAY | VT_PTR` = `0x0045`，`ref->type.t = VT_INT` = `0x0003`，`ref->c = 10`

---

## 思考题

1. 为什么 tcc 将所有指针类型共用 `VT_PTR`，而不是为每种指针分配不同的基本类型编号？

2. tcc 中 `VT_DEFSIGN` 位的作用是什么？为什么不能只用 `VT_UNSIGNED` 来区分 signed 和 unsigned？

3. 数组类型同时设置 `VT_ARRAY` 和 `VT_PTR` 的设计有什么好处？这如何简化了数组到指针的退化（array-to-pointer decay）操作？
