#!/bin/bash
#
# run_full_stack_test.sh
#
# Build and run the FULL STACK streflop cross-architecture comparison test.
#
# Unlike run_crossarch_test.sh (which compiles isolated libm with FPU stubs),
# this builds the real streflop library with real FPU control code:
#   - ARM64:  STREFLOP_ARM_NATIVE (FPCR mrs/msr)
#   - x86_64: STREFLOP_SSE        (MXCSR)
#
# Then runs both and diffs output to prove the full wrapper chain
# (streflop::sqrt -> SMath.h -> streflop_libm::__ieee754_sqrtf) produces
# bit-identical results across architectures.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STREFLOP="$REPO_ROOT/rts/lib/streflop"
FLT32_DIR="$STREFLOP/libm/flt-32"
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$TEST_DIR/build_full_stack"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR/arm64" "$BUILD_DIR/x86_64"

# All flt-32 source files (same list as streflop CMakeLists.txt)
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

# Streflop top-level sources (non-libm)
STREFLOP_SOURCES=(SMath.cpp Random.cpp streflopC.cpp)

echo "================================================================"
echo " Building streflop FULL STACK cross-architecture test"
echo "================================================================"
echo ""

#-----------------------------------------------------------------------
# ARM64 native build (STREFLOP_ARM_NATIVE)
#-----------------------------------------------------------------------
echo "--- Building for ARM64 (native, STREFLOP_ARM_NATIVE) ---"

LIBM_INCLUDES="-I$STREFLOP/libm/headers -I$STREFLOP"
ARM_LIBM_FLAGS="-std=c++17 -O2 -w -DSTREFLOP_ARM_NATIVE -DLIBM_COMPILING_FLT32 -ffp-contract=off $LIBM_INCLUDES"
ARM_STREFLOP_FLAGS="-std=c++17 -O2 -w -DSTREFLOP_ARM_NATIVE -ffp-contract=off -I$STREFLOP"

# Build libm flt-32 objects
OBJ_COUNT=0
for src in "${FLT32_SOURCES[@]}"; do
    obj="$BUILD_DIR/arm64/${src%.cpp}.o"
    c++ -arch arm64 $ARM_LIBM_FLAGS -c "$FLT32_DIR/$src" -o "$obj" &
    OBJ_COUNT=$((OBJ_COUNT + 1))
    if (( OBJ_COUNT % 16 == 0 )); then wait; fi
done
wait

# Build streflop top-level objects
for src in "${STREFLOP_SOURCES[@]}"; do
    obj="$BUILD_DIR/arm64/${src%.cpp}.o"
    c++ -arch arm64 $ARM_STREFLOP_FLAGS -c "$STREFLOP/$src" -o "$obj" &
done
wait

# Provide streflop_libm::SimplePositiveInfinity etc. (needed by j0f/j1f/jnf
# which reference these via the bridge header; normally these are dead-code-
# eliminated from the static library since the engine never calls j0/j1/jn)
cat > "$BUILD_DIR/arm64/libm_constants.cpp" << 'CONST_EOF'
namespace streflop_libm {
    extern const float SimplePositiveInfinity = __builtin_inff();
    extern const float SimpleNegativeInfinity = -__builtin_inff();
    extern const float SimpleNaN = __builtin_nanf("");
}
CONST_EOF
c++ -arch arm64 -std=c++17 -O2 -w -DSTREFLOP_ARM_NATIVE -ffp-contract=off \
    -c "$BUILD_DIR/arm64/libm_constants.cpp" -o "$BUILD_DIR/arm64/libm_constants.o"

# Build test
c++ -arch arm64 -std=c++17 -O2 -w -DSTREFLOP_ARM_NATIVE -ffp-contract=off \
    -I"$REPO_ROOT/rts" \
    -c "$TEST_DIR/test_streflop_full_stack.cpp" \
    -o "$BUILD_DIR/arm64/test_main.o"

# Link ARM64
c++ -arch arm64 "$BUILD_DIR/arm64/"*.o -o "$BUILD_DIR/arm64/test_full_stack"
echo "  ARM64 binary: $BUILD_DIR/arm64/test_full_stack"

#-----------------------------------------------------------------------
# x86_64 Rosetta build (STREFLOP_SSE)
#-----------------------------------------------------------------------
echo "--- Building for x86_64 (Rosetta 2, STREFLOP_SSE) ---"

X86_LIBM_FLAGS="-std=c++17 -O2 -w -DSTREFLOP_SSE -DLIBM_COMPILING_FLT32 -msse -mfpmath=sse $LIBM_INCLUDES"
X86_STREFLOP_FLAGS="-std=c++17 -O2 -w -DSTREFLOP_SSE -msse -mfpmath=sse -I$STREFLOP"

# Build libm flt-32 objects
OBJ_COUNT=0
for src in "${FLT32_SOURCES[@]}"; do
    obj="$BUILD_DIR/x86_64/${src%.cpp}.o"
    arch -x86_64 c++ -arch x86_64 $X86_LIBM_FLAGS -c "$FLT32_DIR/$src" -o "$obj" &
    OBJ_COUNT=$((OBJ_COUNT + 1))
    if (( OBJ_COUNT % 16 == 0 )); then wait; fi
done
wait

# Build streflop top-level objects
for src in "${STREFLOP_SOURCES[@]}"; do
    obj="$BUILD_DIR/x86_64/${src%.cpp}.o"
    arch -x86_64 c++ -arch x86_64 $X86_STREFLOP_FLAGS -c "$STREFLOP/$src" -o "$obj" &
done
wait

# Same libm_constants for x86_64
cat > "$BUILD_DIR/x86_64/libm_constants.cpp" << 'CONST_EOF'
namespace streflop_libm {
    extern const float SimplePositiveInfinity = __builtin_inff();
    extern const float SimpleNegativeInfinity = -__builtin_inff();
    extern const float SimpleNaN = __builtin_nanf("");
}
CONST_EOF
arch -x86_64 c++ -arch x86_64 -std=c++17 -O2 -w -DSTREFLOP_SSE -msse -mfpmath=sse \
    -c "$BUILD_DIR/x86_64/libm_constants.cpp" -o "$BUILD_DIR/x86_64/libm_constants.o"

# Build test
arch -x86_64 c++ -arch x86_64 -std=c++17 -O2 -w -DSTREFLOP_SSE -msse -mfpmath=sse \
    -I"$REPO_ROOT/rts" \
    -c "$TEST_DIR/test_streflop_full_stack.cpp" \
    -o "$BUILD_DIR/x86_64/test_main.o"

# Link x86_64
arch -x86_64 c++ -arch x86_64 "$BUILD_DIR/x86_64/"*.o \
    -o "$BUILD_DIR/x86_64/test_full_stack"
echo "  x86_64 binary: $BUILD_DIR/x86_64/test_full_stack"

#-----------------------------------------------------------------------
# Run both and compare
#-----------------------------------------------------------------------
echo ""
echo "================================================================"
echo " Running tests"
echo "================================================================"
echo ""

echo "--- Running ARM64 binary (STREFLOP_ARM_NATIVE) ---"
"$BUILD_DIR/arm64/test_full_stack" > "$BUILD_DIR/output_arm64.txt"
echo "  Output saved to: $BUILD_DIR/output_arm64.txt"

echo "--- Running x86_64 binary (STREFLOP_SSE, via Rosetta 2) ---"
arch -x86_64 "$BUILD_DIR/x86_64/test_full_stack" > "$BUILD_DIR/output_x86_64.txt"
echo "  Output saved to: $BUILD_DIR/output_x86_64.txt"

echo ""
echo "================================================================"
echo " Comparing outputs (diff)"
echo "================================================================"
echo ""

# Strip Mode: and Architecture: lines since they differ by design
sed -E '/^(Mode:|Architecture:)/d' "$BUILD_DIR/output_arm64.txt"  > "$BUILD_DIR/output_arm64_cmp.txt"
sed -E '/^(Mode:|Architecture:)/d' "$BUILD_DIR/output_x86_64.txt" > "$BUILD_DIR/output_x86_64_cmp.txt"

if diff -u "$BUILD_DIR/output_arm64_cmp.txt" "$BUILD_DIR/output_x86_64_cmp.txt"; then
    echo ""
    echo "PASS: All results are bit-identical across ARM64 (STREFLOP_ARM_NATIVE)"
    echo "      and x86_64 (STREFLOP_SSE) through the FULL streflop wrapper chain."
    echo ""
    echo "This proves that streflop::sqrt(), streflop::sin(), exp2() etc."
    echo "produce identical IEEE-754 bit patterns on both architectures,"
    echo "including functions that use FPU env control (feholdexcept/fesetround/fesetenv)."
    echo ""
    TOTAL=$(grep -c '= 0x' "$BUILD_DIR/output_arm64.txt" || true)
    echo "Total values compared: $TOTAL"
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
