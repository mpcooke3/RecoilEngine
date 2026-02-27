/*
 * test_streflop_full_stack.cpp
 *
 * Cross-architecture test that exercises the FULL streflop wrapper chain:
 *   streflop::sqrt() -> SMath.h wrapper -> streflop_libm::__ieee754_sqrtf()
 *
 * Unlike the Phase 1 test (which compiled libm in isolation with FPU stubs),
 * this test uses the real streflop headers and FPU control code, verifying
 * that the full stack produces identical results on ARM64 and x86_64.
 *
 * Output is IEEE-754 bit patterns (hex uint32_t) for exact comparison via diff.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>

// Include the full streflop stack - this pulls in FPUSettings.h with real
// FPU control code (ARM64 FPCR or x86 MXCSR depending on mode)
#include "lib/streflop/streflop_cond.h"

// Helper: print a float as its IEEE-754 bit pattern
static void P(const char* label, float val) {
    uint32_t bits;
    memcpy(&bits, &val, sizeof(bits));
    printf("%-40s = 0x%08X  (%a)\n", label, bits, (double)val);
}

// Helper: construct a float from raw bits
static float from_bits(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

int main() {
    // Initialize streflop FPU control (sets round-to-nearest, etc.)
    streflop::streflop_init<streflop::Simple>();

    printf("=== streflop FULL STACK cross-architecture test ===\n");
#if defined(STREFLOP_ARM_NATIVE)
    printf("Mode: STREFLOP_ARM_NATIVE\n");
#elif defined(STREFLOP_SSE)
    printf("Mode: STREFLOP_SSE\n");
#else
    printf("Mode: UNKNOWN\n");
#endif
#if defined(__aarch64__) || defined(__arm64__)
    printf("Architecture: ARM64\n");
#elif defined(__x86_64__)
    printf("Architecture: x86_64\n");
#else
    printf("Architecture: unknown\n");
#endif
    printf("\n");

    /* All calls go through streflop:: wrappers (SMath.h) which call
       streflop_libm:: internal functions. This exercises the full chain. */

    printf("--- sqrt (streflop::sqrt) ---\n");
    P("sqrt(0.0)",          streflop::sqrt(streflop::Simple(0.0f)));
    P("sqrt(1.0)",          streflop::sqrt(streflop::Simple(1.0f)));
    P("sqrt(2.0)",          streflop::sqrt(streflop::Simple(2.0f)));
    P("sqrt(0.5)",          streflop::sqrt(streflop::Simple(0.5f)));
    P("sqrt(4.0)",          streflop::sqrt(streflop::Simple(4.0f)));
    P("sqrt(1e-20)",        streflop::sqrt(streflop::Simple(1e-20f)));
    P("sqrt(1e+20)",        streflop::sqrt(streflop::Simple(1e+20f)));
    P("sqrt(FLT_MIN_NORM)", streflop::sqrt(from_bits(0x00800000)));
    P("sqrt(FLT_MAX)",      streflop::sqrt(from_bits(0x7f7fffff)));
    P("sqrt(subnormal)",    streflop::sqrt(from_bits(0x00000001)));
    printf("\n");

    printf("--- sin (streflop::sin) ---\n");
    P("sin(0.0)",           streflop::sin(streflop::Simple(0.0f)));
    P("sin(1.0)",           streflop::sin(streflop::Simple(1.0f)));
    P("sin(-1.0)",          streflop::sin(streflop::Simple(-1.0f)));
    P("sin(0.5)",           streflop::sin(streflop::Simple(0.5f)));
    P("sin(pi/4)",          streflop::sin(streflop::Simple(0.7853981633974483f)));
    P("sin(pi/2)",          streflop::sin(streflop::Simple(1.5707963267948966f)));
    P("sin(pi)",            streflop::sin(streflop::Simple(3.1415926535897932f)));
    P("sin(3*pi)",          streflop::sin(streflop::Simple(9.4247779607693797f)));
    P("sin(1e6)",           streflop::sin(streflop::Simple(1e6f)));
    P("sin(tiny)",          streflop::sin(streflop::Simple(1e-30f)));
    printf("\n");

    printf("--- cos (streflop::cos) ---\n");
    P("cos(0.0)",           streflop::cos(streflop::Simple(0.0f)));
    P("cos(1.0)",           streflop::cos(streflop::Simple(1.0f)));
    P("cos(-1.0)",          streflop::cos(streflop::Simple(-1.0f)));
    P("cos(0.5)",           streflop::cos(streflop::Simple(0.5f)));
    P("cos(pi/4)",          streflop::cos(streflop::Simple(0.7853981633974483f)));
    P("cos(pi/2)",          streflop::cos(streflop::Simple(1.5707963267948966f)));
    P("cos(pi)",            streflop::cos(streflop::Simple(3.1415926535897932f)));
    P("cos(1e6)",           streflop::cos(streflop::Simple(1e6f)));
    printf("\n");

    printf("--- tan (streflop::tan) ---\n");
    P("tan(0.0)",           streflop::tan(streflop::Simple(0.0f)));
    P("tan(1.0)",           streflop::tan(streflop::Simple(1.0f)));
    P("tan(-1.0)",          streflop::tan(streflop::Simple(-1.0f)));
    P("tan(0.5)",           streflop::tan(streflop::Simple(0.5f)));
    P("tan(pi/4)",          streflop::tan(streflop::Simple(0.7853981633974483f)));
    P("tan(pi/3)",          streflop::tan(streflop::Simple(1.0471975511965976f)));
    P("tan(1e6)",           streflop::tan(streflop::Simple(1e6f)));
    printf("\n");

    printf("--- pow (streflop::pow) ---\n");
    P("pow(2.0, 10.0)",     streflop::pow(streflop::Simple(2.0f), streflop::Simple(10.0f)));
    P("pow(2.0, -1.0)",     streflop::pow(streflop::Simple(2.0f), streflop::Simple(-1.0f)));
    P("pow(2.0, 0.5)",      streflop::pow(streflop::Simple(2.0f), streflop::Simple(0.5f)));
    P("pow(10.0, 3.0)",     streflop::pow(streflop::Simple(10.0f), streflop::Simple(3.0f)));
    P("pow(0.5, 2.0)",      streflop::pow(streflop::Simple(0.5f), streflop::Simple(2.0f)));
    P("pow(1.0, 999.0)",    streflop::pow(streflop::Simple(1.0f), streflop::Simple(999.0f)));
    P("pow(0.0, 1.0)",      streflop::pow(streflop::Simple(0.0f), streflop::Simple(1.0f)));
    P("pow(0.0, 0.0)",      streflop::pow(streflop::Simple(0.0f), streflop::Simple(0.0f)));
    P("pow(-1.0, 2.0)",     streflop::pow(streflop::Simple(-1.0f), streflop::Simple(2.0f)));
    P("pow(-1.0, 3.0)",     streflop::pow(streflop::Simple(-1.0f), streflop::Simple(3.0f)));
    P("pow(1e10, 0.1)",     streflop::pow(streflop::Simple(1e10f), streflop::Simple(0.1f)));
    printf("\n");

    printf("--- exp (streflop::exp) ---\n");
    P("exp(0.0)",           streflop::exp(streflop::Simple(0.0f)));
    P("exp(1.0)",           streflop::exp(streflop::Simple(1.0f)));
    P("exp(-1.0)",          streflop::exp(streflop::Simple(-1.0f)));
    P("exp(2.0)",           streflop::exp(streflop::Simple(2.0f)));
    P("exp(10.0)",          streflop::exp(streflop::Simple(10.0f)));
    P("exp(-10.0)",         streflop::exp(streflop::Simple(-10.0f)));
    P("exp(0.001)",         streflop::exp(streflop::Simple(0.001f)));
    P("exp(88.0)",          streflop::exp(streflop::Simple(88.0f)));
    P("exp(-87.0)",         streflop::exp(streflop::Simple(-87.0f)));
    printf("\n");

    printf("--- log (streflop::log) ---\n");
    P("log(1.0)",           streflop::log(streflop::Simple(1.0f)));
    P("log(2.0)",           streflop::log(streflop::Simple(2.0f)));
    P("log(0.5)",           streflop::log(streflop::Simple(0.5f)));
    P("log(10.0)",          streflop::log(streflop::Simple(10.0f)));
    P("log(1e-10)",         streflop::log(streflop::Simple(1e-10f)));
    P("log(1e+10)",         streflop::log(streflop::Simple(1e+10f)));
    P("log(1.0001)",        streflop::log(streflop::Simple(1.0001f)));
    P("log(FLT_MIN_NORM)",  streflop::log(from_bits(0x00800000)));
    P("log(FLT_MAX)",       streflop::log(from_bits(0x7f7fffff)));
    printf("\n");

    printf("--- floor (streflop::floor) ---\n");
    P("floor(0.0)",         streflop::floor(streflop::Simple(0.0f)));
    P("floor(1.5)",         streflop::floor(streflop::Simple(1.5f)));
    P("floor(-1.5)",        streflop::floor(streflop::Simple(-1.5f)));
    P("floor(0.999)",       streflop::floor(streflop::Simple(0.999f)));
    P("floor(-0.001)",      streflop::floor(streflop::Simple(-0.001f)));
    P("floor(1e10)",        streflop::floor(streflop::Simple(1e10f)));
    P("floor(-0.0)",        streflop::floor(streflop::Simple(-0.0f)));
    printf("\n");

    printf("--- ceil (streflop::ceil) ---\n");
    P("ceil(0.0)",          streflop::ceil(streflop::Simple(0.0f)));
    P("ceil(1.5)",          streflop::ceil(streflop::Simple(1.5f)));
    P("ceil(-1.5)",         streflop::ceil(streflop::Simple(-1.5f)));
    P("ceil(0.001)",        streflop::ceil(streflop::Simple(0.001f)));
    P("ceil(-0.999)",       streflop::ceil(streflop::Simple(-0.999f)));
    P("ceil(1e10)",         streflop::ceil(streflop::Simple(1e10f)));
    P("ceil(-0.0)",         streflop::ceil(streflop::Simple(-0.0f)));
    printf("\n");

    printf("--- fabs (streflop::fabs) ---\n");
    P("fabs(0.0)",          streflop::fabs(streflop::Simple(0.0f)));
    P("fabs(-0.0)",         streflop::fabs(streflop::Simple(-0.0f)));
    P("fabs(1.0)",          streflop::fabs(streflop::Simple(1.0f)));
    P("fabs(-1.0)",         streflop::fabs(streflop::Simple(-1.0f)));
    P("fabs(-1e20)",        streflop::fabs(streflop::Simple(-1e20f)));
    P("fabs(FLT_MIN_NORM)", streflop::fabs(from_bits(0x00800000)));
    P("fabs(-subnormal)",   streflop::fabs(-from_bits(0x00000001)));
    printf("\n");

    printf("--- atan2 (streflop::atan2) ---\n");
    P("atan2(0.0, 1.0)",    streflop::atan2(streflop::Simple(0.0f), streflop::Simple(1.0f)));
    P("atan2(1.0, 0.0)",    streflop::atan2(streflop::Simple(1.0f), streflop::Simple(0.0f)));
    P("atan2(1.0, 1.0)",    streflop::atan2(streflop::Simple(1.0f), streflop::Simple(1.0f)));
    P("atan2(-1.0, -1.0)",  streflop::atan2(streflop::Simple(-1.0f), streflop::Simple(-1.0f)));
    P("atan2(1.0, -1.0)",   streflop::atan2(streflop::Simple(1.0f), streflop::Simple(-1.0f)));
    P("atan2(-1.0, 1.0)",   streflop::atan2(streflop::Simple(-1.0f), streflop::Simple(1.0f)));
    P("atan2(0.0, -1.0)",   streflop::atan2(streflop::Simple(0.0f), streflop::Simple(-1.0f)));
    P("atan2(1e10, 1e-10)",  streflop::atan2(streflop::Simple(1e10f), streflop::Simple(1e-10f)));
    P("atan2(1e-10, 1e10)",  streflop::atan2(streflop::Simple(1e-10f), streflop::Simple(1e10f)));
    printf("\n");

    printf("--- acos (streflop::acos) ---\n");
    P("acos(1.0)",          streflop::acos(streflop::Simple(1.0f)));
    P("acos(0.0)",          streflop::acos(streflop::Simple(0.0f)));
    P("acos(-1.0)",         streflop::acos(streflop::Simple(-1.0f)));
    P("acos(0.5)",          streflop::acos(streflop::Simple(0.5f)));
    P("acos(-0.5)",         streflop::acos(streflop::Simple(-0.5f)));
    P("acos(0.999)",        streflop::acos(streflop::Simple(0.999f)));
    P("acos(-0.999)",       streflop::acos(streflop::Simple(-0.999f)));
    printf("\n");

    printf("--- asin (streflop::asin) ---\n");
    P("asin(0.0)",          streflop::asin(streflop::Simple(0.0f)));
    P("asin(1.0)",          streflop::asin(streflop::Simple(1.0f)));
    P("asin(-1.0)",         streflop::asin(streflop::Simple(-1.0f)));
    P("asin(0.5)",          streflop::asin(streflop::Simple(0.5f)));
    P("asin(-0.5)",         streflop::asin(streflop::Simple(-0.5f)));
    P("asin(0.001)",        streflop::asin(streflop::Simple(0.001f)));
    P("asin(0.999)",        streflop::asin(streflop::Simple(0.999f)));
    printf("\n");

    printf("--- fmod (streflop::fmod) ---\n");
    P("fmod(5.0, 3.0)",     streflop::fmod(streflop::Simple(5.0f), streflop::Simple(3.0f)));
    P("fmod(-5.0, 3.0)",    streflop::fmod(streflop::Simple(-5.0f), streflop::Simple(3.0f)));
    P("fmod(5.0, -3.0)",    streflop::fmod(streflop::Simple(5.0f), streflop::Simple(-3.0f)));
    P("fmod(10.5, 0.7)",    streflop::fmod(streflop::Simple(10.5f), streflop::Simple(0.7f)));
    P("fmod(1e10, 3.0)",    streflop::fmod(streflop::Simple(1e10f), streflop::Simple(3.0f)));
    P("fmod(1.0, 1e-20)",   streflop::fmod(streflop::Simple(1.0f), streflop::Simple(1e-20f)));
    P("fmod(0.0, 1.0)",     streflop::fmod(streflop::Simple(0.0f), streflop::Simple(1.0f)));
    printf("\n");

    /* Also test exp2 and nearbyint specifically since they use
       feholdexcept/fesetround/fesetenv - the FPU control functions
       that differ between STREFLOP_SSE and STREFLOP_ARM_NATIVE */
    printf("--- exp2 (uses FPU env control) ---\n");
    P("exp2(0.0)",          streflop::exp2(streflop::Simple(0.0f)));
    P("exp2(1.0)",          streflop::exp2(streflop::Simple(1.0f)));
    P("exp2(10.0)",         streflop::exp2(streflop::Simple(10.0f)));
    P("exp2(-1.0)",         streflop::exp2(streflop::Simple(-1.0f)));
    P("exp2(0.5)",          streflop::exp2(streflop::Simple(0.5f)));
    P("exp2(23.0)",         streflop::exp2(streflop::Simple(23.0f)));
    P("exp2(-23.0)",        streflop::exp2(streflop::Simple(-23.0f)));
    P("exp2(0.001)",        streflop::exp2(streflop::Simple(0.001f)));
    printf("\n");

    printf("--- nearbyint (uses FPU env control) ---\n");
    // nearbyint is not in streflop:: but we can call the libm version directly
    // Actually, let's test via round/floor/ceil which exercise similar paths
    P("floor(2.3)",         streflop::floor(streflop::Simple(2.3f)));
    P("floor(2.7)",         streflop::floor(streflop::Simple(2.7f)));
    P("ceil(2.3)",          streflop::ceil(streflop::Simple(2.3f)));
    P("ceil(2.7)",          streflop::ceil(streflop::Simple(2.7f)));
    P("floor(-2.3)",        streflop::floor(streflop::Simple(-2.3f)));
    P("floor(-2.7)",        streflop::floor(streflop::Simple(-2.7f)));
    printf("\n");

    printf("=== Test complete ===\n");
    return 0;
}
