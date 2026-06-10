# 练习 1: 使用 libtcc 构建交互式计算器

## 背景

libtcc 允许在运行时编译和执行 C 代码。本练习要求你利用这一能力构建一个交互式计算器，用户输入 C 表达式，程序编译、执行并返回结果。

## 要求

1. 从标准输入逐行读取用户输入（每行一个表达式）
2. 将表达式包装为函数：
   ```c
   double calc(void) { return <用户表达式>; }
   ```
3. 使用 libtcc 编译并执行该函数
4. 输出计算结果
5. 循环执行直到用户输入 `quit`

## 提示

- 使用 `TCC_OUTPUT_MEMORY` 模式
- 注册 `sin`、`cos`、`tan`、`sqrt`、`pow`、`log` 等数学函数为宿主符号
- 为错误处理设置回调函数，将编译错误显示给用户而非终止程序
- 使用 `snprintf` 动态构建要编译的代码字符串
- 考虑添加 `#line 1 "calc"` 指令来改善错误消息

## 扩展挑战

1. 支持变量定义和引用（维护一个全局状态字符串，每次编译时在前面加上之前的变量定义）
2. 支持用户自定义函数（如 `def double square(double x) { return x*x; }`）
3. 实现结果缓存：如果表达式之前计算过，直接返回缓存结果

## 验证标准

```bash
$ ./calculator
> 2 + 3 * 4
14.000000
> sqrt(2)
1.414214
> sin(3.14159/4)
0.707107
> invalid syntax here
Error: expected expression before 'invalid'
> quit
$
```
