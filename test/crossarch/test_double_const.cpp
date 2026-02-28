/*
 * Test: -fsingle-precision-constant effect on float arithmetic
 *
 * GCC's -fsingle-precision-constant flag treats floating-point LITERALS
 * as float instead of double. This means:
 *   x * 0.1   → x * 0.1f   (float arithmetic) with the flag
 *   x * 0.1   → x * 0.1    (double arithmetic, then back to float) without
 *
 * ARM64 Clang does NOT support this flag, so it always does double arithmetic
 * for bare double constants.
 *
 * BUILD:
 *   ARM64:  clang++ -std=c++17 -O3 -ffp-contract=off -o test_dc test_double_const.cpp
 *   x86_64: g++ -std=c++17 -O3 -mfpmath=sse -frounding-math -fsingle-precision-constant -mno-fma -o test_dc test_double_const.cpp
 *
 * If results differ, -fsingle-precision-constant is the root cause of the desync.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <initializer_list>

// Print float as hex
void phex(const char* label, float f) {
    printf("  %-40s = %a\n", label, f);
}

// Print the difference
void pdiff(const char* label, float with_d, float with_f) {
    uint32_t a, b;
    memcpy(&a, &with_d, 4);
    memcpy(&b, &with_f, 4);
    int diff = (int)a - (int)b;
    printf("  %-40s: double=%a float=%a diff=%d ULPs %s\n",
        label, with_d, with_f, diff, diff == 0 ? "MATCH" : "*** DIFFERS ***");
}

int main() {
#if defined(__aarch64__) || defined(__arm64__)
    printf("Platform: ARM64 (Clang, no -fsingle-precision-constant)\n");
#elif defined(__x86_64__)
    printf("Platform: x86_64");
#if defined(__GNUC__) && !defined(__clang__)
    printf(" (GCC %d.%d.%d, -fsingle-precision-constant)", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
    printf("\n");
#endif
    printf("\n");

    // Test values that exercise the precision difference
    float x1 = 0x1.5d7f52p-5f;   // speed.y from debug data
    float x2 = 0x1.99999ap-2f;   // accRate from debug data
    float x3 = 0x1.c71c72p-4f;   // gravity

    printf("=== Test 1: Multiplication by bare double vs explicit float ===\n");
    {
        // 0.1 as double: 0x1.999999999999ap-4
        // 0.1 as float:  0x1.99999ap-4
        // These are DIFFERENT values!
        float rd = x1 * 0.1;    // bare double constant
        float rf = x1 * 0.1f;   // explicit float constant
        pdiff("x1 * 0.1 vs 0.1f", rd, rf);

        rd = x2 * 0.1;
        rf = x2 * 0.1f;
        pdiff("x2 * 0.1 vs 0.1f", rd, rf);

        rd = x3 * 0.02;
        rf = x3 * 0.02f;
        pdiff("x3 * 0.02 vs 0.02f", rd, rf);
    }

    printf("\n=== Test 2: Division by bare double vs float ===\n");
    {
        float rd = x1 / 3.0;
        float rf = x1 / 3.0f;
        pdiff("x1 / 3.0 vs 3.0f", rd, rf);

        rd = x2 / 7.0;
        rf = x2 / 7.0f;
        pdiff("x2 / 7.0 vs 7.0f", rd, rf);
    }

    printf("\n=== Test 3: Addition/subtraction with bare double ===\n");
    {
        float rd = x1 + 0.1;
        float rf = x1 + 0.1f;
        pdiff("x1 + 0.1 vs 0.1f", rd, rf);

        rd = 1.0 - x2;
        rf = 1.0f - x2;
        pdiff("1.0 - x2 vs 1.0f - x2", rd, rf);
    }

    printf("\n=== Test 4: Compound expressions ===\n");
    {
        // This matches the engine pattern: accRate *= (0.99f + gsRNG.NextFloat() * 0.02f)
        // All constants already have f suffix, so should be fine
        float nextFloat = 0.7f;  // example RNG value
        float rd = 0.99 + nextFloat * 0.02;
        float rf = 0.99f + nextFloat * 0.02f;
        pdiff("0.99+NF*0.02 vs 0.99f+NF*0.02f", rd, rf);

        // If 0.99 is double: double(0.99) + double(float(nextFloat) * double(0.02))
        // = double(0.99) + double(float(0.7f * 0.02)) ... actually this depends on evaluation
    }

    printf("\n=== Test 5: What the compiler actually does with bare constants ===\n");
    {
        // Test: x * 0.1 with -fsingle-precision-constant
        // GCC: treats 0.1 as float(0.1) = 0x1.99999ap-4
        // Clang: treats 0.1 as double(0.1) = 0x1.999999999999ap-4
        // Then: float(x * double(0.1)) vs x * float(0.1)

        // Choose a value where the difference matters
        float x = 0x1.fffffep+0f;  // largest float < 2
        float rd = x * 0.1;    // might use double intermediary
        float rf = x * 0.1f;
        pdiff("max_float_lt_2 * 0.1", rd, rf);

        x = 0x1.p+24f;  // 16777216
        rd = x * 0.1;
        rf = x * 0.1f;
        pdiff("2^24 * 0.1", rd, rf);

        x = 1.0f / 3.0f;  // 0.333333...
        rd = x * 0.7;
        rf = x * 0.7f;
        pdiff("(1/3) * 0.7", rd, rf);
    }

    printf("\n=== Test 6: RNG NextFloat computation ===\n");
    {
        // Engine: static_cast<float>(NextInt(N)) / N
        // N = 1 << 24 = 16777216 (uint32_t)
        // This is float / uint32_t → float / float (uint32_t promoted to float)
        // -fsingle-precision-constant doesn't affect integer conversions
        uint32_t N = 1 << 24;
        for (uint32_t val : {0u, 1u, 100u, 1000000u, 8388608u, 16777215u}) {
            float result = static_cast<float>(val) / N;
            printf("  NextFloat(%u / %u) = %a\n", val, N, result);
        }
    }

    printf("\n=== Test 7: Accumulation with bare double constants ===\n");
    {
        // Simulate what happens if a physics loop uses bare doubles
        float speed = 0.0f;
        float gravity = -0x1.c71c72p-4f;
        float myGravity = 0x1.99999ap-2f;
        float invDrag = 0x1.eb7b0ap-1f;

        // With bare double 1.0 in drag expression (1.0 - drag)
        // Engine code: invDrag = 1.0f - drag
        // But what if someone wrote: invDrag = 1.0 - drag?

        float drag_f = 1.0f - invDrag;  // using float constant
        float drag_d = 1.0 - invDrag;   // using double constant
        pdiff("drag: 1.0-invDrag vs 1.0f-invDrag", drag_d, drag_f);

        // Accumulate for many frames using bare double in one path
        float speed_f = 0.0f, speed_d = 0.0f;
        for (int i = 0; i < 100; i++) {
            speed_f += gravity * myGravity;
            speed_f *= invDrag;

            speed_d += gravity * myGravity;
            speed_d = (float)(speed_d * (double)invDrag);  // what Clang might do without -fsingle-precision-constant
        }
        pdiff("100 frames accumulation", speed_d, speed_f);
    }

    printf("\n=== Test 8: The ACTUAL test - same expressions, see if platforms differ ===\n");
    {
        // These expressions use ONLY float constants (f suffix)
        // They should be identical across platforms
        float a = 0x1.99999ap-2f;
        float b = 0x1.c71c72p-4f;

        phex("a * b", a * b);
        phex("a + b", a + b);
        phex("a / b", a / b);
        phex("a * 0.99f + 0.01f", a * 0.99f + 0.01f);

        // Now with bare double constants - THIS is where platforms could differ
        phex("a * 0.99 + 0.01  (bare double)", (float)(a * 0.99 + 0.01));
        phex("a * 0.99f + 0.01f (float)", a * 0.99f + 0.01f);

        // The critical question: does -fsingle-precision-constant make
        // the bare-double version match the float version?
        float rd = (float)(a * 0.99 + 0.01);
        float rf = a * 0.99f + 0.01f;
        pdiff("a*0.99+0.01: bare vs float", rd, rf);
    }

    printf("\n=== All tests complete ===\n");
    return 0;
}
