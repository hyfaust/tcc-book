/*
 * conditional.c - Demonstrates conditional compilation in TinyCC
 *
 * Compile with:
 *   tcc -E conditional.c                    # see preprocessed output
 *   tcc -DDEBUG_MODE -o cond conditional.c  # define DEBUG_MODE
 *   tcc -o cond conditional.c               # without DEBUG_MODE
 */

#include <stdio.h>

/* ============================================================
 * 1. #ifdef / #ifndef - basic include guard pattern
 * ============================================================ */

/* Simulated include guard (normally in a header file) */
#ifndef MY_HEADER_H
#define MY_HEADER_H

#define HEADER_VERSION 1

#endif /* MY_HEADER_H */

/* ============================================================
 * 2. #if / #elif / #else / #endif - value-based branching
 * ============================================================ */

/* Feature flags (for use in conditional compilation examples below) */
#define FEATURE_A 1
/* FEATURE_B intentionally not defined */

#define PLATFORM_LINUX 1
#define PLATFORM_WINDOWS 2
#define PLATFORM_MACOS 3

/* Detect platform */
#if defined(__linux__)
    #define CURRENT_PLATFORM PLATFORM_LINUX
    #define PLATFORM_NAME "Linux"
#elif defined(_WIN32)
    #define CURRENT_PLATFORM PLATFORM_WINDOWS
    #define PLATFORM_NAME "Windows"
#elif defined(__APPLE__)
    #define CURRENT_PLATFORM PLATFORM_MACOS
    #define PLATFORM_NAME "macOS"
#else
    #define CURRENT_PLATFORM 0
    #define PLATFORM_NAME "Unknown"
#endif

/* ============================================================
 * 3. Nested conditional compilation
 * ============================================================ */

#define FEATURE_A 1
#define FEATURE_B 0
#define FEATURE_C 1

/* ============================================================
 * 4. #if with arithmetic expressions
 * ============================================================ */

#define VERSION_MAJOR 2
#define VERSION_MINOR 5
#define VERSION_PATCH 3

#define VERSION_CODE  (VERSION_MAJOR * 10000 + VERSION_MINOR * 100 + VERSION_PATCH)

/* ============================================================
 * 5. defined() operator
 * ============================================================ */

/* defined() can be used with or without parentheses */
#if defined FEATURE_A && !defined FEATURE_B
    #define FEATURE_A_ONLY 1
#endif

/* ============================================================
 * 6. Conditional debug macros
 * ============================================================ */

#ifdef DEBUG_MODE
    #define DBG_LOG(fmt, ...) \
        fprintf(stderr, "[DEBUG %s:%d] " fmt "\n", \
                __FILE__, __LINE__, ##__VA_ARGS__)
    #define DBG_ASSERT(cond) \
        do { \
            if (!(cond)) { \
                fprintf(stderr, "[ASSERT FAILED] %s (%s:%d)\n", \
                        #cond, __FILE__, __LINE__); \
            } \
        } while(0)
#else
    #define DBG_LOG(fmt, ...)   /* nothing */
    #define DBG_ASSERT(cond)    /* nothing */
#endif

/* ============================================================
 * 7. Header guard with #ifndef at file beginning
 *    (TinyCC's CachedInclude optimization detects this pattern)
 * ============================================================ */

#ifndef CONFIG_H
#define CONFIG_H

#define MAX_CONNECTIONS 128
#define TIMEOUT_SECONDS 30

#endif /* CONFIG_H */

/* ============================================================
 * 8. Multiple #elif chains
 * ============================================================ */

#if VERSION_CODE >= 30000
    #define VERSION_STR "3.x or later"
#elif VERSION_CODE >= 20000
    #define VERSION_STR "2.x"
#elif VERSION_CODE >= 10000
    #define VERSION_STR "1.x"
#else
    #define VERSION_STR "0.x (pre-release)"
#endif

/* ============================================================
 * 9. Conditional compilation with enum/struct
 * ============================================================ */

#if FEATURE_C
typedef struct {
    int x;
    int y;
    int z;
} Point3D;
#else
typedef struct {
    int x;
    int y;
} Point2D;
#endif

int main(void)
{
    printf("=== Conditional Compilation Demo ===\n\n");

    /* Platform detection */
    printf("Platform: %s (code=%d)\n", PLATFORM_NAME, CURRENT_PLATFORM);

    /* Version info */
    printf("Version code: %d\n", VERSION_CODE);
    printf("Version range: %s\n", VERSION_STR);
    printf("Header guard version: %d\n", HEADER_VERSION);

    /* Feature flags */
    printf("\nFeature A: %s\n", FEATURE_A ? "enabled" : "disabled");
    printf("Feature B: %s\n", FEATURE_B ? "enabled" : "disabled");
    printf("Feature C: %s\n", FEATURE_C ? "enabled" : "disabled");
    printf("Feature A only: %s\n",
#ifdef FEATURE_A_ONLY
           FEATURE_A_ONLY ? "yes" : "no"
#else
           "no (FEATURE_A_ONLY not defined)"
#endif
           );

    /* Debug macros */
    DBG_LOG("starting main");
    DBG_ASSERT(1 + 1 == 2);
    DBG_ASSERT(VERSION_CODE > 0);
    printf("Debug mode: %s\n",
#ifdef DEBUG_MODE
           "enabled (compile with -DDEBUG_MODE)"
#else
           "disabled (compile without -DDEBUG_MODE)"
#endif
    );

    /* Conditional struct */
    printf("\nStruct size: %zu bytes\n",
#if FEATURE_C
           sizeof(Point3D)
#else
           sizeof(Point2D)
#endif
    );

    /* Nested conditions */
#if FEATURE_A
    #if FEATURE_B
        printf("Both A and B are enabled\n");
    #elif FEATURE_C
        printf("A and C are enabled, B is disabled\n");
    #else
        printf("Only A is enabled\n");
    #endif
#else
    printf("A is disabled\n");
#endif

    /* Platform-specific code */
    printf("\nPlatform-specific paths:\n");
#if CURRENT_PLATFORM == PLATFORM_LINUX
    printf("  Config: /etc/myapp.conf\n");
#elif CURRENT_PLATFORM == PLATFORM_WINDOWS
    printf("  Config: %%APPDATA%%\\myapp\\config.ini\n");
#elif CURRENT_PLATFORM == PLATFORM_MACOS
    printf("  Config: ~/Library/Preferences/myapp.plist\n");
#else
    printf("  Config: ./myapp.conf\n");
#endif

    /* Test __has_include (C23 feature, supported by TinyCC) */
#if __has_include(<stdint.h>)
    printf("\n<stdint.h> is available\n");
#endif

    printf("\nAll conditional compilation demos completed.\n");
    return 0;
}
