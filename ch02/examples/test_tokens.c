/*
 * test_tokens.c - 包含各种类型 token 的测试文件
 *
 * 这个文件的设计目的是覆盖 C 语言中尽可能多的 token 类型。
 * 可以配合 token_dump.c 使用，也可以直接用 tcc -E 查看预处理输出。
 *
 * 使用方法:
 *   tcc -E test_tokens.c          # 查看预处理输出
 *   tcc -E -dM test_tokens.c      # 查看预定义宏
 */

#include <stdio.h>
#include <wchar.h>

/* ===== 2.1 控制流关键字 ===== */
void test_control_flow(void)
{
    int i = 0;

    if (i == 0) {
        i = 1;
    } else {
        i = 2;
    }

    while (i < 10) {
        i++;
        if (i == 5) continue;
        if (i == 8) break;
    }

    for (i = 0; i < 10; i++) {
        /* loop body */
    }

    do {
        i--;
    } while (i > 0);

    switch (i) {
    case 0:
        i = -1;
        break;
    default:
        i = 0;
        break;
    }

    goto label;
label:
    return;
}

/* ===== 2.2 类型关键字 ===== */
void test_types(void)
{
    /* 基本类型 */
    char c = 'a';
    signed char sc = -1;
    unsigned char uc = 255;
    short s = 1;
    unsigned short us = 2;
    int i = 42;
    unsigned int ui = 100u;
    long l = 1000L;
    unsigned long ul = 2000UL;
    long long ll = 100000LL;
    unsigned long long ull = 200000ULL;
    float f = 3.14f;
    double d = 2.718;
    long double ld = 1.234L;
    void *p = (void *)0;

    /* C99 类型 */
    _Bool b = 1;

    /* 结构体和联合体 */
    struct Point { int x, y; };
    struct Point pt = { 10, 20 };

    union Data { int i; float f; };
    union Data data;
    data.i = 42;

    /* 枚举 */
    enum Color { RED, GREEN, BLUE };
    enum Color color = RED;

    /* sizeof 和 typedef */
    typedef int MyInt;
    MyInt mi = sizeof(int);
    int sz = sizeof(struct Point);

    /* 使用变量以避免警告 */
    (void)c; (void)sc; (void)uc; (void)s; (void)us;
    (void)ui; (void)l; (void)ul; (void)ll; (void)ull;
    (void)f; (void)d; (void)ld; (void)p; (void)b;
    (void)pt; (void)data; (void)color; (void)mi; (void)sz;
}

/* ===== 2.3 存储类和修饰符 ===== */
extern int extern_var;
static int static_var = 0;
const int const_var = 100;
volatile int vol_var;
register int reg_var;

/* ===== 2.4 运算符 ===== */
int test_operators(int a, int b)
{
    int result = 0;

    /* 算术运算符 */
    result = a + b;
    result = a - b;
    result = a * b;
    result = a / b;
    result = a % b;

    /* 一元运算符 */
    result = -a;
    result = !a;
    result = ~a;
    a++;
    a--;
    ++a;
    --a;

    /* 关系运算符 */
    result = (a == b);
    result = (a != b);
    result = (a < b);
    result = (a > b);
    result = (a <= b);
    result = (a >= b);

    /* 逻辑运算符 */
    result = (a && b);
    result = (a || b);

    /* 位运算符 */
    result = a & b;
    result = a | b;
    result = a ^ b;
    result = a << 2;
    result = a >> 3;

    /* 复合赋值运算符 */
    result += a;
    result -= a;
    result *= a;
    result /= a;
    result %= a;
    result &= a;
    result |= a;
    result ^= a;
    result <<= 1;
    result >>= 1;

    /* 三元运算符 */
    result = (a > b) ? a : b;

    /* 逗号运算符 */
    result = (a++, b++, a + b);

    /* 结构体成员访问 */
    struct S { int x; };
    struct S s = { 42 };
    struct S *sp = &s;
    result = s.x;
    result = sp->x;

    /* 指针操作 */
    int *ptr = &a;
    result = *ptr;

    return result;
}

/* ===== 2.5 字符串和字符常量 ===== */
void test_strings(void)
{
    /* 普通字符串 */
    const char *s1 = "hello world";
    const char *s2 = "line1\nline2\ttab";
    const char *s3 = "escape: \\ \" \' \? \a \b \f \v";

    /* 十六进制转义 */
    const char *s4 = "\x48\x65\x6C\x6C\x6F";  /* "Hello" */

    /* 八进制转义 */
    const char *s5 = "\110\145\154\154\157";   /* "Hello" */

    /* 宽字符串 */
    const wchar_t *ws = L"wide string";

    /* 字符常量 */
    char c1 = 'A';
    char c2 = '\n';
    char c3 = '\x41';
    wchar_t wc = L'Z';

    /* 多字节字符串拼接 */
    const char *s6 = "part1 " "part2 " "part3";

    (void)s1; (void)s2; (void)s3; (void)s4; (void)s5;
    (void)ws; (void)c1; (void)c2; (void)c3; (void)wc;
    (void)s6;
}

/* ===== 2.6 数字常量 ===== */
void test_numbers(void)
{
    /* 十进制整数 */
    int dec1 = 0;
    int dec2 = 42;
    int dec3 = 2147483647;

    /* 八进制整数 */
    int o1 = 0777;
    int o2 = 0123;

    /* 十六进制整数 */
    int h1 = 0xFF;
    int h2 = 0xDEADBEEF;
    int h3 = 0X1a2B;

    /* 二进制整数 (GCC 扩展) */
    int b1 = 0b1010;
    int b2 = 0B11110000;

    /* 带后缀整数 */
    unsigned int u1 = 100u;
    unsigned int u2 = 100U;
    long l1 = 100l;
    long l2 = 100L;
    unsigned long ul1 = 100ul;
    unsigned long ul2 = 100UL;
    long long ll1 = 100ll;
    long long ll2 = 100LL;
    unsigned long long ull1 = 100ull;
    unsigned long long ull2 = 100ULL;

    /* 混合后缀 */
    unsigned long ul3 = 100LU;
    unsigned long long ull3 = 100ULL;

    /* 十进制浮点数 */
    float f1 = 3.14f;
    float f2 = 3.14F;
    double d1 = 3.14;
    double d2 = .5;
    double d3 = 5.;
    double d4 = 1e10;
    double d5 = 1.5e-3;
    double d6 = 1.5E+3;
    long double ld1 = 3.14L;

    /* 十六进制浮点数 */
    double hf1 = 0x1.0p1;
    double hf2 = 0x1.8p+10;
    double hf3 = 0xAp-4;

    (void)dec1; (void)dec2; (void)dec3;
    (void)d1; (void)d2; (void)d3;
    (void)o1; (void)o2;
    (void)h1; (void)h2; (void)h3;
    (void)b1; (void)b2;
    (void)u1; (void)u2; (void)l1; (void)l2;
    (void)ul1; (void)ul2; (void)ll1; (void)ll2;
    (void)ull1; (void)ull2; (void)ul3; (void)ull3;
    (void)f1; (void)f2; (void)d4; (void)d5; (void)d6;
    (void)ld1;
    (void)hf1; (void)hf2; (void)hf3;
}

/* ===== 2.7 预处理器指令 ===== */
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define STRINGIFY(x) #x
#define CONCAT(a, b) a ## b
#define EMPTY

#ifdef __linux__
static const char *platform = "linux";
#elif defined(__APPLE__)
static const char *platform = "apple";
#else
static const char *platform = "other";
#endif

#ifndef HEADER_GUARD
#define HEADER_GUARD
#endif

/* 行号指令 */
#line 1000 "synthetic.c"

/* 条件编译嵌套 */
#if defined(__x86_64__)
#  if __SIZEOF_POINTER__ == 8
static int arch_bits = 64;
#  else
static int arch_bits = 32;
#  endif
#elif defined(__i386__)
static int arch_bits = 32;
#else
static int arch_bits = 0;
#endif

/* ===== 2.8 GCC 扩展语法 ===== */
/* Note: __int128 is a GCC extension; tcc supports it in expressions but not in typedef */
long long big_val = 0;

static inline __attribute__((always_inline))
int always_inline_func(int x)
{
    return x * 2;
}

__attribute__((noreturn))
static void never_returns(void)
{
    while (1) {}
}

struct __attribute__((packed)) PackedStruct {
    char c;
    int i;
};

struct __attribute__((aligned(16))) AlignedStruct {
    int data[4];
};

typedef int __attribute__((mode(SI))) int32_mode;

/* typeof */
static int test_typeof(int x)
{
    typeof(x) y = x + 1;
    return y;
}

/* __label__ */
static int test_local_label(int x)
{
    __label__ done;
    if (x < 0) goto done;
    x = x * 2;
done:
    return x;
}

/* statement expression */
static int test_stmt_expr(int x)
{
    return ({
        int _tmp = x * x;
        _tmp + 1;
    });
}

/* ===== 2.9 内联汇编 ===== */
static void test_asm(void)
{
    int result;
    /* x86 内联汇编 */
    __asm__ __volatile__ (
        "movl $42, %0"
        : "=r"(result)
        :
        : "memory"
    );
    (void)result;
}

/* ===== 2.10 C11 特性 ===== */
_Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");

/* _Generic */
#define type_name(x) _Generic((x),    \
    int: "int",                         \
    float: "float",                     \
    double: "double",                   \
    default: "other"                    \
)

static void test_generic(void)
{
    const char *name = type_name(42);
    (void)name;
}

/* _Alignas and _Alignof */
static _Alignas(64) int aligned_var;
static int align_test = _Alignof(long long);

/* _Atomic */
static _Atomic int atomic_var;

/* _Noreturn */
_Noreturn void exit_forever(int code);

/* ===== 2.11 内建函数 ===== */
static void test_builtins(void)
{
    /* __builtin_constant_p */
    int is_const = __builtin_constant_p(42);

    /* __builtin_expect */
    long val = __builtin_expect(1, 1);

    /* __builtin_types_compatible_p */
    int compat = __builtin_types_compatible_p(int, int);

    /* __builtin_choose_expr */
    int chosen = __builtin_choose_expr(1, 42, 0);

    /* __builtin_frame_address */
    void *frame = __builtin_frame_address(0);

    /* __builtin_return_address */
    void *ret = __builtin_return_address(0);

    (void)is_const; (void)val; (void)compat;
    (void)chosen; (void)frame; (void)ret;
}

/* ===== 2.12 特殊标识符 ===== */
static void test_special_ident(void)
{
    const char *func_name = __func__;
    const char *func_name2 = __FUNCTION__;
    int line = __LINE__;
    const char *file = __FILE__;
    (void)func_name; (void)func_name2;
    (void)line; (void)file;
}

/* ===== 2.13 注释样式 ===== */
/* This is a C-style comment */
// This is a C++-style comment
/* Multi
   line
   comment */
// Multi \
   line \
   C++ comment with backslash continuation

/* ===== 2.14 空白和续行 ===== */
int    spaced_out    =    42   ;

int \
continued \
= \
100;

/* ===== main ===== */
int main(void)
{
    test_control_flow();
    test_types();
    test_strings();
    test_numbers();
    always_inline_func(1);
    test_typeof(42);
    test_local_label(1);
    test_stmt_expr(3);
    test_asm();
    test_generic();
    test_builtins();
    test_special_ident();
    return 0;
}
