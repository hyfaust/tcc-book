/*
 * hello.c - Hello World example for Chapter 1
 *
 * This is the simplest possible C program. It demonstrates:
 *   - #include directive (preprocessor)
 *   - Function definition (main)
 *   - Function call (printf)
 *   - String literal
 *   - Return statement
 *
 * Compile with:
 *   tcc hello.c -o hello          # compile to executable
 *   tcc -run hello.c              # compile and run directly
 *   tcc -E hello.c                # preprocess only
 *   tcc -S hello.c                # compile to assembly
 *   tcc -c hello.c                # compile to object file
 */

#include <stdio.h>

int main(void)
{
    printf("Hello, TinyCC!\n");
    return 0;
}
