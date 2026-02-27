/*
 * test_streflop_crossarch.cpp
 *
 * Standalone cross-architecture comparison test for streflop's portable libm.
 * Tests every math function used in the engine's synced simulation code using
 * the flt-32 (single-precision) streflop implementations.
 *
 * The streflop libm functions are pure C integer/float bit manipulation with
 * no SSE/x87 intrinsic dependencies. This test proves they produce bit-identical
 * results on ARM64 and x86_64.
 *
 * Compile for ARM64 (native):
 *   c++ -DSTREFLOP_SSE -DLIBM_COMPILING_FLT32 -include arm_fpu_compat.h ...
 *
 * Compile for x86_64 (Rosetta):
 *   arch -x86_64 c++ -DSTREFLOP_SSE -DLIBM_COMPILING_FLT32 ...
 *
 * Output is printed in hex (%a) format for bit-exact comparison via diff.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>

/* Declare the streflop_libm functions we test.
   Under STREFLOP_SSE, Simple = float. We declare them here rather than
   pulling in the full streflop header chain, keeping this file self-contained. */
namespace streflop_libm {
    extern float __ieee754_sqrtf(float);
    extern float __sinf(float);
    extern float __cosf(float);
    extern float __tanf(float);
    extern float __ieee754_powf(float, float);
    extern float __ieee754_expf(float);
    extern float __ieee754_logf(float);
    extern float __floorf(float);
    extern float __ceilf(float);
    extern float __fabsf(float);
    extern float __ieee754_atan2f(float, float);
    extern float __ieee754_acosf(float);
    extern float __ieee754_asinf(float);
    extern float __ieee754_fmodf(float, float);
}

using namespace streflop_libm;

/* Helper: print a float result in hex for bit-exact comparison */
static void P(const char* label, float val) {
    printf("%-36s = %a\n", label, (double)val);
}

/* Helper: construct a float from raw bits */
static float from_bits(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

int main() {
    printf("=== streflop libm flt-32 cross-architecture test ===\n");
#if defined(__aarch64__) || defined(__arm64__)
    printf("Architecture: ARM64\n");
#elif defined(__x86_64__)
    printf("Architecture: x86_64\n");
#else
    printf("Architecture: unknown\n");
#endif
    printf("\n");

    /* ---- sqrt ---- */
    printf("--- sqrt ---\n");
    P("sqrt(0.0)",          __ieee754_sqrtf(0.0f));
    P("sqrt(1.0)",          __ieee754_sqrtf(1.0f));
    P("sqrt(2.0)",          __ieee754_sqrtf(2.0f));
    P("sqrt(0.5)",          __ieee754_sqrtf(0.5f));
    P("sqrt(4.0)",          __ieee754_sqrtf(4.0f));
    P("sqrt(1e-20)",        __ieee754_sqrtf(1e-20f));
    P("sqrt(1e+20)",        __ieee754_sqrtf(1e+20f));
    P("sqrt(FLT_MIN_NORM)", __ieee754_sqrtf(from_bits(0x00800000))); /* smallest normal */
    P("sqrt(FLT_MAX)",      __ieee754_sqrtf(from_bits(0x7f7fffff))); /* largest finite */
    P("sqrt(subnormal)",    __ieee754_sqrtf(from_bits(0x00000001))); /* smallest subnormal */
    printf("\n");

    /* ---- sin ---- */
    printf("--- sin ---\n");
    P("sin(0.0)",           __sinf(0.0f));
    P("sin(1.0)",           __sinf(1.0f));
    P("sin(-1.0)",          __sinf(-1.0f));
    P("sin(0.5)",           __sinf(0.5f));
    P("sin(pi/4)",          __sinf(0.7853981633974483f));
    P("sin(pi/2)",          __sinf(1.5707963267948966f));
    P("sin(pi)",            __sinf(3.1415926535897932f));
    P("sin(3*pi)",          __sinf(9.4247779607693797f));
    P("sin(1e6)",           __sinf(1e6f));
    P("sin(tiny)",          __sinf(1e-30f));
    printf("\n");

    /* ---- cos ---- */
    printf("--- cos ---\n");
    P("cos(0.0)",           __cosf(0.0f));
    P("cos(1.0)",           __cosf(1.0f));
    P("cos(-1.0)",          __cosf(-1.0f));
    P("cos(0.5)",           __cosf(0.5f));
    P("cos(pi/4)",          __cosf(0.7853981633974483f));
    P("cos(pi/2)",          __cosf(1.5707963267948966f));
    P("cos(pi)",            __cosf(3.1415926535897932f));
    P("cos(1e6)",           __cosf(1e6f));
    printf("\n");

    /* ---- tan ---- */
    printf("--- tan ---\n");
    P("tan(0.0)",           __tanf(0.0f));
    P("tan(1.0)",           __tanf(1.0f));
    P("tan(-1.0)",          __tanf(-1.0f));
    P("tan(0.5)",           __tanf(0.5f));
    P("tan(pi/4)",          __tanf(0.7853981633974483f));
    P("tan(pi/3)",          __tanf(1.0471975511965976f));
    P("tan(1e6)",           __tanf(1e6f));
    printf("\n");

    /* ---- pow ---- */
    printf("--- pow ---\n");
    P("pow(2.0, 10.0)",     __ieee754_powf(2.0f, 10.0f));
    P("pow(2.0, -1.0)",     __ieee754_powf(2.0f, -1.0f));
    P("pow(2.0, 0.5)",      __ieee754_powf(2.0f, 0.5f));
    P("pow(10.0, 3.0)",     __ieee754_powf(10.0f, 3.0f));
    P("pow(0.5, 2.0)",      __ieee754_powf(0.5f, 2.0f));
    P("pow(1.0, 999.0)",    __ieee754_powf(1.0f, 999.0f));
    P("pow(0.0, 1.0)",      __ieee754_powf(0.0f, 1.0f));
    P("pow(0.0, 0.0)",      __ieee754_powf(0.0f, 0.0f));
    P("pow(-1.0, 2.0)",     __ieee754_powf(-1.0f, 2.0f));
    P("pow(-1.0, 3.0)",     __ieee754_powf(-1.0f, 3.0f));
    P("pow(1e10, 0.1)",     __ieee754_powf(1e10f, 0.1f));
    printf("\n");

    /* ---- exp ---- */
    printf("--- exp ---\n");
    P("exp(0.0)",           __ieee754_expf(0.0f));
    P("exp(1.0)",           __ieee754_expf(1.0f));
    P("exp(-1.0)",          __ieee754_expf(-1.0f));
    P("exp(2.0)",           __ieee754_expf(2.0f));
    P("exp(10.0)",          __ieee754_expf(10.0f));
    P("exp(-10.0)",         __ieee754_expf(-10.0f));
    P("exp(0.001)",         __ieee754_expf(0.001f));
    P("exp(88.0)",          __ieee754_expf(88.0f));  /* near overflow */
    P("exp(-87.0)",         __ieee754_expf(-87.0f)); /* near underflow */
    printf("\n");

    /* ---- log ---- */
    printf("--- log ---\n");
    P("log(1.0)",           __ieee754_logf(1.0f));
    P("log(2.0)",           __ieee754_logf(2.0f));
    P("log(0.5)",           __ieee754_logf(0.5f));
    P("log(10.0)",          __ieee754_logf(10.0f));
    P("log(1e-10)",         __ieee754_logf(1e-10f));
    P("log(1e+10)",         __ieee754_logf(1e+10f));
    P("log(1.0001)",        __ieee754_logf(1.0001f));
    P("log(FLT_MIN_NORM)",  __ieee754_logf(from_bits(0x00800000)));
    P("log(FLT_MAX)",       __ieee754_logf(from_bits(0x7f7fffff)));
    printf("\n");

    /* ---- floor ---- */
    printf("--- floor ---\n");
    P("floor(0.0)",         __floorf(0.0f));
    P("floor(1.5)",         __floorf(1.5f));
    P("floor(-1.5)",        __floorf(-1.5f));
    P("floor(0.999)",       __floorf(0.999f));
    P("floor(-0.001)",      __floorf(-0.001f));
    P("floor(1e10)",        __floorf(1e10f));
    P("floor(-0.0)",        __floorf(-0.0f));
    printf("\n");

    /* ---- ceil ---- */
    printf("--- ceil ---\n");
    P("ceil(0.0)",          __ceilf(0.0f));
    P("ceil(1.5)",          __ceilf(1.5f));
    P("ceil(-1.5)",         __ceilf(-1.5f));
    P("ceil(0.001)",        __ceilf(0.001f));
    P("ceil(-0.999)",       __ceilf(-0.999f));
    P("ceil(1e10)",         __ceilf(1e10f));
    P("ceil(-0.0)",         __ceilf(-0.0f));
    printf("\n");

    /* ---- fabs ---- */
    printf("--- fabs ---\n");
    P("fabs(0.0)",          __fabsf(0.0f));
    P("fabs(-0.0)",         __fabsf(-0.0f));
    P("fabs(1.0)",          __fabsf(1.0f));
    P("fabs(-1.0)",         __fabsf(-1.0f));
    P("fabs(-1e20)",        __fabsf(-1e20f));
    P("fabs(FLT_MIN_NORM)", __fabsf(from_bits(0x00800000)));
    P("fabs(-subnormal)",   __fabsf(-from_bits(0x00000001)));
    printf("\n");

    /* ---- atan2 ---- */
    printf("--- atan2 ---\n");
    P("atan2(0.0, 1.0)",    __ieee754_atan2f(0.0f, 1.0f));
    P("atan2(1.0, 0.0)",    __ieee754_atan2f(1.0f, 0.0f));
    P("atan2(1.0, 1.0)",    __ieee754_atan2f(1.0f, 1.0f));
    P("atan2(-1.0, -1.0)",  __ieee754_atan2f(-1.0f, -1.0f));
    P("atan2(1.0, -1.0)",   __ieee754_atan2f(1.0f, -1.0f));
    P("atan2(-1.0, 1.0)",   __ieee754_atan2f(-1.0f, 1.0f));
    P("atan2(0.0, -1.0)",   __ieee754_atan2f(0.0f, -1.0f));
    P("atan2(1e10, 1e-10)",  __ieee754_atan2f(1e10f, 1e-10f));
    P("atan2(1e-10, 1e10)",  __ieee754_atan2f(1e-10f, 1e10f));
    printf("\n");

    /* ---- acos ---- */
    printf("--- acos ---\n");
    P("acos(1.0)",          __ieee754_acosf(1.0f));
    P("acos(0.0)",          __ieee754_acosf(0.0f));
    P("acos(-1.0)",         __ieee754_acosf(-1.0f));
    P("acos(0.5)",          __ieee754_acosf(0.5f));
    P("acos(-0.5)",         __ieee754_acosf(-0.5f));
    P("acos(0.999)",        __ieee754_acosf(0.999f));
    P("acos(-0.999)",       __ieee754_acosf(-0.999f));
    printf("\n");

    /* ---- asin ---- */
    printf("--- asin ---\n");
    P("asin(0.0)",          __ieee754_asinf(0.0f));
    P("asin(1.0)",          __ieee754_asinf(1.0f));
    P("asin(-1.0)",         __ieee754_asinf(-1.0f));
    P("asin(0.5)",          __ieee754_asinf(0.5f));
    P("asin(-0.5)",         __ieee754_asinf(-0.5f));
    P("asin(0.001)",        __ieee754_asinf(0.001f));
    P("asin(0.999)",        __ieee754_asinf(0.999f));
    printf("\n");

    /* ---- fmod ---- */
    printf("--- fmod ---\n");
    P("fmod(5.0, 3.0)",     __ieee754_fmodf(5.0f, 3.0f));
    P("fmod(-5.0, 3.0)",    __ieee754_fmodf(-5.0f, 3.0f));
    P("fmod(5.0, -3.0)",    __ieee754_fmodf(5.0f, -3.0f));
    P("fmod(10.5, 0.7)",    __ieee754_fmodf(10.5f, 0.7f));
    P("fmod(1e10, 3.0)",    __ieee754_fmodf(1e10f, 3.0f));
    P("fmod(1.0, 1e-20)",   __ieee754_fmodf(1.0f, 1e-20f));
    P("fmod(0.0, 1.0)",     __ieee754_fmodf(0.0f, 1.0f));
    printf("\n");

    printf("=== Test complete ===\n");
    return 0;
}
