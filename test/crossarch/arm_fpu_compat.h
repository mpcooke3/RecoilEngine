/*
 * arm_fpu_compat.h - Stub FPU settings for ARM64 compilation of streflop libm.
 *
 * FPUSettings.h contains x86 inline assembly (fstcw, stmxcsr, etc.) that
 * cannot compile on ARM. This header pre-defines the STREFLOP_FPU_H guard
 * so FPUSettings.h is entirely skipped, and provides minimal no-op stubs
 * for the FPU types/functions that a few libm files reference.
 *
 * The libm math functions themselves are pure integer/float bit manipulation
 * and do not depend on FPU control register state.
 */

#ifndef STREFLOP_FPU_H
#define STREFLOP_FPU_H

namespace streflop {

enum FPU_Exceptions {
    FE_INVALID   = 0x0001,
    FE_DIVBYZERO = 0x0004,
    FE_OVERFLOW  = 0x0008,
    FE_UNDERFLOW = 0x0010,
    FE_INEXACT   = 0x0020
};
#define FE_ALL_EXCEPT (FE_INEXACT | FE_DIVBYZERO | FE_UNDERFLOW | FE_OVERFLOW | FE_INVALID)

enum FPU_RoundMode {
    FE_TONEAREST  = 0x0000,
    FE_DOWNWARD   = 0x0400,
    FE_UPWARD     = 0x0800,
    FE_TOWARDZERO = 0x0C00
};
#define FE_TONEAREST  FE_TONEAREST
#define FE_DOWNWARD   FE_DOWNWARD
#define FE_UPWARD     FE_UPWARD
#define FE_TOWARDZERO FE_TOWARDZERO

struct fpenv_t {
    int sse_mode;
    short int x87_mode;
};

extern fpenv_t FE_DFL_ENV;

inline int feraiseexcept(FPU_Exceptions) { return 0; }
inline int feclearexcept(int) { return 0; }
inline int fegetround() { return FE_TONEAREST; }
inline int fesetround(FPU_RoundMode) { return 0; }
inline int fegetenv(fpenv_t*) { return 0; }
inline int fesetenv(const fpenv_t*) { return 0; }
inline int feholdexcept(fpenv_t*) { return 0; }

template<typename T> inline void streflop_init() {}

} // namespace streflop

#endif // STREFLOP_FPU_H
