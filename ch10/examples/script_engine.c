/*
 * script_engine.c - A mini script engine using libtcc
 *
 * Demonstrates a complete embedded scripting system:
 *   - Loading and compiling C scripts
 *   - Registering host functions for the script to call
 *   - Hot-reload support (re-compile when file changes)
 *   - Error recovery (keep old script running on compile failure)
 *
 * Usage:
 *   ./script_engine script.c
 *
 * The script must define:
 *   void on_init(void);
 *   void on_event(const char *event, const char *data);
 *   void on_shutdown(void);
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "libtcc.h"

/* ========== Host API available to scripts ========== */

static void host_log(const char *level, const char *msg)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
    printf("[%s %s] %s\n", timebuf, level, msg);
}

static int host_random_int(int min, int max)
{
    return min + rand() % (max - min + 1);
}

static double host_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void host_print_int(const char *label, int val)
{
    printf("  %s = %d\n", label, val);
}

static void host_print_str(const char *label, const char *val)
{
    printf("  %s = \"%s\"\n", label, val);
}

/* ========== Script engine state ========== */

typedef void (*script_fn_init)(void);
typedef void (*script_fn_event)(const char *, const char *);
typedef void (*script_fn_shutdown)(void);

typedef struct {
    TCCState *tcc;
    script_fn_init on_init;
    script_fn_event on_event;
    script_fn_shutdown on_shutdown;
    char *source_path;
    time_t last_modified;
    int loaded;
    int reload_count;
} ScriptEngine;

/* ========== Error handler ========== */

static int compile_errors = 0;

static void script_error_handler(void *opaque, const char *msg)
{
    fprintf(stderr, "  [compile error] %s\n", msg);
    compile_errors++;
}

/* ========== Script loading ========== */

static void register_host_functions(TCCState *s)
{
    tcc_add_symbol(s, "host_log",          host_log);
    tcc_add_symbol(s, "host_random_int",   host_random_int);
    tcc_add_symbol(s, "host_time_sec",     host_time_sec);
    tcc_add_symbol(s, "host_print_int",    host_print_int);
    tcc_add_symbol(s, "host_print_str",    host_print_str);
}

static int script_load(ScriptEngine *eng, const char *path)
{
    TCCState *s;
    struct stat st;

    if (stat(path, &st) < 0) {
        fprintf(stderr, "Error: cannot stat '%s'\n", path);
        return -1;
    }

    compile_errors = 0;

    /* Create new TCC state */
    s = tcc_new();
    if (!s) {
        fprintf(stderr, "Error: tcc_new() failed\n");
        return -1;
    }

    tcc_set_error_func(s, NULL, script_error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Register host API */
    register_host_functions(s);

    /* Compile script */
    if (tcc_add_file(s, path) == -1) {
        fprintf(stderr, "Script compilation failed (%d error(s))\n",
                compile_errors);
        tcc_delete(s);
        return -1;
    }

    /* Relocate */
    if (tcc_relocate(s) < 0) {
        fprintf(stderr, "Script relocation failed\n");
        tcc_delete(s);
        return -1;
    }

    /* Get entry points */
    script_fn_init new_init = tcc_get_symbol(s, "on_init");
    script_fn_event new_event = tcc_get_symbol(s, "on_event");
    script_fn_shutdown new_shutdown = tcc_get_symbol(s, "on_shutdown");

    if (!new_event) {
        fprintf(stderr, "Warning: script does not define on_event()\n");
    }

    /* Swap in new state (under lock if multi-threaded) */
    if (eng->tcc) {
        /* Call old shutdown before replacing */
        if (eng->on_shutdown)
            eng->on_shutdown();
        tcc_delete(eng->tcc);
    }

    eng->tcc = s;
    eng->on_init = new_init;
    eng->on_event = new_event;
    eng->on_shutdown = new_shutdown;
    eng->last_modified = st.st_mtime;
    eng->loaded = 1;
    eng->reload_count++;

    host_log("ENGINE", "Script loaded successfully");

    /* Call init */
    if (eng->on_init)
        eng->on_init();

    return 0;
}

static void script_unload(ScriptEngine *eng)
{
    if (eng->loaded) {
        if (eng->on_shutdown)
            eng->on_shutdown();
        tcc_delete(eng->tcc);
        eng->tcc = NULL;
        eng->loaded = 0;
    }
    free(eng->source_path);
}

/* ========== Hot reload check ========== */

static int script_check_reload(ScriptEngine *eng)
{
    struct stat st;
    if (!eng->source_path || !eng->loaded)
        return 0;
    if (stat(eng->source_path, &st) < 0)
        return 0;
    if (st.st_mtime <= eng->last_modified)
        return 0;

    host_log("ENGINE", "Script file changed, reloading...");
    return script_load(eng, eng->source_path);
}

/* ========== Example default script ========== */

static const char *default_script =
    "#include <tcclib.h>\n"
    "\n"
    "extern void host_log(const char *, const char *);\n"
    "extern int host_random_int(int, int);\n"
    "extern double host_time_sec(void);\n"
    "extern void host_print_int(const char *, int);\n"
    "\n"
    "void on_init(void) {\n"
    "    host_log(\"SCRIPT\", \"Hello from the script engine!\");\n"
    "}\n"
    "\n"
    "void on_event(const char *event, const char *data) {\n"
    "    host_log(\"SCRIPT\", event);\n"
    "    if (!strcmp(event, \"random\")) {\n"
    "        int n = host_random_int(1, 100);\n"
    "        host_print_int(\"random number\", n);\n"
    "    } else if (!strcmp(event, \"time\")) {\n"
    "        /* no-op: time is printed by host */\n"
    "    }\n"
    "}\n"
    "\n"
    "void on_shutdown(void) {\n"
    "    host_log(\"SCRIPT\", \"Goodbye from the script engine!\");\n"
    "}\n";

/* ========== Main ========== */

int main(int argc, char **argv)
{
    ScriptEngine eng = {0};
    char input[256];
    double t0, t1;

    printf("=== Mini Script Engine (libtcc) ===\n\n");

    if (argc > 1) {
        /* Load script from file */
        eng.source_path = strdup(argv[1]);
        printf("Loading script: %s\n", argv[1]);
        if (script_load(&eng, argv[1]) < 0) {
            fprintf(stderr, "Failed to load script.\n");
            /* Fall through to interactive mode with no script */
        }
    } else {
        /* No file provided: write default script to temp file */
        const char *tmp = "/tmp/tcc_script_engine_demo.c";
        FILE *f = fopen(tmp, "w");
        if (f) {
            fputs(default_script, f);
            fclose(f);
            eng.source_path = strdup(tmp);
            printf("Using default script: %s\n", tmp);
            script_load(&eng, tmp);
        }
    }

    /* Event loop */
    printf("\nType events (or 'quit' to exit):\n");
    printf("  random  - generate a random number\n");
    printf("  time    - show current time\n");
    printf("  reload  - force script reload\n");
    printf("  <other> - pass as event to script\n\n");

    while (1) {
        /* Check for hot-reload before each command */
        script_check_reload(&eng);

        printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        /* Trim newline */
        input[strcspn(input, "\n")] = '\0';

        if (!strcmp(input, "quit") || !strcmp(input, "exit"))
            break;

        if (!strcmp(input, "reload")) {
            if (eng.source_path)
                script_load(&eng, eng.source_path);
            continue;
        }

        if (!strcmp(input, "time")) {
            t0 = host_time_sec();
            if (eng.on_event)
                eng.on_event("time", "");
            t1 = host_time_sec();
            printf("  Event dispatch time: %.6f sec\n", t1 - t0);
            continue;
        }

        /* Forward to script */
        if (eng.on_event) {
            t0 = host_time_sec();
            eng.on_event(input, "");
            t1 = host_time_sec();
            printf("  [%.6f sec]\n", t1 - t0);
        } else {
            printf("  No script loaded.\n");
        }
    }

    /* Cleanup */
    printf("\nShutting down...\n");
    script_unload(&eng);
    printf("Done. (%d reload(s))\n", eng.reload_count);
    return 0;
}
