#include <cstdio>
#include <cstdint>
#include <cmath>

int main() {
    // Simulate the exact conversion from CobInstance::AimWeapon:
    //   callinArgs[1] = short(heading * RAD2TAANG);
    // where heading is in [0, 2*pi) and RAD2TAANG = 65536 / (2*pi)
    
    float PI = 3.14159265f;
    float TWOPI = 2.0f * PI;
    float RAD2TAANG = 65536.0f / TWOPI;
    float TAANG2RAD = TWOPI / 65536.0f;
    
    // Test values spanning the range
    float test_headings[] = {
        0.0f,           // 0 rad
        1.0f,           // ~57 deg (safe, in short range)
        PI * 0.5f,      // pi/2 (~90 deg, safe)
        PI * 0.99f,     // just under pi (safe)
        PI,             // pi exactly (boundary: 32768)
        PI * 1.01f,     // just over pi (OVERFLOW!)
        PI * 1.5f,      // 3pi/2 (~270 deg, overflow)
        5.87083f,       // the actual weapon 1 heading from logs
        TWOPI - 0.01f,  // near 2pi (overflow)
    };
    
    for (float h : test_headings) {
        float product = h * RAD2TAANG;
        
        // The exact code from CobInstance.cpp line 436:
        short s = short(product);
        
        // What the int array element actually stores:
        int callinArg = s;  // widened back to int
        
        // Proper wrapping for comparison:
        int32_t raw_int = static_cast<int32_t>(product);
        int16_t proper = static_cast<int16_t>(static_cast<uint16_t>(raw_int));
        
        printf("heading=%10.6f  product=%10.1f  short()=%6d  callinArg=%6d  proper=%6d  %s\n",
               h, product, (int)s, callinArg, (int)proper,
               (product > 32767.0f || product < -32768.0f) ? "** UB! **" : "ok");
    }
    
    printf("\n--- Critical test: the exact value from our logs ---\n");
    // Weapon 1 aimHeading that produces rawDest=0x1.77bbb8p+2 on ARM64
    // 0x1.77bbb8p+2 / TAANG2RAD should give us the COB integer
    float rawDest_arm = 0x1.77bbb8p+2;
    float rawDest_x86 = -0x1.a63fecp-2;
    
    int cob_int_arm = (int)(rawDest_arm / TAANG2RAD);
    int cob_int_x86 = (int)(rawDest_x86 / TAANG2RAD);
    printf("ARM64 rawDest=%a -> COB int = %d\n", rawDest_arm, cob_int_arm);
    printf("x86_64 rawDest=%a -> COB int = %d\n", rawDest_x86, cob_int_x86);
    printf("Difference: %d (expected 65536 = one full rotation)\n", cob_int_arm - cob_int_x86);
    
    printf("\n--- Demonstrating the downstream divergence ---\n");
    // If COB script divides heading by 2:
    int div_arm = cob_int_arm / 2;
    int div_x86 = cob_int_x86 / 2;
    float angle_arm = div_arm * TAANG2RAD;
    float angle_x86 = div_x86 * TAANG2RAD;
    printf("heading/2: ARM64=%d (%.4f rad), x86_64=%d (%.4f rad)\n",
           div_arm, angle_arm, div_x86, angle_x86);
    printf("Angle difference: %.4f rad (%.1f degrees) -- MASSIVE!\n",
           fabsf(angle_arm - angle_x86), fabsf(angle_arm - angle_x86) * 180.0f / PI);
    
    return 0;
}
