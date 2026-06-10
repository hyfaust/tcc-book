# 练习 2: 编写和运行 TCC 测试

## 背景

TCC 的测试套件是保证编译器质量的关键基础设施。本练习要求你编写新测试并理解测试流程。

## 任务

### 任务 1: 为 tests2/ 添加新测试

在 `tests/tests2/` 目录下添加一个新的测试文件，覆盖以下特性之一（选择一个）：

**选项 A: 复合字面量 (Compound Literals)**
```c
#include <stdio.h>

int main(void) {
    int *p = (int[]){1, 2, 3, 4, 5};
    int i;
    for (i = 0; i < 5; i++)
        printf("%d ", p[i]);
    printf("\n");

    struct { int x; int y; } *ps = &(struct { int x; int y; }){10, 20};
    printf("%d %d\n", ps->x, ps->y);

    return 0;
}
```

**选项 B: 指定初始化器 (Designated Initializers)**
```c
#include <stdio.h>

int main(void) {
    int arr[10] = {[0] = 1, [5] = 6, [9] = 10};
    int i;
    for (i = 0; i < 10; i++)
        printf("%d ", arr[i]);
    printf("\n");

    struct { int a; int b; int c; } s = {.c = 30, .a = 10};
    printf("%d %d %d\n", s.a, s.b, s.c);

    return 0;
}
```

**选项 C: _Static_assert**
```c
#include <stdio.h>

_Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");
_Static_assert(sizeof(void*) >= sizeof(int), "pointer must be >= int");

struct header {
    int magic;
    int version;
    int length;
};
_Static_assert(sizeof(struct header) == 12, "header must be 12 bytes");

int main(void) {
    printf("All static assertions passed\n");
    printf("sizeof(int) = %zu\n", sizeof(int));
    printf("sizeof(void*) = %zu\n", sizeof(void*));
    printf("sizeof(struct header) = %zu\n", sizeof(struct header));
    return 0;
}
```

### 步骤

1. 创建测试文件（选择下一个可用编号）
2. 使用参考编译器生成 `.expect` 文件：
   ```bash
   gcc -o test XX_name.c && ./test > XX_name.expect
   ```
3. 使用 TCC 编译并验证输出匹配：
   ```bash
   tcc -o test XX_name.c && ./test | diff - XX_name.expect
   ```
4. 如果有差异，分析是 TCC 的 bug 还是可接受的行为差异

### 任务 2: 运行完整测试套件

1. 构建 TCC（如果还没有构建）
2. 运行完整测试套件：
   ```bash
   cd tests && make test
   ```
3. 记录测试结果：通过了多少，失败了多少
4. 对于失败的测试，分析失败原因

### 任务 3: 边界条件测试

编写一个测试，专门测试整数溢出和边界条件：

```c
#include <stdio.h>
#include <limits.h>

int main(void) {
    /* 整数溢出 */
    int max = INT_MAX;
    printf("INT_MAX = %d\n", max);
    printf("INT_MAX + 1 = %d\n", max + 1);
    printf("INT_MIN = %d\n", INT_MIN);
    printf("INT_MIN - 1 = %d\n", INT_MIN - 1);

    /* 无符号溢出 */
    unsigned umax = UINT_MAX;
    printf("UINT_MAX = %u\n", umax);
    printf("UINT_MAX + 1 = %u\n", umax + 1);

    /* 除法边界 */
    printf("-1 / 2 = %d\n", -1 / 2);
    printf("-1 %% 2 = %d\n", -1 % 2);
    printf("1 / -2 = %d\n", 1 / -2);

    return 0;
}
```

## 验证标准

- 添加的测试文件格式正确（有 `.c` 和 `.expect` 文件）
- TCC 输出与预期输出完全一致
- 能解释失败测试的原因
- 理解了 TCC 测试套件的组织结构和运行方式
