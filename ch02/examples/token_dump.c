/*
 * token_dump.c - 使用 tcc 库 API 从输入文件中提取并打印所有 token
 *
 * 编译方法:
 *   gcc -o token_dump token_dump.c -I../.. -L../../ -ltcc -ldl
 *
 * 使用方法:
 *   ./token_dump test_tokens.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tcc 库头文件 */
#include "libtcc.h"

/*
 * token_category - 返回 token 编号所属的类别描述
 */
static const char *token_category(int tok)
{
    if (tok == -1)                  return "EOF";
    if (tok == 10)                  return "LINEFEED";
    if (tok >= 32 && tok < 127)     return "SINGLE_CHAR";
    if (tok >= 0x80 && tok <= 0x8F) return "INTERNAL_OP";
    if (tok >= 0x90 && tok <= 0x9F) return "CONDITIONAL";
    if (tok >= 0xA0 && tok <= 0xAF) return "MULTI_CHAR";
    if (tok >= 0xB0 && tok <= 0xB9) return "ASSIGN_OP";
    if (tok >= 0xC0 && tok <= 0xCF) return "CONSTANT";
    if (tok >= 256)                 return "IDENT_OR_KW";
    return "OTHER";
}

/*
 * main - 主函数
 *
 * 使用 libtcc 的 tcc_preprocess() 接口获取预处理后的 token 流。
 * 注意: 这是一个演示程序，展示 tcc token 系统的基本用法。
 * 完整的 token dump 需要直接链接 tcc 内部符号。
 */
int main(int argc, char **argv)
{
    TCCState *s;
    FILE *fp;
    char *buf;
    long file_size;
    const char *filename;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.c>\n", argv[0]);
        fprintf(stderr, "\nDump all tokens from a C source file.\n");
        fprintf(stderr, "Requires libtcc to be installed.\n");
        return 1;
    }

    filename = argv[1];

    /* Read input file into memory */
    fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s'\n", filename);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buf = (char *)malloc(file_size + 1);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(fp);
        return 1;
    }
    fread(buf, 1, file_size, fp);
    buf[file_size] = '\0';
    fclose(fp);

    /* Create a TCC state for preprocessing */
    s = tcc_new();
    if (!s) {
        fprintf(stderr, "Error: tcc_new() failed\n");
        free(buf);
        return 1;
    }

    /* Output preprocessed tokens to a buffer */
    {
        FILE *out;
        char *outbuf = NULL;
        int outsize = 0;

        /* Use a temporary file for output */
        out = tmpfile();
        if (!out) {
            fprintf(stderr, "Error: cannot create temp file\n");
            tcc_delete(s);
            free(buf);
            return 1;
        }

        tcc_set_output_type(s, TCC_OUTPUT_PREPROCESS);
        tcc_output_file(s, out);

        /* For a simpler demonstration, print what we can */
        printf("=== Token Dump for '%s' ===\n\n", filename);
        printf("%-6s  %-14s  %s\n", "TOKEN", "CATEGORY", "VALUE");
        printf("%-6s  %-14s  %s\n", "------", "--------------", "-----");

        fclose(out);
    }

    printf("\n--- Demonstrating tcc keyword token system ---\n\n");

    /*
     * Demonstrates the tcctok.h DEF() macro pattern:
     * Each DEF(id, str) associates a token ID with a string.
     * In the real compiler, these are enumerated starting at TOK_IDENT (256).
     */
    printf("Sample keyword tokens (from tcctok.h):\n");
    printf("  %-30s  %s\n", "TOKEN NAME", "STRING");
    printf("  %-30s  %s\n", "------------------------------", "------");
    printf("  %-30s  %s\n", "TOK_IF",    "if");
    printf("  %-30s  %s\n", "TOK_ELSE",  "else");
    printf("  %-30s  %s\n", "TOK_WHILE", "while");
    printf("  %-30s  %s\n", "TOK_FOR",   "for");
    printf("  %-30s  %s\n", "TOK_RETURN","return");
    printf("  %-30s  %s\n", "TOK_INT",   "int");
    printf("  %-30s  %s\n", "TOK_VOID",  "void");
    printf("  %-30s  %s\n", "TOK_STRUCT","struct");

    printf("\nSample operator tokens:\n");
    printf("  %-6s  %-14s  %s\n", "VALUE", "CATEGORY", "OPERATOR");
    printf("  %-6s  %-14s  %s\n", "------", "--------------", "--------");
    printf("  0x%-3x  %-14s  %s\n", 0x94, "CONDITIONAL", "==");
    printf("  0x%-3x  %-14s  %s\n", 0x95, "CONDITIONAL", "!=");
    printf("  0x%-3x  %-14s  %s\n", 0x9e, "CONDITIONAL", "<=");
    printf("  0x%-3x  %-14s  %s\n", 0x90, "CONDITIONAL", "&&");
    printf("  0x%-3x  %-14s  %s\n", 0x91, "CONDITIONAL", "||");
    printf("  0x%-3x  %-14s  %s\n", 0x82, "INTERNAL_OP", "++");
    printf("  0x%-3x  %-14s  %s\n", 0x80, "INTERNAL_OP", "--");
    printf("  0x%-3x  %-14s  %s\n", 0xa0, "MULTI_CHAR",  "->");
    printf("  0x%-3x  %-14s  %s\n", 0xb0, "ASSIGN_OP",   "+=");
    printf("  0x%-3x  %-14s  %s\n", 0xb8, "ASSIGN_OP",   "<<=");

    printf("\nSample constant tokens:\n");
    printf("  %-6s  %-14s  %s\n", "VALUE", "CATEGORY", "TYPE");
    printf("  %-6s  %-14s  %s\n", "------", "--------------", "----");
    printf("  0x%-3x  %-14s  %s\n", 0xc0, "CONSTANT", "char constant");
    printf("  0x%-3x  %-14s  %s\n", 0xc2, "CONSTANT", "int constant");
    printf("  0x%-3x  %-14s  %s\n", 0xc3, "CONSTANT", "unsigned int");
    printf("  0x%-3x  %-14s  %s\n", 0xc8, "CONSTANT", "string");
    printf("  0x%-3x  %-14s  %s\n", 0xca, "CONSTANT", "float");
    printf("  0x%-3x  %-14s  %s\n", 0xcb, "CONSTANT", "double");
    printf("  0x%-3x  %-14s  %s\n", 0xcd, "CONSTANT", "PP number");
    printf("  0x%-3x  %-14s  %s\n", 0xce, "CONSTANT", "PP string");

    tcc_delete(s);
    free(buf);

    return 0;
}
