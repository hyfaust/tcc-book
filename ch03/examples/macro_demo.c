/*
 * macro_demo.c - Demonstrates all macro features in TinyCC preprocessor
 *
 * Compile with:
 *   tcc -E macro_demo.c          # see preprocessed output
 *   tcc -o macro_demo macro_demo.c && ./macro_demo
 */

#include <stdio.h>

/* ============================================================
 * 1. Object-like macros (MACRO_OBJ)
 * ============================================================ */

#define PI 3.14159265358979
#define MAX_BUFFER_SIZE 1024
#define GREETING "Hello, TinyCC!"
#define EMPTY_MACRO   /* empty body */

/* ============================================================
 * 2. Function-like macros (MACRO_FUNC)
 * ============================================================ */

#define SQUARE(x)       ((x) * (x))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define CLAMP(val, lo, hi)  MIN(MAX((val), (lo)), (hi))

/* ============================================================
 * 3. Stringification (#)
 * ============================================================ */

#define STR(x)          #x
#define STR_EXPAND(x)   STR(x)   /* double-expand to stringify macro values */

/* ============================================================
 * 4. Token pasting (##)
 * ============================================================ */

#define CONCAT(a, b)        a ## b
#define CONCAT3(a, b, c)    a ## b ## c
#define MAKE_FUNC(prefix, name)  prefix ## _ ## name

/* ============================================================
 * 5. Variadic macros (__VA_ARGS__)
 * ============================================================ */

/* Standard C99 variadic macro */
#define LOG(fmt, ...) \
    fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

/* Variadic macro with count */
#define VA_COUNT(...)   (sizeof((int[]){__VA_ARGS__}) / sizeof(int))

/* ============================================================
 * 6. Self-referential macro (prevented from infinite recursion)
 * ============================================================ */

#define SELF SELF   /* macro_subst() prevents infinite expansion */

/* ============================================================
 * 7. Macro with macro arguments
 * ============================================================ */

#define APPLY(fn, x)   fn(x)
#define DOUBLE(x)      ((x) + (x))

/* ============================================================
 * 8. Special built-in macros
 * ============================================================ */

#define WHERE  __FILE__ ":" STR_EXPAND(__LINE__)

/* ============================================================
 * Demo functions
 * ============================================================ */

/* Token pasting creates new identifier */
#define MAKE_VAR(n)   var_##n

static int MAKE_VAR(x) = 10;    /* expands to: int var_x = 10; */
static int MAKE_VAR(y) = 20;    /* expands to: int var_y = 20; */

/* Function created by token pasting */
MAKE_FUNC(int, add)(int a, int b) { return a + b; }
/* expands to: int add(int a, int b) { return a + b; } */

int main(void)
{
    int result;

    /* --- Object macros --- */
    printf("=== Object-like Macros ===\n");
    printf("PI = %f\n", PI);
    printf("MAX_BUFFER_SIZE = %d\n", MAX_BUFFER_SIZE);
    printf("GREETING = %s\n", GREETING);

    /* --- Function macros --- */
    printf("\n=== Function-like Macros ===\n");
    result = SQUARE(5);
    printf("SQUARE(5) = %d\n", result);

    result = SQUARE(2 + 3);
    printf("SQUARE(2+3) = %d  (note: safe due to parens)\n", result);

    printf("MAX(10, 20) = %d\n", MAX(10, 20));
    printf("MIN(10, 20) = %d\n", MIN(10, 20));
    printf("CLAMP(15, 0, 10) = %d\n", CLAMP(15, 0, 10));

    /* --- Stringification --- */
    printf("\n=== Stringification (#) ===\n");
    printf("STR(hello) = %s\n", STR(hello));
    printf("STR(1 + 2) = %s\n", STR(1 + 2));
    /* STR_EXPAND expands the macro first, then stringifies */
    printf("STR_EXPAND(PI) = %s\n", STR_EXPAND(PI));
    printf("STR_EXPAND(__LINE__) = %s\n", STR_EXPAND(__LINE__));

    /* --- Token pasting --- */
    printf("\n=== Token Pasting (##) ===\n");
    printf("var_x = %d\n", var_x);
    printf("var_y = %d\n", var_y);
    printf("int_add(3, 4) = %d\n", int_add(3, 4));

    /* CONCAT creates a new pp-token */
    result = CONCAT(12, 34);
    printf("CONCAT(12, 34) = %d\n", result);

    /* --- Variadic macros --- */
    printf("\n=== Variadic Macros (__VA_ARGS__) ===\n");
    LOG("system ready, value=%d", 42);
    LOG("no extra args");

    /* --- Built-in macros --- */
    printf("\n=== Built-in Macros ===\n");
    printf("__LINE__ = %d\n", __LINE__);
    printf("__FILE__ = %s\n", __FILE__);
    printf("__DATE__ = %s\n", __DATE__);
    printf("__TIME__ = %s\n", __TIME__);
    printf("__STDC__ = %d\n", __STDC__);
    printf("__TINYC__ = %d\n", __TINYC__);
    printf("Location: %s\n", WHERE);

    /* --- Macro expansion with nested macros --- */
    printf("\n=== Nested Macro Expansion ===\n");
    /* APPLY(DOUBLE, 5) -> DOUBLE(5) -> ((5) + (5)) -> 10 */
    result = APPLY(DOUBLE, 5);
    printf("APPLY(DOUBLE, 5) = %d\n", result);

    /* --- Self-referential macro --- */
    /* SELF expands to SELF; macro_subst() marks it as nosubst */
    /* We can't directly print it, but tcc -E shows SELF remains */
    printf("\n=== Self-referential Macro ===\n");
    printf("SELF does not infinitely expand (see tcc -E output)\n");

    /* --- Empty __VA_ARGS__ with ## --- */
    printf("\n=== Empty __VA_ARGS__ ===\n");
    /* LOG("simple") has empty __VA_ARGS__, ## eats the comma */
    LOG("no varargs here");

    printf("\nAll macro demos completed.\n");
    return 0;
}
