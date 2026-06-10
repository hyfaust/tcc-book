/*
 * libtcc_host.c - Register host functions for use by compiled code
 *
 * Demonstrates how to:
 *   1. Register C functions from the host program with tcc_add_symbol
 *   2. Register global data from the host program
 *   3. Call host functions from dynamically compiled code
 *   4. Build a bidirectional bridge between host and compiled code
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libtcc.h"

/* ========== Host functions callable by compiled code ========== */

/* Basic math operations */
static int host_add(int a, int b) { return a + b; }
static int host_mul(int a, int b) { return a * b; }

/* Math library wrappers (simple implementations to avoid libm dependency) */
static double host_sin(double x)
{
    /* Taylor series approximation */
    double sum = x, term = x;
    int i;
    for (i = 1; i < 10; i++) {
        term *= -x * x / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

static double host_cos(double x)
{
    double sum = 1, term = 1;
    int i;
    for (i = 1; i < 10; i++) {
        term *= -x * x / ((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

static double host_sqrt(double x)
{
    /* Newton's method */
    double guess = x / 2.0;
    int i;
    for (i = 0; i < 20; i++)
        guess = (guess + x / guess) / 2.0;
    return guess;
}

/* I/O functions */
static void host_print_int(const char *label, int value)
{
    printf("[HOST] %s = %d\n", label, value);
}

static void host_print_double(const char *label, double value)
{
    printf("[HOST] %s = %f\n", label, value);
}

static void host_print_string(const char *msg)
{
    printf("[HOST] %s\n", msg);
}

/* Memory allocation bridge */
static void *host_malloc(size_t size) { return malloc(size); }
static void host_free(void *ptr) { free(ptr); }

/* Timestamp function */
static double host_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Host data accessible by compiled code */
static const char *host_version = "1.0.0";
static int host_verbose = 1;

/* ========== Test program ========== */

static const char *program =
    "extern int host_add(int, int);\n"
    "extern int host_mul(int, int);\n"
    "extern double host_sin(double);\n"
    "extern double host_cos(double);\n"
    "extern double host_sqrt(double);\n"
    "extern void host_print_int(const char *, int);\n"
    "extern void host_print_double(const char *, double);\n"
    "extern void host_print_string(const char *);\n"
    "extern void *host_malloc(unsigned long);\n"
    "extern void host_free(void *);\n"
    "extern double host_time(void);\n"
    "extern const char *host_version;\n"
    "extern int host_verbose;\n"
    "\n"
    "int demo_math(void) {\n"
    "    int a = 10, b = 20;\n"
    "    host_print_string(\"--- Math Demo ---\");\n"
    "    host_print_int(\"add(10, 20)\", host_add(a, b));\n"
    "    host_print_int(\"mul(10, 20)\", host_mul(a, b));\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int demo_trig(void) {\n"
    "    double pi = 3.14159265358979;\n"
    "    host_print_string(\"--- Trigonometry Demo ---\");\n"
    "    host_print_double(\"sin(pi/6)\", host_sin(pi / 6));\n"
    "    host_print_double(\"cos(pi/6)\", host_cos(pi / 6));\n"
    "    host_print_double(\"sqrt(2)\", host_sqrt(2.0));\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int demo_timing(void) {\n"
    "    double t0, t1;\n"
    "    volatile int i;\n"
    "    volatile long long sum = 0;\n"
    "    host_print_string(\"--- Timing Demo ---\");\n"
    "    t0 = host_time();\n"
    "    for (i = 0; i < 1000000; i++)\n"
    "        sum += i;\n"
    "    t1 = host_time();\n"
    "    host_print_double(\"Loop time (sec)\", t1 - t0);\n"
    "    host_print_int(\"Sum (low bits)\", (int)(sum & 0xFFFF));\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int demo_memory(void) {\n"
    "    int *arr;\n"
    "    int i;\n"
    "    host_print_string(\"--- Memory Demo ---\");\n"
    "    arr = (int *)host_malloc(10 * sizeof(int));\n"
    "    for (i = 0; i < 10; i++)\n"
    "        arr[i] = i * i;\n"
    "    for (i = 0; i < 10; i++)\n"
    "        host_print_int(\"arr[i]\", arr[i]);\n"
    "    host_free(arr);\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int demo_host_data(void) {\n"
    "    host_print_string(\"--- Host Data Demo ---\");\n"
    "    host_print_string(host_version);\n"
    "    host_print_int(\"verbose\", host_verbose);\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "int run_all(void) {\n"
    "    demo_math();\n"
    "    demo_trig();\n"
    "    demo_timing();\n"
    "    demo_memory();\n"
    "    demo_host_data();\n"
    "    host_print_string(\"All demos complete.\");\n"
    "    return 0;\n"
    "}\n";

static void error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "TCC: %s\n", msg);
}

int main(void)
{
    TCCState *s;
    int (*run_all)(void);

    s = tcc_new();
    if (!s) {
        fprintf(stderr, "Failed to create TCC state\n");
        return 1;
    }

    tcc_set_error_func(s, stderr, error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Register all host functions and data */
    tcc_add_symbol(s, "host_add", host_add);
    tcc_add_symbol(s, "host_mul", host_mul);
    tcc_add_symbol(s, "host_sin", host_sin);
    tcc_add_symbol(s, "host_cos", host_cos);
    tcc_add_symbol(s, "host_sqrt", host_sqrt);
    tcc_add_symbol(s, "host_print_int", host_print_int);
    tcc_add_symbol(s, "host_print_double", host_print_double);
    tcc_add_symbol(s, "host_print_string", host_print_string);
    tcc_add_symbol(s, "host_malloc", host_malloc);
    tcc_add_symbol(s, "host_free", host_free);
    tcc_add_symbol(s, "host_time", host_time);
    tcc_add_symbol(s, "host_version", &host_version);
    tcc_add_symbol(s, "host_verbose", &host_verbose);

    /* Compile the program */
    if (tcc_compile_string(s, program) == -1) {
        fprintf(stderr, "Compilation failed\n");
        tcc_delete(s);
        return 1;
    }

    /* Relocate */
    if (tcc_relocate(s) < 0) {
        fprintf(stderr, "Relocation failed\n");
        tcc_delete(s);
        return 1;
    }

    /* Get and call the entry function */
    run_all = tcc_get_symbol(s, "run_all");
    if (!run_all) {
        fprintf(stderr, "Symbol 'run_all' not found\n");
        tcc_delete(s);
        return 1;
    }

    printf("=== Running compiled code with host function bridge ===\n\n");
    int ret = run_all();
    printf("\n=== Returned: %d ===\n", ret);

    tcc_delete(s);
    return 0;
}
