/*
 * read_elf.c - A minimal ELF reader that parses headers
 *
 * This program reads and displays the ELF header, section headers,
 * and symbol table of an ELF file. It demonstrates the structures
 * and concepts discussed in Chapter 6.
 *
 * Compile: tcc -o read_elf read_elf.c
 * Usage:   ./read_elf <elf_file>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ELF magic bytes */
#define ELFMAG      "\177ELF"
#define SELFMAG     4

/* ELF class */
#define ELFCLASS32  1
#define ELFCLASS64  2

/* ELF data encoding */
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

/* ELF type */
#define ET_NONE     0
#define ET_REL      1
#define ET_EXEC     2
#define ET_DYN      3
#define ET_CORE     4

/* ELF machine */
#define EM_386      3
#define EM_X86_64   62
#define EM_AARCH64  183
#define EM_RISCV    243

/* Section types */
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_DYNAMIC     6
#define SHT_NOTE        7
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHT_DYNSYM      11
#define SHT_INIT_ARRAY  14
#define SHT_FINI_ARRAY  15
#define SHT_GNU_HASH    0x6ffffff6
#define SHT_GNU_verdef  0x6ffffffd
#define SHT_GNU_verneed 0x6ffffffe
#define SHT_GNU_versym  0x6fffffff

/* Section flags */
#define SHF_WRITE       (1 << 0)
#define SHF_ALLOC       (1 << 1)
#define SHF_EXECINSTR   (1 << 2)
#define SHF_TLS         (1 << 10)

/* Symbol binding */
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2

/* Symbol type */
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_COMMON  5
#define STT_TLS     6

/* Special section indices */
#define SHN_UNDEF   0
#define SHN_ABS     0xfff1
#define SHN_COMMON  0xfff2

/* ELF64 header */
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

/* ELF64 section header */
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

/* ELF64 symbol table entry */
typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

/* ELF64 relocation entry (with addend) */
typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

#define ELF64_ST_BIND(i)    ((i) >> 4)
#define ELF64_ST_TYPE(i)    ((i) & 0xf)
#define ELF64_R_SYM(i)      ((i) >> 32)
#define ELF64_R_TYPE(i)     ((i) & 0xffffffff)

/* Helper: get human-readable type name */
static const char *elf_type_name(uint16_t type)
{
    switch (type) {
    case ET_NONE: return "NONE (No file type)";
    case ET_REL:  return "REL (Relocatable file)";
    case ET_EXEC: return "EXEC (Executable file)";
    case ET_DYN:  return "DYN (Shared object file)";
    case ET_CORE: return "CORE (Core file)";
    default:      return "Unknown";
    }
}

static const char *elf_machine_name(uint16_t machine)
{
    switch (machine) {
    case EM_386:     return "Intel 80386";
    case EM_X86_64:  return "AMD x86-64";
    case EM_AARCH64: return "AArch64";
    case EM_RISCV:   return "RISC-V";
    default:         return "Unknown";
    }
}

static const char *sht_name(uint32_t type)
{
    switch (type) {
    case SHT_NULL:        return "NULL";
    case SHT_PROGBITS:    return "PROGBITS";
    case SHT_SYMTAB:      return "SYMTAB";
    case SHT_STRTAB:      return "STRTAB";
    case SHT_RELA:        return "RELA";
    case SHT_HASH:        return "HASH";
    case SHT_DYNAMIC:     return "DYNAMIC";
    case SHT_NOTE:        return "NOTE";
    case SHT_NOBITS:      return "NOBITS";
    case SHT_REL:         return "REL";
    case SHT_DYNSYM:      return "DYNSYM";
    case SHT_INIT_ARRAY:  return "INIT_ARRAY";
    case SHT_FINI_ARRAY:  return "FINI_ARRAY";
    case SHT_GNU_HASH:    return "GNU_HASH";
    case SHT_GNU_verdef:  return "GNU_verdef";
    case SHT_GNU_verneed: return "GNU_verneed";
    case SHT_GNU_versym:  return "GNU_versym";
    default:              return "UNKNOWN";
    }
}

static const char *stb_name(uint8_t bind)
{
    switch (bind) {
    case STB_LOCAL:  return "LOCAL";
    case STB_GLOBAL: return "GLOBAL";
    case STB_WEAK:   return "WEAK";
    default:         return "UNKNOWN";
    }
}

static const char *stt_name(uint8_t type)
{
    switch (type) {
    case STT_NOTYPE:  return "NOTYPE";
    case STT_OBJECT:  return "OBJECT";
    case STT_FUNC:    return "FUNC";
    case STT_SECTION: return "SECTION";
    case STT_FILE:    return "FILE";
    case STT_COMMON:  return "COMMON";
    case STT_TLS:     return "TLS";
    default:          return "UNKNOWN";
    }
}

static const char *shndx_name(uint16_t shndx)
{
    switch (shndx) {
    case SHN_UNDEF:  return "UND";
    case SHN_ABS:    return "ABS";
    case SHN_COMMON: return "COM";
    default:         return NULL;
    }
}

static const char *reloc_type_name_x86_64(uint32_t type)
{
    switch (type) {
    case 0:  return "R_X86_64_NONE";
    case 1:  return "R_X86_64_64";
    case 2:  return "R_X86_64_PC32";
    case 4:  return "R_X86_64_PLT32";
    case 5:  return "R_X86_64_COPY";
    case 6:  return "R_X86_64_GLOB_DAT";
    case 7:  return "R_X86_64_JUMP_SLOT";
    case 8:  return "R_X86_64_RELATIVE";
    case 9:  return "R_X86_64_GOTPCREL";
    case 10: return "R_X86_64_32";
    case 11: return "R_X86_64_32S";
    default: return "UNKNOWN";
    }
}

static void print_flags(uint64_t flags)
{
    if (flags & SHF_WRITE)     printf("WRITE ");
    if (flags & SHF_ALLOC)     printf("ALLOC ");
    if (flags & SHF_EXECINSTR) printf("EXECINSTR ");
    if (flags & SHF_TLS)       printf("TLS ");
}

/* Read ELF header */
static int read_elf_header(FILE *f, Elf64_Ehdr *ehdr)
{
    if (fread(ehdr, sizeof(*ehdr), 1, f) != 1)
        return -1;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Error: not an ELF file\n");
        return -1;
    }
    if (ehdr->e_ident[4] != ELFCLASS64) {
        fprintf(stderr, "Error: not a 64-bit ELF file\n");
        return -1;
    }
    return 0;
}

/* Display ELF header */
static void print_elf_header(const Elf64_Ehdr *ehdr)
{
    printf("=== ELF Header ===\n");
    printf("  Magic:   ");
    for (int i = 0; i < 16; i++)
        printf("%02x ", ehdr->e_ident[i]);
    printf("\n");
    printf("  Class:                             ELF64\n");
    printf("  Data:                              %s\n",
           ehdr->e_ident[5] == ELFDATA2LSB ? "Little-endian" : "Big-endian");
    printf("  Version:                           %d\n", ehdr->e_ident[6]);
    printf("  OS/ABI:                            %d\n", ehdr->e_ident[7]);
    printf("  Type:                              %s\n", elf_type_name(ehdr->e_type));
    printf("  Machine:                           %s\n", elf_machine_name(ehdr->e_machine));
    printf("  Entry point:                       0x%lx\n", (unsigned long)ehdr->e_entry);
    printf("  Program header offset:             %ld (0x%lx)\n",
           (long)ehdr->e_phoff, (unsigned long)ehdr->e_phoff);
    printf("  Section header offset:             %ld (0x%lx)\n",
           (long)ehdr->e_shoff, (unsigned long)ehdr->e_shoff);
    printf("  Flags:                             0x%x\n", ehdr->e_flags);
    printf("  ELF header size:                   %d\n", ehdr->e_ehsize);
    printf("  Program header entry size:         %d\n", ehdr->e_phentsize);
    printf("  Program header entry count:        %d\n", ehdr->e_phnum);
    printf("  Section header entry size:         %d\n", ehdr->e_shentsize);
    printf("  Section header entry count:        %d\n", ehdr->e_shnum);
    printf("  Section header string table index: %d\n", ehdr->e_shstrndx);
    printf("\n");
}

/* Read section headers */
static Elf64_Shdr *read_section_headers(FILE *f, const Elf64_Ehdr *ehdr)
{
    Elf64_Shdr *shdr = malloc(sizeof(Elf64_Shdr) * ehdr->e_shnum);
    if (!shdr) return NULL;
    fseek(f, ehdr->e_shoff, SEEK_SET);
    if (fread(shdr, sizeof(Elf64_Shdr), ehdr->e_shnum, f) != ehdr->e_shnum) {
        free(shdr);
        return NULL;
    }
    return shdr;
}

/* Read a string from a string table section */
static const char *read_string(FILE *f, const Elf64_Shdr *strtab, uint32_t offset)
{
    static char buf[256];
    long saved = ftell(f);
    fseek(f, strtab->sh_offset + offset, SEEK_SET);
    size_t i = 0;
    int c;
    while (i < sizeof(buf) - 1 && (c = fgetc(f)) != EOF && c != 0)
        buf[i++] = c;
    buf[i] = 0;
    fseek(f, saved, SEEK_SET);
    return buf;
}

/* Display section headers */
static void print_section_headers(FILE *f, const Elf64_Ehdr *ehdr, Elf64_Shdr *shdr)
{
    const Elf64_Shdr *shstrtab = &shdr[ehdr->e_shstrndx];

    printf("=== Section Headers ===\n");
    printf("  %-5s %-18s %-12s %-18s %-10s %-10s %-5s %-5s %-5s %-5s\n",
           "Nr", "Name", "Type", "Addr", "Off", "Size", "ES", "Flg", "Lk", "Inf");
    printf("  %-5s %-18s %-12s %-18s %-10s %-10s %-5s %-5s %-5s %-5s\n",
           "---", "----", "----", "----", "---", "----", "--", "---", "---", "---");

    for (int i = 0; i < ehdr->e_shnum; i++) {
        const char *name = read_string(f, shstrtab, shdr[i].sh_name);
        printf("  [%2d] %-18s %-12s %016lx %08lx %08lx %4lx ",
               i, name, sht_name(shdr[i].sh_type),
               (unsigned long)shdr[i].sh_addr,
               (unsigned long)shdr[i].sh_offset,
               (unsigned long)shdr[i].sh_size,
               (unsigned long)shdr[i].sh_entsize);
        print_flags(shdr[i].sh_flags);
        printf(" %3d %4d\n", shdr[i].sh_link, shdr[i].sh_info);
    }
    printf("\n");
}

/* Display symbol table */
static void print_symbol_table(FILE *f, const Elf64_Shdr *symtab,
                                const Elf64_Shdr *strtab, const Elf64_Shdr *shdr,
                                int shnum)
{
    int nsyms = symtab->sh_size / sizeof(Elf64_Sym);
    Elf64_Sym *syms = malloc(symtab->sh_size);
    if (!syms) return;

    fseek(f, symtab->sh_offset, SEEK_SET);
    if (fread(syms, sizeof(Elf64_Sym), nsyms, f) != nsyms) {
        free(syms);
        return;
    }

    printf("=== Symbol Table (.symtab) ===\n");
    printf("  %-6s %-18s %-18s %-8s %-8s %-8s %-8s\n",
           "Num", "Value", "Size", "Type", "Bind", "Vis", "Ndx");
    printf("  %-6s %-18s %-18s %-8s %-8s %-8s %-8s\n",
           "---", "-----", "----", "----", "----", "---", "---");

    for (int i = 0; i < nsyms; i++) {
        const char *name = read_string(f, strtab, syms[i].st_name);
        uint8_t bind = ELF64_ST_BIND(syms[i].st_info);
        uint8_t type = ELF64_ST_TYPE(syms[i].st_info);

        const char *ndx_str = shndx_name(syms[i].st_shndx);
        char ndx_buf[16];
        if (!ndx_str) {
            snprintf(ndx_buf, sizeof(ndx_buf), "%d", syms[i].st_shndx);
            ndx_str = ndx_buf;
        }

        printf("  %6d %-18s %016lx %-8s %-8s %-8s %-8s\n",
               i, name,
               (unsigned long)syms[i].st_value,
               stt_name(type), stb_name(bind),
               "DEFAULT", ndx_str);
    }
    printf("\n");
    free(syms);
}

/* Display relocation entries */
static void print_relocations(FILE *f, const Elf64_Shdr *rela,
                               const Elf64_Shdr *symtab,
                               const Elf64_Shdr *strtab)
{
    int nrela = rela->sh_size / sizeof(Elf64_Rela);
    Elf64_Rela *relas = malloc(rela->sh_size);
    if (!relas) return;

    fseek(f, rela->sh_offset, SEEK_SET);
    if (fread(relas, sizeof(Elf64_Rela), nrela, f) != nrela) {
        free(relas);
        return;
    }

    /* Read the target section name from section headers */
    printf("=== Relocation Section ===\n");
    printf("  %-18s %-18s %-12s %-8s %-18s\n",
           "Offset", "Addend", "Type", "SymIdx", "Symbol");
    printf("  %-18s %-18s %-12s %-8s %-18s\n",
           "------", "------", "----", "------", "------");

    Elf64_Sym *syms = NULL;
    int nsyms = 0;
    if (symtab) {
        nsyms = symtab->sh_size / sizeof(Elf64_Sym);
        syms = malloc(symtab->sh_size);
        if (syms) {
            fseek(f, symtab->sh_offset, SEEK_SET);
            fread(syms, sizeof(Elf64_Sym), nsyms, f);
        }
    }

    for (int i = 0; i < nrela; i++) {
        uint32_t sym_idx = ELF64_R_SYM(relas[i].r_info);
        uint32_t type = ELF64_R_TYPE(relas[i].r_info);
        const char *sym_name = "(no symtab)";
        if (syms && sym_idx < nsyms) {
            sym_name = read_string(f, strtab, syms[sym_idx].st_name);
        }
        printf("  %016lx %+ld %-12s %6d   %s\n",
               (unsigned long)relas[i].r_offset,
               (long)relas[i].r_addend,
               reloc_type_name_x86_64(type),
               sym_idx, sym_name);
    }
    printf("\n");
    free(relas);
    free(syms);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <elf_file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    Elf64_Ehdr ehdr;
    if (read_elf_header(f, &ehdr) < 0) {
        fclose(f);
        return 1;
    }
    print_elf_header(&ehdr);

    Elf64_Shdr *shdr = read_section_headers(f, &ehdr);
    if (!shdr) {
        fprintf(stderr, "Error: cannot read section headers\n");
        fclose(f);
        return 1;
    }
    print_section_headers(f, &ehdr, shdr);

    /* Find .symtab and .strtab */
    const Elf64_Shdr *shstrtab = &shdr[ehdr.e_shstrndx];
    const Elf64_Shdr *symtab = NULL;
    const Elf64_Shdr *strtab = NULL;

    for (int i = 0; i < ehdr.e_shnum; i++) {
        const char *name = read_string(f, shstrtab, shdr[i].sh_name);
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = &shdr[i];
            strtab = &shdr[shdr[i].sh_link];
        }
    }

    if (symtab && strtab)
        print_symbol_table(f, symtab, strtab, shdr, ehdr.e_shnum);

    /* Print relocation sections */
    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (shdr[i].sh_type == SHT_RELA) {
            /* Find the associated symtab (through sh_link) */
            const Elf64_Shdr *rel_symtab = &shdr[shdr[i].sh_link];
            const Elf64_Shdr *rel_strtab = &shdr[rel_symtab->sh_link];
            const char *name = read_string(f, shstrtab, shdr[i].sh_name);
            printf("  Section: %s\n", name);
            print_relocations(f, &shdr[i], rel_symtab, rel_strtab);
        }
    }

    free(shdr);
    fclose(f);
    return 0;
}
