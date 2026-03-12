// test_int_nan.cpp
// Demonstrates platform-dependent behavior of int(float) for exceptional inputs.
// This is undefined behavior in C++ — ARM64 and x86_64 produce different results.
//
// Compile: g++ -O2 -std=c++17 -o test_int_nan test_int_nan.cpp -lm
//
// Expected results:
//   x86_64:  int(NaN) = -2147483648 (INT_MIN) for all exceptional inputs
//   ARM64:   int(NaN) = 0, int(+Inf) = INT_MAX, int(-Inf) = INT_MIN

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <climits>

// Prevent compiler from constant-folding the cast
__attribute__((noinline))
int float_to_int(float v) {
    return static_cast<int>(v);
}

// Reproduce the exact COB GET POW operation that triggers the Hellas Basin desync
__attribute__((noinline))
int cob_get_pow(int p1, int p2) {
    const int COBSCALE = 65536;
    float base = (p1 * 1.0f) / COBSCALE;
    float exp  = (p2 * 1.0f) / COBSCALE;
    float result = powf(base, exp);
    return static_cast<int>(result * COBSCALE);
}

int main() {
    float nan_val  = NAN;
    float pinf     = INFINITY;
    float ninf     = -INFINITY;
    float big      = 3e9f;       // > INT_MAX
    float neg_big  = -3e9f;      // < INT_MIN
    float normal   = 42.5f;

    printf("=== int(float) platform behavior test ===\n\n");

    printf("%-25s  %15s  %15s\n", "Input", "float value", "int(float)");
    printf("%-25s  %15s  %15s\n", "-------------------------", "---------------", "---------------");
    printf("%-25s  %15.6g  %15d\n", "NaN",       nan_val,  float_to_int(nan_val));
    printf("%-25s  %15.6g  %15d\n", "+Infinity", pinf,     float_to_int(pinf));
    printf("%-25s  %15.6g  %15d\n", "-Infinity", ninf,     float_to_int(ninf));
    printf("%-25s  %15.6g  %15d\n", "3e9 (> INT_MAX)",  big,     float_to_int(big));
    printf("%-25s  %15.6g  %15d\n", "-3e9 (< INT_MIN)", neg_big, float_to_int(neg_big));
    printf("%-25s  %15.6g  %15d\n", "42.5 (normal)",    normal,  float_to_int(normal));

    printf("\n=== COB GET POW reproduction (Hellas Basin desync trigger) ===\n\n");

    // The actual values from the replay trace at frame 46828, uid=16872:
    // dy_x = 446740, so POW(dy_x, 131072) = pow(6.82, 2) * 65536 = 3045297
    // Then 65536 - 3045297 = -2979761 (negative!)
    // Then POW(-2979761, 32768) = pow(-45.47, 0.5) * 65536 = NaN * 65536

    int p1 = -2979761;  // negative sqrt argument
    int p2 = 32768;     // 0.5 in COBSCALE (square root)
    int result = cob_get_pow(p1, p2);

    printf("COB POW(%d, %d):\n", p1, p2);
    printf("  base = %d / 65536 = %.6f\n", p1, p1 / 65536.0f);
    printf("  exp  = %d / 65536 = %.6f\n", p2, p2 / 65536.0f);
    printf("  pow(%.6f, 0.5) = sqrt(negative) = NaN\n", p1 / 65536.0f);
    printf("  int(NaN * 65536) = %d\n", result);
    printf("\n");

    if (result == 0)
        printf("  --> ARM64 behavior: int(NaN) = 0\n");
    else if (result == INT_MIN)
        printf("  --> x86_64 behavior: int(NaN) = INT_MIN (%d)\n", INT_MIN);
    else
        printf("  --> UNEXPECTED: int(NaN) = %d\n", result);

    printf("\n=== Downstream effect on ATAN ===\n\n");

    int dy_x = 446740;
    printf("ATAN(%d, %d):\n", dy_x, result);
    float atan_result = atan2f((float)dy_x, (float)result);
    int taang = (int)(atan_result * (65536.0f / (2.0f * M_PI)) * 4.0f);
    // Simplified — real engine uses TAANG lookup but direction is the point
    printf("  atan2(%d, %d) = %.6f radians\n", dy_x, result, atan_result);
    if (result == 0)
        printf("  atan2(positive, 0) = PI/2 = 90 degrees (ARM64 path)\n");
    else if (result == INT_MIN)
        printf("  atan2(positive, INT_MIN) ≈ PI = 180 degrees (x86 path)\n");

    printf("\n  ==> Different turret rotation ==> beam hits different target ==> DESYNC\n");

    return 0;
}
