# 练习 4.2：复杂声明解析追踪

## 目标

通过手动追踪 tcc 的 `parse_btype()` 和 `type_decl()` 函数的执行过程，理解 C 声明的解析机制，并画出最终的 `Sym` 链表结构。

## 背景

C 语言的声明语法遵循"声明模仿使用"（declaration mimics use）的原则。tcc 的声明解析分为两步：

1. `parse_btype()`：解析类型说明符（如 `static const int`）
2. `type_decl()`：解析声明符（如 `*arr[3]`），包括指针、数组、函数参数

`type_decl()` 的关键规则：
- 遇到 `*`：调用 `mk_pointer(type)`，在类型外面包裹一层指针
- 遇到 `(`：可能是嵌套声明符（递归调用 `type_decl`）或函数参数列表（`post_type`）
- 遇到标识符：记录变量名
- 遇到 `[`：`post_type` 处理数组维度，递归处理多维
- 遇到 `(` 参数列表 `)`：`post_type` 创建函数原型 Sym

---

## 题目

### 题目 1：函数指针数组

追踪以下声明的解析过程：

```c
int (*handlers[5])(double, int);
```

**要求**：
1. 写出 `parse_btype()` 的执行步骤（识别了哪些记号，`t` 的最终值）
2. 写出 `type_decl()` 的执行步骤（每一步遇到的记号和执行的操作）
3. 画出最终的 `CType` + `Sym` 链表结构（从 `handlers` 变量开始）

**提示**：解析顺序
```
parse_btype: 'int' → t = VT_INT

type_decl:
  无 * 前缀
  遇到 '(' → 尝试 post_type 失败（因为是嵌套声明符）
  递归 type_decl:
    无 * 前缀
    遇到 '*' → mk_pointer: t = VT_PTR -> {VT_INT}
    遇到 'handlers' → v = 'handlers'
  跳过 ')'
  遇到 '[' → post_type:
    解析 '5' → n = 5
    跳过 ']'
    创建 Sym: c=5, type=VT_PTR->{VT_INT}
    type->t = VT_ARRAY|VT_PTR
  遇到 '(' → post_type:
    解析参数 'double' → Sym{type=VT_DOUBLE}
    跳过 ','
    解析参数 'int' → Sym{type=VT_INT}
    跳过 ')'
    创建函数原型 Sym: type=VT_INT, func_type=FUNC_NEW
    type->t = VT_FUNC
```

### 题目 2：const 指针的指针

追踪以下声明的解析过程：

```c
static const char *(* const table[4])(void);
```

**要求**：
1. 写出 `parse_btype()` 的完整执行过程
2. 写出 `type_decl()` 的完整执行过程
3. 画出最终的类型链结构

**提示**：注意 `* const` 中的 `const` 修饰的是指针本身（通过 `type_decl` 中的 `qualifiers` 收集），而 `const char` 中的 `const` 修饰的是被指向的类型（通过 `parse_btype` 设置）。

### 题目 3：变参函数指针

追踪以下声明的解析过程：

```c
int (* (*get_printer(void))(const char *, ...))(int);
```

这是一个函数 `get_printer`，无参数，返回一个函数指针，该函数指针指向一个变参函数（接受 `const char *` 和 `...`），该变参函数返回另一个函数指针（接受 `int`，返回 `int`）。

**要求**：
1. 分步写出解析过程
2. 画出完整的类型链
3. 用通俗语言描述这个类型

### 题目 4：简化练习

对于以下每个声明，直接画出最终的 `CType` + `Sym` 结构图：

(a) `double *p;`

(b) `int a[3][4];`

(c) `void (*callback)(int, void *);`

(d) `const struct { int x; int y; } *p;`

(e) `int (*ap[2])(char);`

---

## 解题模板

对于每个声明，请按以下格式记录解析过程：

```
=== parse_btype ===
遇到 'int':
  t = VT_INT (0x0003)
  type_found = 1
跳出循环

=== type_decl ===
遇到 '*':
  mk_pointer: 创建 Sym_A, type->t = VT_PTR (0x0005)
  Sym_A.type.t = VT_INT
  ret = pointed_type(type) → Sym_A

遇到 '(':
  post_type 返回 0（不是参数列表，是嵌套括号）
  递归 type_decl:
    遇到 'varname':
      *v = 'varname'
  跳过 ')'

遇到 '[':
  post_type:
    解析 'N' → n = N
    创建 Sym_B: c=N, type.t = VT_PTR (指向 Sym_A)
    type->t = VT_ARRAY | VT_PTR (0x0045)
    type->ref = Sym_B

=== 最终结构 ===
varname 的 CType:
  t = VT_ARRAY | VT_PTR (0x0045)
  ref = Sym_B { c=N, type.t = VT_ARRAY|VT_PTR -> ... }
```

---

## 思考题

1. 为什么 `type_decl()` 需要返回 `ret`（最内层类型的指针）？这个返回值在哪里被使用？

2. `type_decl()` 中 `post` 和 `ret` 两个指针的区别是什么？在什么情况下它们指向同一个 `CType`，什么情况下不同？

3. 解析 `int (*p)[10]` 和 `int *p[10]` 的区别在哪里？`()` 的存在如何改变了解析结果？

4. tcc 的 `post_type()` 如何区分"函数参数列表"和"嵌套括号声明符"？它使用了什么试探方法？
