# Inside TinyCC: A Compiler Textbook from Source to Practice

> A comprehensive compiler textbook based on the TinyCC 0.9.28 source code

[中文版](README_zh.md) | English

---

## About This Book

This book uses the complete source code of [TinyCC](https://repo.or.cz/tinycc.git) (tcc) as a foundation to systematically explain the design and implementation of modern C compilers. Unlike traditional compiler textbooks that stay at the theoretical level, every concept in this book directly corresponds to a specific implementation in the tcc source code — readers can read the theory, study the source, and run experiments simultaneously.

### Highlights

- **Theory meets practice** — Every concept has rigorous theoretical exposition alongside concrete tcc source code
- **Runnable examples** — Each chapter includes example code that can be compiled and run directly
- **Progressive learning** — From lexical analysis to code generation, from ELF linking to JIT runtime
- **For all levels** — Beginners can start from the appendix; experts can dive directly into specific chapters

### Target Audience

- Computer science students who want to understand compiler internals
- C programmers who want to deepen their understanding of the language
- Developers who want to embed tcc into their own projects
- Open-source contributors who want to participate in compiler development

### Prerequisites

- C language basics (pointers, structs, preprocessor)
- Basic computer architecture knowledge (CPU, memory, assembly)
- Linux command line basics

If any of these are lacking, please start with [Appendix A: Prerequisites for Beginners](docs/appendix/prerequisites.md).

---

## Table of Contents

### Part I: Foundations

| Chapter | Topic | Core Content |
|---------|-------|--------------|
| [Ch 1](docs/ch01/index.md) | Compiler Introduction | Compiler concepts, tcc overview, single-pass architecture |
| [Ch 2](docs/ch02/index.md) | Lexical Analysis | Token system, BufferedFile, identifier hashing |
| [Ch 3](docs/ch03/index.md) | Preprocessor | Macro expansion (3 stages), conditional compilation, #include |

### Part II: Core

| Chapter | Topic | Core Content |
|---------|-------|--------------|
| [Ch 4](docs/ch04/index.md) | Syntax Analysis & Type System | CType/Sym structures, symbol table, expression parsing |
| [Ch 5](docs/ch05/index.md) | Code Generation | Virtual stack, backend interface, register allocation |
| [Ch 6](docs/ch06/index.md) | Linker & ELF | ELF format, relocation, GOT/PLT, JIT runtime |

### Part III: Applications

| Chapter | Topic | Core Content |
|---------|-------|--------------|
| [Ch 7](docs/ch07/index.md) | Runtime & Embedded API | libtcc API, runtime library |
| [Ch 8](docs/ch08/platforms.md) | Cross-Platform & Assembler | Multi-arch backends, cross-compilation, inline assembly |
| [Ch 9](docs/ch09/index.md) | Debugging & Testing | STAB/DWARF, test suite |
| [Ch 10](docs/ch10/index.md) | Hands-on Projects | Script engine, modifying tcc, community participation |

### Appendix

| Appendix | Topic |
|----------|-------|
| [Prerequisites](docs/appendix/prerequisites.md) | C language basics, compiler concepts, learning path |

---

## Quick Start

```bash
# Clone tcc source
git clone https://repo.or.cz/tinycc.git
cd tinycc

# Build
./configure && make && make test

# Compile and run a C program
./tcc -run hello.c
```

## Build the Website

```bash
cd book

# Build static site
./build.sh

# Local preview at http://127.0.0.1:8080
./build.sh serve

# Clean build artifacts
./build.sh clean
```

## Compilation Pipeline

```
Source → tccpp.c (Lexer+Preprocessor) → tccgen.c (Parser+CodeGen) → tccelf.c (Linker) → Output
```

## Key Source Files

| File | Lines | Responsibility |
|------|-------|----------------|
| `tcc.c` | ~430 | Main entry point |
| `tcc.h` | ~2000 | Core data structures |
| `tccpp.c` | ~4000 | Lexer + preprocessor |
| `tccgen.c` | ~9000 | Parser + code generator |
| `tccelf.c` | ~3500 | ELF linker |
| `libtcc.c` | ~2200 | libtcc API implementation |

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

TinyCC itself is licensed under LGPL v2.1.
