/*
 * libtcc_hello.c - Basic libtcc usage example
 *
 * Demonstrates the minimal workflow for embedding TCC:
 *   tcc_new → configure → compile → run → tcc_delete
 */
#include <stdio.h>
#include <stdlib.h>
#include "libtcc.h"

static void error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "TCC Error: %s\n", msg);
}

int main(int argc, char **argv)
{
    TCCState *s;
    const char *program =
        "#include <tcclib.h>\n"
        "int main(int argc, char **argv) {\n"
        "    int i;\n"
        "    printf(\"Hello from embedded TCC!\\n\");\n"
        "    printf(\"Arguments:\\n\");\n"
        "    for (i = 0; i < argc; i++)\n"
        "        printf(\"  argv[%d] = %s\\n\", i, argv[i]);\n"
        "    return 0;\n"
        "}\n";

    /* Step 1: Create a new compilation context */
    s = tcc_new();
    if (!s) {
        fprintf(stderr, "Failed to create TCC state\n");
        return 1;
    }

    /* Step 2: Configure error handling */
    tcc_set_error_func(s, stderr, error_handler);

    /* Step 3: Optionally set library/include paths */
    /* tcc_set_lib_path(s, "/usr/local/lib/tcc"); */
    /* tcc_add_include_path(s, "/usr/include"); */

    /* Step 4: Set output type to in-memory execution */
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Step 5: Compile the source code */
    if (tcc_compile_string(s, program) == -1) {
        fprintf(stderr, "Compilation failed\n");
        tcc_delete(s);
        return 1;
    }

    /* Step 6: Run the compiled program */
    printf("--- Running compiled program ---\n");
    int ret = tcc_run(s, argc, argv);
    printf("--- Program returned: %d ---\n", ret);

    /* Step 7: Cleanup */
    tcc_delete(s);
    return ret;
}
