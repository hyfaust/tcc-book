/*
 * scope_demo.c - Demonstrates scope, shadowing, and symbol table behavior
 *
 * This file illustrates how tcc's symbol table (Sym, sym_push, sym_pop,
 * sym_link, prev_tok) manages names across nested scopes. Compile with:
 *   tcc -c scope_demo.c
 * Use with Chapter 4, Exercise 3 to trace symbol table changes.
 */

/* ============================================================
 * Section 1: Global scope basics
 * ============================================================ */

int global_x = 10;              /* pushed to global_stack */
int global_y = 20;              /* pushed to global_stack */
static int global_z = 30;       /* pushed to global_stack, VT_STATIC */

/* ============================================================
 * Section 2: File-scope struct and typedef
 * ============================================================ */

struct point {
    int x;
    int y;
};

typedef int my_int;

/* ============================================================
 * Section 3: Function definition with parameter scope
 * ============================================================ */

int compute(int a, int b)
{
    /* At this point:
     *   local_stack: [sentinel] -> [param b] -> [param a]
     *   table_ident['a']->sym_identifier = param a (scope 1)
     *   table_ident['b']->sym_identifier = param b (scope 1)
     *   global_x, global_y still in global_stack
     */
    int result;                  /* pushed to local_stack, scope 1 */

    result = a + b;
    return result;
}

/* ============================================================
 * Section 4: Name shadowing with blocks
 * ============================================================ */

int shadow_demo(int x)
{
    /* Scope 1: parameter x is visible */
    int y = x * 2;              /* y pushed to local_stack, scope 1 */

    {
        /* Scope 2: new_scope() */
        int x = 100;           /* SHADOWS parameter x!
         * sym_push creates new Sym for 'x' at scope 2
         * sym_link(new_x, 1) sets:
         *   table_ident['x']->sym_identifier = new_x
         *   new_x->prev_tok = old_x (the parameter)
         */

        int z = x + y;         /* 'x' resolves to scope-2 x (value 100) */

        /* At this point:
         *   table_ident['x']->sym_identifier chain:
         *     scope-2 x -> scope-1 x (parameter) -> global (if any)
         */

        {
            /* Scope 3: nested new_scope() */
            int x = 999;       /* SHADOWS scope-2 x!
             * table_ident['x']->sym_identifier = scope-3 x
             * scope-3 x->prev_tok = scope-2 x
             */

            /* 'x' resolves to scope-3 x (value 999) */
            y = x + z;         /* y = 999 + 101 = 1100 */
        }
        /* prev_scope() for scope 3:
         * sym_pop pops scope-3 x
         * sym_link(scope-3 x, 0) restores:
         *   table_ident['x']->sym_identifier = scope-2 x
         */

        /* 'x' resolves to scope-2 x again (value 100) */
        y = y + x;             /* y = 1100 + 100 = 1200 */
    }
    /* prev_scope() for scope 2:
     * sym_pop pops scope-2 x and scope-2 z
     * sym_link(scope-2 x, 0) restores:
     *   table_ident['x']->sym_identifier = parameter x
     */

    /* 'x' resolves to parameter x again */
    return x + y;               /* x + 1200 */
}

/* ============================================================
 * Section 5: Local types (struct/enum in block scope)
 * ============================================================ */

int local_type_demo(int flag)
{
    int val = 0;

    if (flag) {
        /* new_scope_s() — simplified scope for if/while/switch */
        struct inner {
            int data;
        };

        struct inner s;        /* local variable using local struct */
        s.data = 42;
        val = s.data;
    }
    /* prev_scope_s() pops local symbols but not types in newer tcc versions */

    /* Note: struct inner is NOT visible here in C99+ strict mode.
     * tcc may or may not enforce this depending on version. */

    return val;
}

/* ============================================================
 * Section 6: for-loop scope (C99 declarations)
 * ============================================================ */

int for_scope_demo(void)
{
    int sum = 0;

    for (int i = 0; i < 10; i++) {
        /* new_scope() for the entire for statement
         * 'i' is pushed at the for-init scope
         */
        sum += i;
    }
    /* prev_scope() pops 'i'
     * table_ident['i']->sym_identifier is restored
     * 'i' is no longer accessible here
     */

    /* for (int i = 0; i < 5; i++) { }  -- OK, 'i' is in a new scope */

    return sum;
}

/* ============================================================
 * Section 7: Typedef shadowing
 * ============================================================ */

typedef int my_type;

int typedef_shadow_demo(void)
{
    my_type a = 10;             /* my_type resolves to global typedef (int) */

    {
        /* Scope 2 */
        typedef double my_type; /* SHADOWS global my_type!
         * sym_push creates new Sym for 'my_type' with VT_TYPEDEF|VT_DOUBLE
         * table_ident['my_type']->sym_identifier = new typedef
         * new->prev_tok = global my_type
         */

        my_type b = 3.14;      /* my_type resolves to double */
        (void)b;
    }
    /* prev_scope() restores global my_type */

    my_type c = 20;            /* my_type resolves to int again */
    return a + c;
}

/* ============================================================
 * Section 8: Enum constants in scope
 * ============================================================ */

enum status { OK = 0, ERROR = -1 };

int enum_scope_demo(int mode)
{
    /* OK and ERROR are visible here (scope 0/global) */
    int result = OK;

    {
        enum status { PENDING = 1, DONE = 2 };
        /* PENDING and DONE are pushed at scope 1
         * Note: 'status' tag may or may not be visible depending on tcc behavior
         */
        result = PENDING;
    }
    /* PENDING is no longer visible */

    if (mode) {
        result = ERROR;         /* ERROR is still visible (global enum) */
    }

    return result;
}

/* ============================================================
 * Section 9: Label scope (__label__ extension)
 * ============================================================ */

int label_scope_demo(int x)
{
    __label__ done;             /* local label declaration (GCC extension) */

    if (x > 0) {
        x = 1;
        goto done;
    }
    x = 0;

done:
    return x;
}

/* ============================================================
 * Section 10: Complex shadowing scenario
 * ============================================================ */

int outer = 1;

int complex_shadow(void)
{
    int result;

    {
        int outer = 2;         /* shadows global 'outer' */
        {
            int outer = 3;     /* shadows scope-2 'outer' */
            {
                int outer = 4; /* shadows scope-3 'outer' */
                result = outer;/* result = 4 */
            }
            /* restore scope-3 outer */
            result += outer;   /* result = 4 + 3 = 7 */
        }
        /* restore scope-2 outer */
        result += outer;       /* result = 7 + 2 = 9 */
    }
    /* restore global outer */
    result += outer;           /* result = 9 + 1 = 10 */

    return result;             /* 10 */
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void)
{
    int r;

    r = compute(3, 4);
    r = shadow_demo(5);
    r = local_type_demo(1);
    r = for_scope_demo();
    r = typedef_shadow_demo();
    r = enum_scope_demo(0);
    r = label_scope_demo(1);
    r = complex_shadow();

    (void)r;
    return 0;
}
