#!/bin/bash
# cross_compile.sh - Cross compilation demo with TCC
#
# This script demonstrates how to build and use TCC cross compilers
# for different target architectures.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TCC_SRC="${SCRIPT_DIR}/../.."
BUILD_DIR="${TCC_SRC}/build-cross"

echo "=== TCC Cross Compilation Demo ==="
echo ""

# ---- Helper function ----
build_cross_compiler() {
    local arch=$1
    local prefix=$2
    local configure_opts=$3

    local build_dir="${BUILD_DIR}/${arch}"
    echo "--- Building ${arch} cross compiler ---"

    mkdir -p "${build_dir}"
    cd "${TCC_SRC}"

    echo "  Configure: ./configure ${configure_opts}"
    # Uncomment the following lines to actually build:
    # ./configure ${configure_opts}
    # make -j$(nproc)
    # cp tcc "${build_dir}/"

    echo "  Build directory: ${build_dir}"
    echo ""
}

# ---- Build cross compilers (commented out for demo) ----
# Uncomment to build:
#
# ARM64 cross compiler
# build_cross_compiler "arm64" "aarch64-linux-gnu-" \
#     "--targetarm64 --enable-cross --cross-prefix=aarch64-linux-gnu-"
#
# RISC-V 64 cross compiler
# build_cross_compiler "riscv64" "riscv64-linux-gnu-" \
#     "--targetriscv64 --enable-cross --cross-prefix=riscv64-linux-gnu-"
#
# i386 (32-bit x86) cross compiler
# build_cross_compiler "i386" "" \
#     "--targeti386 --enable-cross"

# ---- Demo: Simple C program ----
cat > /tmp/tcc_demo.c << 'EOF'
#include <stdio.h>

int factorial(int n) {
    int result = 1;
    int i;
    for (i = 2; i <= n; i++)
        result *= i;
    return result;
}

int main(void) {
    int i;
    printf("Factorial table:\n");
    for (i = 0; i <= 10; i++)
        printf("  %2d! = %d\n", i, factorial(i));
    return 0;
}
EOF

echo "=== Demo source: /tmp/tcc_demo.c ==="
cat /tmp/tcc_demo.c
echo ""

# ---- Cross compile to different targets ----

# Native compilation (x86-64)
echo "=== Native compilation (x86-64) ==="
if command -v tcc &> /dev/null; then
    tcc -o /tmp/tcc_demo_native /tmp/tcc_demo.c && echo "  -> /tmp/tcc_demo_native"
    file /tmp/tcc_demo_native 2>/dev/null || true
else
    echo "  (tcc not found in PATH, skipping)"
fi
echo ""

# Generate object file
echo "=== Generate ELF object ==="
if command -v tcc &> /dev/null; then
    tcc -c -o /tmp/tcc_demo.o /tmp/tcc_demo.c && echo "  -> /tmp/tcc_demo.o"
    if command -v readelf &> /dev/null; then
        echo "  ELF header:"
        readelf -h /tmp/tcc_demo.o 2>/dev/null | grep -E "Class|Machine|Type" || true
    fi
else
    echo "  (tcc not found in PATH, skipping)"
fi
echo ""

# ---- Generate assembly for different architectures ----
echo "=== Generate assembly (x86-64) ==="
if command -v tcc &> /dev/null; then
    tcc -S -o /tmp/tcc_demo.s /tmp/tcc_demo.c && echo "  -> /tmp/tcc_demo.s"
    echo "  First 20 lines of assembly:"
    head -20 /tmp/tcc_demo.s 2>/dev/null || true
else
    echo "  (tcc not found in PATH, skipping)"
fi
echo ""

# ---- Using cross compilers (when available) ----
echo "=== Cross compilation examples ==="
echo ""
echo "  To build and use an ARM64 cross compiler:"
echo "    ./configure --targetarm64 --enable-cross"
echo "    make"
echo "    ./tcc -o demo_arm64 demo.c"
echo "    file demo_arm64    # should show: ELF 64-bit LSB, ARM aarch64"
echo ""
echo "  To build and use a RISC-V cross compiler:"
echo "    ./configure --targetriscv64 --enable-cross"
echo "    make"
echo "    ./tcc -o demo_riscv demo.c"
echo "    file demo_riscv    # should show: ELF 64-bit LSB, RISC-V"
echo ""
echo "  To cross-compile with a sysroot:"
echo "    ./tcc --sysroot=/path/to/sysroot -o demo demo.c"
echo ""

# ---- Cleanup ----
echo "=== Cleanup ==="
rm -f /tmp/tcc_demo.c /tmp/tcc_demo_native /tmp/tcc_demo.o /tmp/tcc_demo.s
echo "Done."
