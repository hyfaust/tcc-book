/*
 * types_demo.c - Demonstrates all C type constructs
 *
 * This file covers every major type construct that tcc's parser
 * and type system must handle. Compile with:
 *   tcc -c types_demo.c
 * Use with Chapter 4 exercises to trace how tcc encodes each type.
 */

/* ============================================================
 * Section 1: Basic integer types
 * ============================================================ */

char                a1;         /* VT_BYTE (signed by default on most targets) */
signed char         a2;         /* VT_BYTE | VT_DEFSIGN */
unsigned char       a3;         /* VT_BYTE | VT_UNSIGNED | VT_DEFSIGN */

short               b1;         /* VT_SHORT */
signed short        b2;         /* VT_SHORT | VT_DEFSIGN */
unsigned short      b3;         /* VT_SHORT | VT_UNSIGNED | VT_DEFSIGN */
short int           b4;         /* VT_SHORT | VT_INT (normalized to VT_SHORT) */

int                 c1;         /* VT_INT */
signed int          c2;         /* VT_INT | VT_DEFSIGN */
unsigned int        c3;         /* VT_INT | VT_UNSIGNED | VT_DEFSIGN */

long                d1;         /* VT_INT|VT_LONG (32-bit) or VT_LLONG|VT_LONG (64-bit) */
long int            d2;         /* same as long */
unsigned long       d3;         /* VT_INT|VT_LONG|VT_UNSIGNED or VT_LLONG|VT_LONG|VT_UNSIGNED */

long long           e1;         /* VT_LLONG | VT_LONG */
long long int       e2;         /* same */
unsigned long long  e3;         /* VT_LLONG | VT_LONG | VT_UNSIGNED */

/* ============================================================
 * Section 2: Floating-point types
 * ============================================================ */

float               f1;         /* VT_FLOAT */
double              f2;         /* VT_DOUBLE */
long double         f3;         /* VT_LDOUBLE (encoded directly, not VT_LONG|VT_DOUBLE) */

/* ============================================================
 * Section 3: Boolean and void
 * ============================================================ */

_Bool               g1;         /* VT_BOOL */
/* void cannot be used as a variable type */

/* ============================================================
 * Section 4: Pointer types
 * ============================================================ */

int                *h1;         /* VT_PTR -> Sym{type.t=VT_INT} */
char               *h2;         /* VT_PTR -> Sym{type.t=VT_BYTE} */
void               *h3;         /* VT_PTR -> Sym{type.t=VT_VOID} */
double             *h4;         /* VT_PTR -> Sym{type.t=VT_DOUBLE} */
int               **h5;         /* VT_PTR -> Sym{type.t=VT_PTR -> Sym{type.t=VT_INT}} */
const int          *h6;         /* VT_PTR -> Sym{type.t=VT_INT|VT_CONSTANT} */
int *const          h7;         /* VT_PTR|VT_CONSTANT -> Sym{type.t=VT_INT} */
volatile int       *h8;         /* VT_PTR -> Sym{type.t=VT_INT|VT_VOLATILE} */

/* ============================================================
 * Section 5: Array types
 * ============================================================ */

int                 i1[10];     /* VT_ARRAY|VT_PTR -> Sym{c=10, type.t=VT_INT} */
char                i2[100];    /* VT_ARRAY|VT_PTR -> Sym{c=100, type.t=VT_BYTE} */
int                 i3[3][5];   /* VT_ARRAY|VT_PTR -> Sym{c=3,
                                     type.t=VT_ARRAY|VT_PTR -> Sym{c=5, type.t=VT_INT}} */
const char         *i4[4];     /* VT_ARRAY|VT_PTR -> Sym{c=4,
                                     type.t=VT_PTR -> Sym{type.t=VT_BYTE|VT_CONSTANT}} */

/* Incomplete array (extern or with initializer) */
extern int          i5[];       /* VT_ARRAY|VT_PTR -> Sym{c=-1, type.t=VT_INT} */

/* ============================================================
 * Section 6: Function types
 * ============================================================ */

/* Simple function declaration */
int foo(int x, double y);       /* VT_FUNC -> Sym{type.t=VT_INT, f.func_type=FUNC_NEW,
                                     next -> Sym{v='x', type.t=VT_INT},
                                     next -> Sym{v='y', type.t=VT_DOUBLE}} */

/* Variadic function */
int bar(int count, ...);        /* VT_FUNC, f.func_type=FUNC_ELLIPSIS */

/* Old-style function (K&R) */
int baz();                      /* VT_FUNC, f.func_type=FUNC_OLD */

/* Function returning pointer */
int *ret_ptr(int n);            /* VT_FUNC -> Sym{type.t=VT_PTR -> Sym{type.t=VT_INT}} */

/* Function returning void */
void do_nothing(void);          /* VT_FUNC -> Sym{type.t=VT_VOID} */

/* ============================================================
 * Section 7: Function pointer types
 * ============================================================ */

/* Pointer to function taking int, returning int */
int   (*fp1)(int);              /* VT_PTR -> Sym{type.t=VT_FUNC, ...} */

/* Pointer to variadic function */
int   (*fp2)(int, ...);         /* VT_PTR -> Sym{type.t=VT_FUNC, f.func_type=FUNC_ELLIPSIS} */

/* Pointer to function returning pointer */
int *(*fp3)(void);              /* VT_PTR -> Sym{type.t=VT_FUNC, type.ref->type=VT_PTR->VT_INT} */

/* Array of function pointers */
int (*fp4[5])(double);          /* VT_ARRAY|VT_PTR -> Sym{c=5,
                                     type.t=VT_PTR -> Sym{type.t=VT_FUNC, ...}} */

/* ============================================================
 * Section 8: Struct and union types
 * ============================================================ */

struct point {
    int x;                      /* SYM_FIELD, type.t=VT_INT, c=0 (offset) */
    int y;                      /* SYM_FIELD, type.t=VT_INT, c=4 (offset) */
};

struct rect {
    struct point top_left;      /* SYM_FIELD, type.t=VT_STRUCT, c=0 */
    struct point bottom_right;  /* SYM_FIELD, type.t=VT_STRUCT, c=8 */
};

/* Anonymous struct member (C11) */
struct container {
    int id;
    struct {                    /* Anonymous struct */
        int a;
        int b;
    };
};

union data {
    int    i;                   /* SYM_FIELD, c=0 */
    float  f;                   /* SYM_FIELD, c=0 (same offset as i) */
    char   str[8];              /* SYM_FIELD, c=0 */
};

/* Bitfield type */
struct flags {
    unsigned int active   : 1;  /* VT_BITFIELD | VT_INT | VT_UNSIGNED, shift=0, width=1 */
    unsigned int mode     : 3;  /* VT_BITFIELD | VT_INT | VT_UNSIGNED, shift=1, width=3 */
    unsigned int color    : 12; /* VT_BITFIELD | VT_INT | VT_UNSIGNED, shift=4, width=12 */
    int          reserved : 16; /* VT_BITFIELD | VT_INT, shift=16, width=16 */
};

/* ============================================================
 * Section 9: Enum types
 * ============================================================ */

enum color {
    RED,                        /* VT_ENUM_VAL, enum_val=0 */
    GREEN = 5,                  /* VT_ENUM_VAL, enum_val=5 */
    BLUE,                       /* VT_ENUM_VAL, enum_val=6 */
    ALPHA = 255                 /* VT_ENUM_VAL, enum_val=255 */
};

enum color current_color;       /* VT_INT (enum stored as int), ref -> enum color Sym */

/* ============================================================
 * Section 10: Typedef types
 * ============================================================ */

typedef int                     my_int;         /* VT_TYPEDEF | VT_INT */
typedef unsigned long           size_t_demo;    /* VT_TYPEDEF | VT_INT|VT_LONG|VT_UNSIGNED */
typedef struct point            point_t;        /* VT_TYPEDEF | VT_STRUCT */
typedef int (*callback_t)(int); /* VT_TYPEDEF | VT_PTR -> Sym{type.t=VT_FUNC} */
typedef void (*sighandler_t)(int);

/* ============================================================
 * Section 11: Storage class specifiers
 * ============================================================ */

int             sc1;            /* no storage specifier */
static int      sc2;            /* VT_STATIC | VT_INT */
extern int      sc3;            /* VT_EXTERN | VT_INT */
static int      sc4 = 42;      /* VT_STATIC | VT_INT, has initializer */

/* ============================================================
 * Section 12: Type qualifiers
 * ============================================================ */

const int           q1 = 10;    /* VT_CONSTANT | VT_INT */
volatile int        q2;         /* VT_VOLATILE | IT */
const volatile int  q3;         /* VT_CONSTANT | VT_VOLATILE | VT_INT */
int const           q4;         /* same as const int (order doesn't matter) */

/* ============================================================
 * Section 13: Inline functions
 * ============================================================ */

static inline int max(int a, int b)
{
    return a > b ? a : b;
}

/* ============================================================
 * Section 14: Compound literals (C99)
 * ============================================================ */

struct point origin(void)
{
    /* Compound literal: creates a temporary struct */
    return (struct point){ 0, 0 };
}

/* ============================================================
 * Section 15: Complex declaration examples
 * ============================================================ */

/* Signal handler: pointer to function (int) -> void */
void (*signal_handler)(int);

/* Array of 10 pointers to functions (int) -> int */
int (*dispatch_table[10])(int);

/* Function returning pointer to array of 5 ints */
int (*get_row(int idx))[5];

/* Pointer to array of 3 pointers to functions
   taking (double, ...) -> char* */
char *(*(*pafp[3])(double, ...));

/* const pointer to volatile int */
volatile int * const volatile_ptr = 0;

/* Array of const pointers to functions */
typedef void (*event_handler_t)(int);
const event_handler_t handlers[8];

/* ============================================================
 * Section 16: Main function using some of the above
 * ============================================================ */

int main(void)
{
    struct point p;
    enum color c;
    int (*fn_ptr)(int) = 0;
    int arr[5];

    p.x = 10;
    p.y = 20;
    c = RED;
    arr[0] = max(p.x, p.y);

    return 0;
}
