/*
 * simple_math.c - Demonstrates functions, variables, and types
 *
 * This example exercises several C language features that the compiler
 * must handle:
 *   - Multiple function definitions
 *   - Local and global variables
 *   - Integer and floating-point types
 *   - Control flow (if/else, for loop)
 *   - Function calls with arguments and return values
 *   - Printf format strings with multiple types
 *
 * Compile with:
 *   tcc simple_math.c -o simple_math
 *   tcc -run simple_math.c
 */

#include <stdio.h>

/* Global constant */
#define MAX_FIB 20

/* Function: compute factorial recursively */
static int factorial(int n)
{
    if (n <= 1)
        return 1;
    return n * factorial(n - 1);
}

/* Function: compute Fibonacci number iteratively */
static int fibonacci(int n)
{
    int a = 0, b = 1, i, temp;

    for (i = 0; i < n; i++) {
        temp = a + b;
        a = b;
        b = temp;
    }
    return a;
}

/* Function: compute the maximum of two integers */
static int max(int a, int b)
{
    return a > b ? a : b;
}

/* Function: compute power using repeated multiplication */
static double power(double base, int exp)
{
    double result = 1.0;
    int i;

    for (i = 0; i < exp; i++)
        result *= base;
    return result;
}

int main(void)
{
    int i;
    int fact_val = 10;
    double pi_approx;

    /* Demonstrate factorial */
    printf("Factorials:\n");
    for (i = 0; i <= fact_val; i++)
        printf("  %2d! = %d\n", i, factorial(i));

    /* Demonstrate Fibonacci sequence */
    printf("\nFibonacci sequence (first %d terms):\n", MAX_FIB);
    for (i = 0; i < MAX_FIB; i++)
        printf("  F(%2d) = %d\n", i, fibonacci(i));

    /* Demonstrate max function */
    printf("\nmax(42, 17) = %d\n", max(42, 17));
    printf("max(-3, 5)  = %d\n", max(-3, 5));

    /* Demonstrate floating-point power function */
    printf("\nPowers of 2:\n");
    for (i = 0; i <= 10; i++)
        printf("  2^%2d = %.0f\n", i, power(2.0, i));

    /* Approximate pi using Leibniz formula: pi/4 = 1 - 1/3 + 1/5 - 1/7 + ... */
    pi_approx = 0.0;
    for (i = 0; i < 100000; i++) {
        if (i % 2 == 0)
            pi_approx += 1.0 / (2 * i + 1);
        else
            pi_approx -= 1.0 / (2 * i + 1);
    }
    pi_approx *= 4.0;
    printf("\nPi approximation (Leibniz, 100000 terms): %.10f\n", pi_approx);

    return 0;
}
