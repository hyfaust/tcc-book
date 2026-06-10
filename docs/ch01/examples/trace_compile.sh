#!/bin/bash
# trace_compile.sh - Trace a C program through all compilation stages
#
# This script demonstrates the four main stages of C compilation:
#   1. Preprocessing (-E): expand macros and includes
#   2. Compilation to assembly (-S): generate assembly code
#   3. Compilation to object (-c): generate object file
#   4. Linking: generate executable
#
# Usage:
#   ./trace_compile.sh [input.c]
#   (defaults to ../examples/hello.c if no argument given)

set -e

# Determine the input file
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INPUT="${1:-${SCRIPT_DIR}/../examples/hello.c}"

if [ ! -f "$INPUT" ]; then
    echo "Error: input file '$INPUT' not found."
    echo "Usage: $0 [input.c]"
    exit 1
fi

BASENAME=$(basename "$INPUT" .c)
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "============================================================"
echo "  Tracing compilation of: $INPUT"
echo "  Temporary directory:    $TMPDIR"
echo "============================================================"
echo ""

# Locate tcc binary
TCC=""
if command -v tcc &>/dev/null; then
    TCC="tcc"
elif [ -x "./tcc" ]; then
    TCC="./tcc"
elif [ -x "../tcc" ]; then
    TCC="../tcc"
else
    echo "Error: tcc not found. Build it first with: ./configure && make"
    exit 1
fi
echo "Using tcc: $TCC"
echo ""

# ---- Stage 1: Preprocessing ----
echo "============================================================"
echo "  STAGE 1: Preprocessing (tcc -E)"
echo "============================================================"
echo ""
echo "The preprocessor expands #include directives, #define macros,"
echo "and processes #if/#ifdef conditionals. The output is pure C"
echo "code with all macros expanded and all includes inlined."
echo ""
echo "--- Preprocessor output (first 40 lines) ---"
$TCC -E "$INPUT" 2>/dev/null | head -40
echo ""
echo "... (full output saved to ${TMPDIR}/${BASENAME}.i)"
$TCC -E "$INPUT" -o "${TMPDIR}/${BASENAME}.i" 2>/dev/null
echo "Preprocessor output: $(wc -l < ${TMPDIR}/${BASENAME}.i) lines"
echo ""

# ---- Stage 2: Compilation to Assembly ----
echo "============================================================"
echo "  STAGE 2: Compilation to Assembly (tcc -S)"
echo "============================================================"
echo ""
echo "The compiler parses the preprocessed C code and generates"
echo "target architecture assembly language."
echo ""
echo "--- Assembly output ---"
$TCC -S "$INPUT" -o "${TMPDIR}/${BASENAME}.s" 2>/dev/null
cat "${TMPDIR}/${BASENAME}.s"
echo ""
echo "Assembly output: $(wc -l < ${TMPDIR}/${BASENAME}.s) lines"
echo ""

# ---- Stage 3: Compilation to Object File ----
echo "============================================================"
echo "  STAGE 3: Compilation to Object File (tcc -c)"
echo "============================================================"
echo ""
echo "The assembler encodes assembly instructions into machine code"
echo "bytes and produces an object file (.o) in ELF format."
echo ""
$TCC -c "$INPUT" -o "${TMPDIR}/${BASENAME}.o" 2>/dev/null
echo "--- Object file info ---"
echo "Object file size: $(stat -c %s ${TMPDIR}/${BASENAME}.o 2>/dev/null || stat -f %z ${TMPDIR}/${BASENAME}.o 2>/dev/null) bytes"

if command -v file &>/dev/null; then
    echo "File type: $(file ${TMPDIR}/${BASENAME}.o)"
fi

if command -v readelf &>/dev/null; then
    echo ""
    echo "--- ELF Sections ---"
    readelf -S "${TMPDIR}/${BASENAME}.o" 2>/dev/null || true
    echo ""
    echo "--- Symbol Table ---"
    readelf -s "${TMPDIR}/${BASENAME}.o" 2>/dev/null | head -30 || true
fi
echo ""

# ---- Stage 4: Linking ----
echo "============================================================"
echo "  STAGE 4: Linking (tcc -o)"
echo "============================================================"
echo ""
echo "The linker combines object files and libraries, resolves"
echo "symbol references, and produces the final executable."
echo ""
$TCC "$INPUT" -o "${TMPDIR}/${BASENAME}" 2>/dev/null
echo "--- Executable info ---"
echo "Executable size: $(stat -c %s ${TMPDIR}/${BASENAME} 2>/dev/null || stat -f %z ${TMPDIR}/${BASENAME} 2>/dev/null) bytes"

if command -v file &>/dev/null; then
    echo "File type: $(file ${TMPDIR}/${BASENAME})"
fi

if command -v readelf &>/dev/null; then
    echo ""
    echo "--- Program Headers (loadable segments) ---"
    readelf -l "${TMPDIR}/${BASENAME}" 2>/dev/null || true
fi
echo ""

# ---- Stage 5: Execution ----
echo "============================================================"
echo "  STAGE 5: Execution"
echo "============================================================"
echo ""
echo "--- Program output ---"
"${TMPDIR}/${BASENAME}"
echo ""
echo "Exit code: $?"
echo ""

# ---- Summary ----
echo "============================================================"
echo "  COMPILATION STAGE SUMMARY"
echo "============================================================"
echo ""
echo "  Source (.c)       : $INPUT"
echo "  Preprocessed (.i) : ${TMPDIR}/${BASENAME}.i ($(wc -l < ${TMPDIR}/${BASENAME}.i) lines)"
echo "  Assembly (.s)     : ${TMPDIR}/${BASENAME}.s ($(wc -l < ${TMPDIR}/${BASENAME}.s) lines)"
echo "  Object (.o)       : ${TMPDIR}/${BASENAME}.o ($(stat -c %s ${TMPDIR}/${BASENAME}.o 2>/dev/null || stat -f %z ${TMPDIR}/${BASENAME}.o 2>/dev/null) bytes)"
echo "  Executable        : ${TMPDIR}/${BASENAME} ($(stat -c %s ${TMPDIR}/${BASENAME} 2>/dev/null || stat -f %z ${TMPDIR}/${BASENAME} 2>/dev/null) bytes)"
echo ""
echo "Done. Temporary files in $TMPDIR will be cleaned up."
