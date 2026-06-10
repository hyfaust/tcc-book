# 练习 3：#pragma pack 与结构体布局

## 目标

通过实验理解 `#pragma pack` 对结构体内存布局的影响，并分析 TinyCC 中 pack 栈的实现机制。

## 背景

`#pragma pack` 控制结构体成员的对齐方式。TinyCC 使用一个栈（`pack_stack`）来管理嵌套的 pack 设置：

```c
/* TCCState 中的定义 */
int pack_stack[PACK_STACK_SIZE];
int *pack_stack_ptr;
```

对齐值必须是 1 到 16 之间的 2 的幂（或 0 表示默认对齐）。

## 实验代码

创建文件 `pack_demo.c`，内容如下：

```c
#include <stdio.h>
#include <stddef.h>

/* 默认对齐 */
struct DefaultAlign {
    char  a;    /* 1 byte */
    int   b;    /* 4 bytes */
    char  c;    /* 1 byte */
    short d;    /* 2 bytes */
};

/* pack(1)：无填充 */
#pragma pack(push, 1)
struct Pack1 {
    char  a;
    int   b;
    char  c;
    short d;
};
#pragma pack(pop)

/* pack(2)：2 字节对齐 */
#pragma pack(push, 2)
struct Pack2 {
    char  a;
    int   b;
    char  c;
    short d;
};
#pragma pack(pop)

/* pack(4)：4 字节对齐 */
#pragma pack(push, 4)
struct Pack4 {
    char  a;
    int   b;
    char  c;
    short d;
};
#pragma pack(pop)

/* 嵌套 pack 示例 */
#pragma pack(push, 1)
struct Outer {
    char x;
    #pragma pack(push, 4)
    struct Inner {
        char  a;
        int   b;
    } inner;
    #pragma pack(pop)
    char y;
};
#pragma pack(pop)

#define PRINT_OFFSET(type, member) \
    printf("  %-20s offset=%-3zu size=%zu\n", \
           #member, offsetof(type, member), sizeof(((type*)0)->member))

int main(void)
{
    printf("=== Structure Layout Analysis ===\n\n");

    printf("DefaultAlign (size=%zu):\n", sizeof(struct DefaultAlign));
    PRINT_OFFSET(struct DefaultAlign, a);
    PRINT_OFFSET(struct DefaultAlign, b);
    PRINT_OFFSET(struct DefaultAlign, c);
    PRINT_OFFSET(struct DefaultAlign, d);

    printf("\nPack1 (size=%zu):\n", sizeof(struct Pack1));
    PRINT_OFFSET(struct Pack1, a);
    PRINT_OFFSET(struct Pack1, b);
    PRINT_OFFSET(struct Pack1, c);
    PRINT_OFFSET(struct Pack1, d);

    printf("\nPack2 (size=%zu):\n", sizeof(struct Pack2));
    PRINT_OFFSET(struct Pack2, a);
    PRINT_OFFSET(struct Pack2, b);
    PRINT_OFFSET(struct Pack2, c);
    PRINT_OFFSET(struct Pack2, d);

    printf("\nPack4 (size=%zu):\n", sizeof(struct Pack4));
    PRINT_OFFSET(struct Pack4, a);
    PRINT_OFFSET(struct Pack4, b);
    PRINT_OFFSET(struct Pack4, c);
    PRINT_OFFSET(struct Pack4, d);

    printf("\nOuter with nested pack (size=%zu):\n", sizeof(struct Outer));
    PRINT_OFFSET(struct Outer, x);
    PRINT_OFFSET(struct Outer, inner);
    PRINT_OFFSET(struct Outer, inner.a);
    PRINT_OFFSET(struct Outer, inner.b);
    PRINT_OFFSET(struct Outer, y);

    return 0;
}
```

## 任务

### 任务 1：预测结构体布局

在编译和运行之前，手动计算每个结构体的大小和成员偏移量。画出内存布局图。

**DefaultAlign（默认对齐，通常 4 或 8 字节对齐）：**

```
偏移:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
内容:  [a][pad ][pad ][pad ][b             ][c][pad ][d    ][pad ]
```

请填写下表：

| 结构体 | a 的偏移 | b 的偏移 | c 的偏移 | d 的偏移 | 总大小 |
|--------|---------|---------|---------|---------|--------|
| DefaultAlign | 0 | | | | |
| Pack1 | 0 | | | | |
| Pack2 | 0 | | | | |
| Pack4 | 0 | | | | |

---

### 任务 2：编译运行

```bash
tcc -o pack_demo pack_demo.c && ./pack_demo
```

将实际输出与你的预测对比。如有差异，分析原因。

---

### 任务 3：pack 栈的行为分析

TinyCC 的 `#pragma pack` 使用栈管理。分析以下代码序列中 `pack_stack` 的状态变化：

```c
#pragma pack(push, 1)      /* 栈状态: [1] */
struct A { char a; int b; };
#pragma pack(push, 4)      /* 栈状态: [1, 4] */
struct B { char a; int b; };
#pragma pack(pop)           /* 栈状态: [1] */
struct C { char a; int b; };
#pragma pack(pop)           /* 栈状态: [] (默认) */
struct D { char a; int b; };
```

**问题：**

1. 每个 `#pragma pack(push, N)` 执行后，`pack_stack_ptr` 指向哪里？
2. `#pragma pack(pop)` 执行后，当前对齐值恢复为什么？
3. 如果 `pop` 的次数超过 `push` 的次数，会发生什么？（提示：查看 `pragma_parse()` 中的 `stk_error` 检查。）

---

### 任务 4：pack(push) 与 pack(push, N) 的区别

```c
/* 场景 A */
int current_align = 8;  /* 假设当前对齐为 8 */
#pragma pack(push)       /* 只压栈，不改变值 */
/* 此时对齐值是多少？ */

/* 场景 B */
#pragma pack(push, 2)    /* 压栈并设置为 2 */
/* 此时对齐值是多少？ */
```

分析 TinyCC 源码中 `pragma_parse()` 的实现，解释两者的区别。

---

### 任务 5：pack() 重置

```c
#pragma pack(2)
struct Packed { char a; int b; };
#pragma pack()             /* 重置为默认 */
struct Normal { char a; int b; };
```

**问题：**

1. `#pragma pack()`（无参数）在 TinyCC 中是如何实现的？
2. 它将对齐值设为什么？（提示：查看 `pragma_parse()` 中 `val` 的默认值。）

---

### 任务 6：嵌套 pack 与内部结构体

分析 `Outer` 结构体的布局：

```c
#pragma pack(push, 1)
struct Outer {
    char x;               /* 外层 pack(1) */
    #pragma pack(push, 4)
    struct Inner {
        char  a;          /* 内层 pack(4) */
        int   b;
    } inner;
    #pragma pack(pop)
    char y;               /* 恢复外层 pack(1) */
};
#pragma pack(pop)
```

**问题：**

1. `x` 和 `inner` 之间有填充吗？为什么？
2. `inner.a` 和 `inner.b` 之间有填充吗？为什么？
3. `inner.b` 和 `y` 之间有填充吗？为什么？
4. `Outer` 的总大小是多少？

---

### 任务 7：实现 pack 值验证

TinyCC 要求 pack 值满足以下条件：
- 范围：1 到 16
- 必须是 2 的幂

```c
if (val < 1 || val > 16 || (val & (val - 1)) != 0)
    goto pragma_err;
```

**问题：**

1. `(val & (val - 1)) != 0` 如何检测 2 的幂？为什么这有效？
2. 为什么 pack 值限制在 1-16 范围内？有什么实际原因？
3. 如果传入 `#pragma pack(3)`，TinyCC 会如何处理？

---

## 验证方法

1. 编译运行：`tcc -o pack_demo pack_demo.c && ./pack_demo`
2. 查看预处理输出：`tcc -E pack_demo.c`（观察 `#pragma` 如何被处理）
3. 使用 `offsetof()` 宏验证偏移量

## 思考题

1. `#pragma pack` 对性能有什么影响？在什么场景下应该使用 `pack(1)`？
2. 网络协议解析通常使用 `pack(1)`，为什么？有什么替代方案？
3. TinyCC 的 `PACK_STACK_SIZE` 设为多少？如果超过这个限制会发生什么？
