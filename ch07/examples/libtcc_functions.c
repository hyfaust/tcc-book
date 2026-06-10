/*
 * libtcc_functions.c - Call compiled functions from host
 *
 * Demonstrates how to:
 *   1. Compile a C string into memory
 *   2. Extract function pointers via tcc_get_symbol
 *   3. Call compiled functions from the host program
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libtcc.h"

static int num_errors = 0;

static void error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    num_errors++;
}

/* Program with multiple functions to be called from host */
static const char *program =
    "#include <tcclib.h>\n"
    "\n"
    "/* Recursive fibonacci */\n"
    "int fib(int n) {\n"
    "    if (n <= 2) return 1;\n"
    "    return fib(n - 1) + fib(n - 2);\n"
    "}\n"
    "\n"
    "/* String formatting function */\n"
    "int format_result(char *buf, int bufsz, int n, int result) {\n"
    "    return snprintf(buf, bufsz, \"fib(%d) = %d\", n, result);\n"
    "}\n"
    "\n"
    "/* Array processing function */\n"
    "void bubble_sort(int *arr, int len) {\n"
    "    int i, j, tmp;\n"
    "    for (i = 0; i < len - 1; i++) {\n"
    "        for (j = 0; j < len - 1 - i; j++) {\n"
    "            if (arr[j] > arr[j+1]) {\n"
    "                tmp = arr[j];\n"
    "                arr[j] = arr[j+1];\n"
    "                arr[j+1] = tmp;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n";

int main(void)
{
    TCCState *s;
    int (*fib)(int);
    int (*format_result)(char *, int, int, int);
    void (*bubble_sort)(int *, int);

    /* Create and configure TCC state */
    s = tcc_new();
    tcc_set_error_func(s, stderr, error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Compile */
    if (tcc_compile_string(s, program) == -1) {
        fprintf(stderr, "Compilation failed with %d errors\n", num_errors);
        tcc_delete(s);
        return 1;
    }

    /* Relocate: resolve all symbols and make code executable */
    if (tcc_relocate(s) < 0) {
        fprintf(stderr, "Relocation failed\n");
        tcc_delete(s);
        return 1;
    }

    /* Get function pointers */
    fib = tcc_get_symbol(s, "fib");
    format_result = tcc_get_symbol(s, "format_result");
    bubble_sort = tcc_get_symbol(s, "bubble_sort");

    if (!fib || !format_result || !bubble_sort) {
        fprintf(stderr, "Failed to get symbols\n");
        tcc_delete(s);
        return 1;
    }

    /* Use the compiled functions */
    printf("=== Fibonacci ===\n");
    int i;
    for (i = 1; i <= 20; i++) {
        char buf[256];
        int result = fib(i);
        format_result(buf, sizeof(buf), i, result);
        printf("  %s\n", buf);
    }

    printf("\n=== Bubble Sort ===\n");
    int arr[] = {64, 34, 25, 12, 22, 11, 90, 1, 55, 42};
    int len = sizeof(arr) / sizeof(arr[0]);

    printf("Before: ");
    for (i = 0; i < len; i++) printf("%d ", arr[i]);
    printf("\n");

    bubble_sort(arr, len);

    printf("After:  ");
    for (i = 0; i < len; i++) printf("%d ", arr[i]);
    printf("\n");

    tcc_delete(s);
    return 0;
}
