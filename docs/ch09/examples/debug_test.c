/*
 * debug_test.c - A C file designed for debugging with GDB
 *
 * Compile with:
 *   tcc -g -o debug_test debug_test.c      (STAB format)
 *   tcc -gdwarf -o debug_test debug_test.c  (DWARF format)
 *
 * Debug with:
 *   gdb ./debug_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== Data structures for testing ========== */

typedef struct point {
    double x;
    double y;
} point_t;

typedef struct rectangle {
    point_t origin;
    double width;
    double height;
    char name[32];
} rect_t;

enum color { RED = 0, GREEN = 1, BLUE = 2, WHITE = 7, BLACK = 8 };

typedef struct {
    int id;
    enum color fill;
    enum color stroke;
    rect_t bounds;
} shape_t;

/* ========== Functions for testing step/next ========== */

double point_distance(const point_t *a, const point_t *b)
{
    double dx = b->x - a->x;
    double dy = b->y - a->y;
    return dx * dx + dy * dy;  /* Note: squared distance */
}

double rect_area(const rect_t *r)
{
    return r->width * r->height;
}

double rect_perimeter(const rect_t *r)
{
    return 2.0 * (r->width + r->height);
}

int point_in_rect(const point_t *p, const rect_t *r)
{
    return (p->x >= r->origin.x &&
            p->x <= r->origin.x + r->width &&
            p->y >= r->origin.y &&
            p->y <= r->origin.y + r->height);
}

/* ========== Recursive function for testing backtrace ========== */

int fibonacci(int n)
{
    if (n <= 1)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

/* ========== Loop testing ========== */

int sum_array(const int *arr, int len)
{
    int sum = 0;
    int i;
    for (i = 0; i < len; i++) {
        sum += arr[i];
    }
    return sum;
}

/* ========== Pointer and array testing ========== */

void swap(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void bubble_sort(int arr[], int n)
{
    int i, j;
    for (i = 0; i < n - 1; i++) {
        for (j = 0; j < n - 1 - i; j++) {
            if (arr[j] > arr[j + 1]) {
                swap(&arr[j], &arr[j + 1]);
            }
        }
    }
}

/* ========== String operations ========== */

char *build_greeting(const char *name, int count)
{
    /* Dynamic allocation - test with 'print' in GDB */
    size_t len = strlen("Hello, ") + strlen(name) + 20;
    char *buf = malloc(len);
    if (!buf) return NULL;
    snprintf(buf, len, "Hello, %s! (visit #%d)", name, count);
    return buf;
}

/* ========== Main: exercise all the above ========== */

int main(void)
{
    /* Local variables of various types for debugging tests */
    int i, n;
    double result;
    char *msg;

    /* Point and rectangle */
    point_t p1 = {1.0, 2.0};
    point_t p2 = {4.0, 6.0};
    rect_t rect;
    shape_t shape;

    /* Array */
    int numbers[] = {64, 34, 25, 12, 22, 11, 90};
    int num_count = sizeof(numbers) / sizeof(numbers[0]);

    /* Set up the rectangle */
    rect.origin.x = 0.0;
    rect.origin.y = 0.0;
    rect.width = 10.0;
    rect.height = 5.0;
    strncpy(rect.name, "TestRect", sizeof(rect.name) - 1);
    rect.name[sizeof(rect.name) - 1] = '\0';

    /* Set up the shape */
    shape.id = 1;
    shape.fill = BLUE;
    shape.stroke = BLACK;
    shape.bounds = rect;

    /* Test 1: Basic arithmetic and locals */
    printf("=== Test 1: Basic Locals ===\n");
    n = 10;
    result = 0.0;
    for (i = 0; i < n; i++) {
        result += i * 0.5;
    }
    printf("Sum of 0..%d * 0.5 = %.2f\n", n, result);
    /* GDB: break here, print n, result, i */

    /* Test 2: Struct and pointer operations */
    printf("\n=== Test 2: Structs and Pointers ===\n");
    double dist = point_distance(&p1, &p2);
    printf("Distance(p1, p2) = %.4f\n", dist);

    double area = rect_area(&rect);
    printf("Rectangle '%s': area=%.2f, perimeter=%.2f\n",
           rect.name, area, rect_perimeter(&rect));

    printf("Shape %d: fill=%d, stroke=%d\n",
           shape.id, shape.fill, shape.stroke);
    /* GDB: print p1, p2, rect, shape */

    /* Test 3: Point-in-rectangle test */
    printf("\n=== Test 3: Point In Rectangle ===\n");
    point_t test_points[] = {{5, 3}, {-1, 0}, {11, 6}, {0, 0}};
    for (i = 0; i < 4; i++) {
        int inside = point_in_rect(&test_points[i], &rect);
        printf("  (%.0f, %.0f) in rect: %s\n",
               test_points[i].x, test_points[i].y,
               inside ? "YES" : "NO");
    }

    /* Test 4: Array and sorting */
    printf("\n=== Test 4: Array Operations ===\n");
    printf("Before sort: ");
    for (i = 0; i < num_count; i++) printf("%d ", numbers[i]);
    printf("\n");

    bubble_sort(numbers, num_count);

    printf("After sort:  ");
    for (i = 0; i < num_count; i++) printf("%d ", numbers[i]);
    printf("\n");

    int total = sum_array(numbers, num_count);
    printf("Sum = %d\n", total);
    /* GDB: break bubble_sort, step through, watch arr[] */

    /* Test 5: Recursion */
    printf("\n=== Test 5: Recursion ===\n");
    for (i = 0; i <= 15; i++)
        printf("fib(%d) = %d\n", i, fibonacci(i));
    /* GDB: break fibonacci, bt to see call stack */

    /* Test 6: Dynamic memory */
    printf("\n=== Test 6: Dynamic Memory ===\n");
    for (i = 1; i <= 3; i++) {
        msg = build_greeting("World", i);
        printf("  %s\n", msg);
        free(msg);
    }
    /* GDB: break build_greeting, print buf content after snprintf */

    /* Test 7: Enum and switch */
    printf("\n=== Test 7: Enum and Switch ===\n");
    enum color colors[] = {RED, GREEN, BLUE, WHITE, BLACK};
    for (i = 0; i < 5; i++) {
        const char *name;
        switch (colors[i]) {
        case RED:   name = "Red";   break;
        case GREEN: name = "Green"; break;
        case BLUE:  name = "Blue";  break;
        case WHITE: name = "White"; break;
        case BLACK: name = "Black"; break;
        default:    name = "Unknown"; break;
        }
        printf("  Color %d: %s\n", colors[i], name);
    }
    /* GDB: print colors, print name */

    printf("\n=== All tests passed ===\n");
    return 0;
}
