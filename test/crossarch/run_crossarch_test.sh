#!/bin/bash
#
# run_crossarch_test.sh
#
# Build and run the streflop libm cross-architecture comparison test.
# Compiles the flt-32 portable libm once for ARM64 (native) and once
# for x86_64 (via Rosetta 2), then diffs the output to prove bit-identity.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STREFLOP="$REPO_ROOT/rts/lib/streflop"
FLT32_DIR="$STREFLOP/libm/flt-32"
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$TEST_DIR/build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR/arm64" "$BUILD_DIR/x86_64"

# Common compiler flags
COMMON_CXX_FLAGS="-std=c++11 -O2 -w -DSTREFLOP_SSE -DLIBM_COMPILING_FLT32"
COMMON_INCLUDES="-I$STREFLOP/libm/headers -I$STREFLOP"

# All flt-32 source files from CMakeLists.txt
FLT32_SOURCES=(
    e_acosf.cpp e_acoshf.cpp e_asinf.cpp e_atan2f.cpp e_atanhf.cpp
    e_coshf.cpp e_exp2f.cpp e_expf.cpp e_fmodf.cpp e_gammaf_r.cpp
    e_hypotf.cpp e_j0f.cpp e_j1f.cpp e_jnf.cpp e_lgammaf_r.cpp
    e_log10f.cpp e_log2f.cpp e_logf.cpp e_powf.cpp e_rem_pio2f.cpp
    e_remainderf.cpp e_sinhf.cpp e_sqrtf.cpp
    k_cosf.cpp k_rem_pio2f.cpp k_sinf.cpp k_tanf.cpp
    s_asinhf.cpp s_atanf.cpp s_cbrtf.cpp s_ceilf.cpp s_copysignf.cpp
    s_cosf.cpp s_erff.cpp s_expm1f.cpp s_fabsf.cpp s_finitef.cpp
    s_floorf.cpp s_fpclassifyf.cpp s_frexpf.cpp s_ilogbf.cpp
    s_isinff.cpp s_isnanf.cpp s_ldexpf.cpp s_llrintf.cpp s_llroundf.cpp
    s_log1pf.cpp s_logbf.cpp s_lrintf.cpp s_lroundf.cpp s_modff.cpp
    s_nearbyintf.cpp s_nextafterf.cpp s_remquof.cpp s_rintf.cpp
    s_roundf.cpp s_scalblnf.cpp s_scalbnf.cpp s_signbitf.cpp
    s_sincosf.cpp s_sinf.cpp s_tanf.cpp s_tanhf.cpp s_truncf.cpp
    w_expf.cpp
)

echo "================================================================"
echo " Building streflop libm flt-32 cross-architecture test"
echo "================================================================"
echo ""

#-----------------------------------------------------------------------
# ARM64 native build
#-----------------------------------------------------------------------
echo "--- Building for ARM64 (native) ---"
ARM_FLAGS="$COMMON_CXX_FLAGS $COMMON_INCLUDES -include $TEST_DIR/arm_fpu_compat.h"
OBJ_COUNT=0

for src in "${FLT32_SOURCES[@]}"; do
    obj="$BUILD_DIR/arm64/${src%.cpp}.o"
    c++ -arch arm64 $ARM_FLAGS -c "$FLT32_DIR/$src" -o "$obj" &
    OBJ_COUNT=$((OBJ_COUNT + 1))
    # Limit parallelism to avoid overwhelming the system
    if (( OBJ_COUNT % 16 == 0 )); then
        wait
    fi
done
wait

# Compile the test main (no LIBM_COMPILING_FLT32, no streflop headers needed)
c++ -arch arm64 -std=c++11 -O2 -w -c "$TEST_DIR/test_streflop_crossarch.cpp" \
    -o "$BUILD_DIR/arm64/test_main.o"

# We need to provide the constants and FE_DFL_ENV that the libm references
cat > "$BUILD_DIR/arm64/constants.cpp" << 'CONSTANTS_EOF'
namespace streflop_libm {
    extern const float SimplePositiveInfinity = __builtin_inff();
    extern const float SimpleNegativeInfinity = -__builtin_inff();
    extern const float SimpleNaN = __builtin_nanf("");
}
namespace streflop {
    struct fpenv_t { int sse_mode; short int x87_mode; };
    fpenv_t FE_DFL_ENV = {0, 0};
}
CONSTANTS_EOF
c++ -arch arm64 -std=c++11 -O2 -w -c "$BUILD_DIR/arm64/constants.cpp" \
    -o "$BUILD_DIR/arm64/constants.o"

# Link ARM64
c++ -arch arm64 "$BUILD_DIR/arm64/"*.o -o "$BUILD_DIR/arm64/test_crossarch"
echo "  ARM64 binary: $BUILD_DIR/arm64/test_crossarch"

#-----------------------------------------------------------------------
# x86_64 Rosetta build
#-----------------------------------------------------------------------
echo "--- Building for x86_64 (Rosetta 2) ---"
X86_FLAGS="$COMMON_CXX_FLAGS $COMMON_INCLUDES"
OBJ_COUNT=0

for src in "${FLT32_SOURCES[@]}"; do
    obj="$BUILD_DIR/x86_64/${src%.cpp}.o"
    arch -x86_64 c++ -arch x86_64 $X86_FLAGS -c "$FLT32_DIR/$src" -o "$obj" &
    OBJ_COUNT=$((OBJ_COUNT + 1))
    if (( OBJ_COUNT % 16 == 0 )); then
        wait
    fi
done
wait

# Compile the test main for x86_64
arch -x86_64 c++ -arch x86_64 -std=c++11 -O2 -w -c \
    "$TEST_DIR/test_streflop_crossarch.cpp" \
    -o "$BUILD_DIR/x86_64/test_main.o"

# Constants for x86_64
cat > "$BUILD_DIR/x86_64/constants.cpp" << 'CONSTANTS_EOF'
namespace streflop_libm {
    extern const float SimplePositiveInfinity = __builtin_inff();
    extern const float SimpleNegativeInfinity = -__builtin_inff();
    extern const float SimpleNaN = __builtin_nanf("");
}
namespace streflop {
    struct fpenv_t { int sse_mode; short int x87_mode; };
    fpenv_t FE_DFL_ENV = {0, 0};
}
CONSTANTS_EOF
arch -x86_64 c++ -arch x86_64 -std=c++11 -O2 -w -c \
    "$BUILD_DIR/x86_64/constants.cpp" \
    -o "$BUILD_DIR/x86_64/constants.o"

# Link x86_64
arch -x86_64 c++ -arch x86_64 "$BUILD_DIR/x86_64/"*.o \
    -o "$BUILD_DIR/x86_64/test_crossarch"
echo "  x86_64 binary: $BUILD_DIR/x86_64/test_crossarch"

#-----------------------------------------------------------------------
# Run both and compare
#-----------------------------------------------------------------------
echo ""
echo "================================================================"
echo " Running tests"
echo "================================================================"
echo ""

echo "--- Running ARM64 binary ---"
"$BUILD_DIR/arm64/test_crossarch" > "$BUILD_DIR/output_arm64.txt"
echo "  Output saved to: $BUILD_DIR/output_arm64.txt"

echo "--- Running x86_64 binary (via Rosetta 2) ---"
arch -x86_64 "$BUILD_DIR/x86_64/test_crossarch" > "$BUILD_DIR/output_x86_64.txt"
echo "  Output saved to: $BUILD_DIR/output_x86_64.txt"

echo ""
echo "================================================================"
echo " Comparing outputs (diff)"
echo "================================================================"
echo ""

# Strip the Architecture: line since it differs by design
sed '/^Architecture:/d' "$BUILD_DIR/output_arm64.txt"  > "$BUILD_DIR/output_arm64_cmp.txt"
sed '/^Architecture:/d' "$BUILD_DIR/output_x86_64.txt" > "$BUILD_DIR/output_x86_64_cmp.txt"

if diff -u "$BUILD_DIR/output_arm64_cmp.txt" "$BUILD_DIR/output_x86_64_cmp.txt"; then
    echo "PASS: All results are bit-identical across ARM64 and x86_64."
    echo ""
    echo "--- Sample output (ARM64) ---"
    head -30 "$BUILD_DIR/output_arm64.txt"
    echo "..."
    exit 0
else
    echo ""
    echo "FAIL: Differences detected between ARM64 and x86_64 outputs!"
    echo ""
    echo "Full ARM64 output:  $BUILD_DIR/output_arm64.txt"
    echo "Full x86_64 output: $BUILD_DIR/output_x86_64.txt"
    exit 1
fi
