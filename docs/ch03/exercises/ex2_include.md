# 练习 2：#include 搜索路径追踪

## 目标

理解 TinyCC 中 `#include` 指令的文件搜索机制，包括搜索顺序、`CachedInclude` 优化和 `#pragma once`。

## 背景

TinyCC 的 `parse_include()` 函数（`tccpp.c:1314`）处理 `#include` 指令。搜索路径按以下顺序：

1. **绝对路径**（`i == 0`）：如果文件名以 `/` 开头。
2. **当前文件目录**（`i == 1`）：仅对 `""` 形式有效。
3. **用户包含路径**（`-I` 指定）：`s1->include_paths[]`。
4. **系统包含路径**（`-isystem` 指定）：`s1->sysinclude_paths[]`。

## 场景

假设有以下目录结构：

```
/home/user/project/
├── main.c
├── myheader.h
├── sub/
│   ├── helper.h
│   └── internal.h
└── third_party/
    └── lib/
        └── lib.h
```

`main.c` 的内容：

```c
#include "myheader.h"
#include "sub/helper.h"
#include <stdio.h>
#include "lib.h"
```

编译命令：

```bash
tcc -I /home/user/project/third_party -c main.c
```

## 任务

### 任务 1：追踪 `#include "myheader.h"` 的搜索路径

逐步列出 `parse_include()` 中的搜索过程：

| 步骤 | 索引 `i` | 搜索目录 | 完整路径 | 结果 |
|------|----------|----------|----------|------|
| 1 | 0 | （绝对路径检查） | — | 跳过（不是绝对路径） |
| 2 | 1 | 当前文件目录 `/home/user/project/` | `/home/user/project/myheader.h` | **找到** |

---

### 任务 2：追踪 `#include "sub/helper.h"` 的搜索路径

填写下表：

| 步骤 | 索引 `i` | 搜索目录 | 完整路径 | 结果 |
|------|----------|----------|----------|------|
| 1 | 0 | | | |
| 2 | 1 | | | |
| 3 | 2 | | | |

---

### 任务 3：追踪 `#include <stdio.h>` 的搜索路径

填写下表（注意 `<>` 形式跳过当前文件目录）：

| 步骤 | 索引 `i` | 搜索目录 | 完整路径 | 结果 |
|------|----------|----------|----------|------|
| 1 | 0 | | | |
| 2 | 1 | | | （为什么跳过？） |
| 3 | 2 | | | |
| ... | | | | |

提示：TinyCC 的默认系统包含路径通常是 `/usr/include` 和 `/usr/local/lib/tcc/include`。

---

### 任务 4：追踪 `#include "lib.h"` 的搜索路径

填写下表：

| 步骤 | 索引 `i` | 搜索目录 | 完整路径 | 结果 |
|------|----------|----------|----------|------|
| 1 | 0 | | | |
| 2 | 1 | | | |
| 3 | 2 | | | |

---

### 任务 5：CachedInclude 优化分析

考虑以下头文件 `config.h`：

```c
#ifndef CONFIG_H
#define CONFIG_H

#define MAX_SIZE 1024
#define VERSION 2

#endif
```

**问题：**

1. TinyCC 如何检测这是一个 include guard？提示：关注 `preprocess()` 中 `is_bof` 参数和 `file->ifndef_macro` 字段。

2. 当 `main.c` 中第二次 `#include "config.h"` 时，TinyCC 如何利用 `CachedInclude` 跳过重复解析？

3. `CachedInclude` 结构体中 `ifndef_macro` 字段存储的是什么值？`search_cached_include()` 中如何使用它？

---

### 任务 6：#pragma once

```c
// singleton.h
#pragma once

static int counter = 0;
```

**问题：**

1. `#pragma once` 在 TinyCC 中是如何实现的？（提示：查看 `pragma_parse()` 中 `TOK_once` 分支。）

2. `#pragma once` 和 include guard 模式（`#ifndef/#define/#endif`）在 TinyCC 中的实现有何异同？

3. 如果一个文件同时使用了 `#pragma once` 和 include guard，TinyCC 的行为是什么？

---

### 任务 7：#include_next

`#include_next` 是一个 GNU 扩展，用于在搜索路径的"下一个"位置查找文件。

假设编译命令为：

```bash
tcc -I /path/a -I /path/b -I /path/c -c main.c
```

文件 `/path/a/header.h` 中包含：

```c
#include_next "header.h"
```

**问题：**

1. `#include_next` 的搜索从哪个路径开始？（提示：查看 `file->include_next_index`。）

2. 这个特性在什么场景下有用？（提示：考虑"覆盖式"头文件。）

---

## 验证方法

使用 `tcc -vv` 查看文件包含的详细信息：

```bash
tcc -vv -I /home/user/project/third_party -c main.c
```

输出会显示每个文件的包含路径和跳过信息。

## 思考题

1. 为什么 `<>` 形式不搜索当前文件目录？这有什么安全考虑？
2. `search_cached_include()` 中的 `normalized_PATHCMP()` 是做什么的？为什么需要它？
3. `INCLUDE_STACK_SIZE` 设为 32，如果超过这个限制会发生什么？为什么需要这个限制？
