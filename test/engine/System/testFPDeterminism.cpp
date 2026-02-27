/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/*
 * Floating-Point Determinism Test Suite
 *
 * Tests every streflop math function used in the engine's synced simulation
 * against hardcoded expected hex values from a reference x86_64 run.
 * Uses bit-exact comparison (memcmp) to ensure cross-architecture determinism.
 *
 * Also verifies that FMA (fused multiply-add) is not leaking into compiled
 * code, which would break sync between machines with different FMA support.
 */

#include <cstdint>
#include <cstring>
#include <cmath>

#include "lib/streflop/streflop_cond.h"

#include <catch_amalgamated.hpp>


// Helper: bit-exact comparison of two floats via memcmp
static bool BitExact(float a, float b) {
	return std::memcmp(&a, &b, sizeof(float)) == 0;
}

// Helper: construct a float from its IEEE-754 hex bit pattern
static float FromBits(uint32_t bits) {
	float f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}

// Helper: extract bits from a float for diagnostic printing
static uint32_t ToBits(float f) {
	uint32_t bits;
	std::memcpy(&bits, &f, sizeof(bits));
	return bits;
}

// Macro: CHECK bit-exact match, with hex output on failure
#define CHECK_BITEXACT(actual, expected) \
	do { \
		float _a = (actual); \
		float _e = (expected); \
		INFO("actual=0x" << std::hex << ToBits(_a) << " expected=0x" << ToBits(_e)); \
		CHECK(BitExact(_a, _e)); \
	} while (0)


// ============================================================
// Reference values from Phase 1 cross-arch test (x86_64 output)
// All values encoded as IEEE-754 single-precision bit patterns
// Generated programmatically from %a hex float output
// ============================================================

TEST_CASE("streflop::sqrt determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::sqrt(streflop::Simple(0.0f)),    FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::sqrt(streflop::Simple(1.0f)),    FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::sqrt(streflop::Simple(2.0f)),    FromBits(0x3FB504F3)); // 0x1.6a09e6p+0
	CHECK_BITEXACT(streflop::sqrt(streflop::Simple(0.5f)),    FromBits(0x3F3504F3)); // 0x1.6a09e6p-1
	CHECK_BITEXACT(streflop::sqrt(streflop::Simple(4.0f)),    FromBits(0x40000000)); // 0x1p+1
	CHECK_BITEXACT(streflop::sqrt(streflop::Simple(1e-20f)),  FromBits(0x2EDBE6FF)); // 0x1.b7cdfep-34
	CHECK_BITEXACT(streflop::sqrt(streflop::Simple(1e+20f)),  FromBits(0x501502F9)); // 0x1.2a05f2p+33
	CHECK_BITEXACT(streflop::sqrt(FromBits(0x00800000)),      FromBits(0x20000000)); // sqrt(FLT_MIN_NORM) = 0x1p-63
	CHECK_BITEXACT(streflop::sqrt(FromBits(0x7F7FFFFF)),      FromBits(0x5F7FFFFF)); // sqrt(FLT_MAX) = 0x1.fffffep+63
	CHECK_BITEXACT(streflop::sqrt(FromBits(0x00000001)),      FromBits(0x1A3504F3)); // sqrt(subnormal) = 0x1.6a09e6p-75
}

TEST_CASE("streflop::sin determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::sin(streflop::Simple(0.0f)),                FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::sin(streflop::Simple(1.0f)),                FromBits(0x3F576AA5)); // 0x1.aed54ap-1
	CHECK_BITEXACT(streflop::sin(streflop::Simple(-1.0f)),               FromBits(0xBF576AA5)); // -0x1.aed54ap-1
	CHECK_BITEXACT(streflop::sin(streflop::Simple(0.5f)),                FromBits(0x3EF57744)); // 0x1.eaee88p-2
	CHECK_BITEXACT(streflop::sin(streflop::Simple(0.7853981633974483f)), FromBits(0x3F3504F4)); // sin(pi/4) = 0x1.6a09e8p-1
	CHECK_BITEXACT(streflop::sin(streflop::Simple(1.5707963267948966f)), FromBits(0x3F800000)); // sin(pi/2) = 0x1p+0
	CHECK_BITEXACT(streflop::sin(streflop::Simple(3.1415926535897932f)), FromBits(0xB3BBBD2E)); // sin(pi) = -0x1.777a5cp-24
	CHECK_BITEXACT(streflop::sin(streflop::Simple(9.4247779607693797f)), FromBits(0xB2CCDE2E)); // sin(3*pi) = -0x1.99bc5cp-26
	CHECK_BITEXACT(streflop::sin(streflop::Simple(1e6f)),                FromBits(0xBEB33259)); // sin(1e6) = -0x1.6664b2p-2
	CHECK_BITEXACT(streflop::sin(streflop::Simple(1e-30f)),              FromBits(0x0DA24260)); // sin(tiny) = 0x1.4484cp-100
}

TEST_CASE("streflop::cos determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::cos(streflop::Simple(0.0f)),                FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::cos(streflop::Simple(1.0f)),                FromBits(0x3F0A5140)); // 0x1.14a28p-1
	CHECK_BITEXACT(streflop::cos(streflop::Simple(-1.0f)),               FromBits(0x3F0A5140)); // 0x1.14a28p-1
	CHECK_BITEXACT(streflop::cos(streflop::Simple(0.5f)),                FromBits(0x3F60A940)); // 0x1.c1528p-1
	CHECK_BITEXACT(streflop::cos(streflop::Simple(0.7853981633974483f)), FromBits(0x3F3504F3)); // cos(pi/4) = 0x1.6a09e6p-1
	CHECK_BITEXACT(streflop::cos(streflop::Simple(1.5707963267948966f)), FromBits(0xB33BBD2E)); // cos(pi/2) = -0x1.777a5cp-25
	CHECK_BITEXACT(streflop::cos(streflop::Simple(3.1415926535897932f)), FromBits(0xBF800000)); // cos(pi) = -0x1p+0
	CHECK_BITEXACT(streflop::cos(streflop::Simple(1e6f)),                FromBits(0x3F6FCEFC)); // cos(1e6) = 0x1.df9df8p-1
}

TEST_CASE("streflop::tan determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::tan(streflop::Simple(0.0f)),                FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::tan(streflop::Simple(1.0f)),                FromBits(0x3FC75923)); // 0x1.8eb246p+0
	CHECK_BITEXACT(streflop::tan(streflop::Simple(-1.0f)),               FromBits(0xBFC75923)); // -0x1.8eb246p+0
	CHECK_BITEXACT(streflop::tan(streflop::Simple(0.5f)),                FromBits(0x3F0BDA7B)); // 0x1.17b4f6p-1
	CHECK_BITEXACT(streflop::tan(streflop::Simple(0.7853981633974483f)), FromBits(0x3F800000)); // tan(pi/4) = 0x1p+0
	CHECK_BITEXACT(streflop::tan(streflop::Simple(1.0471975511965976f)), FromBits(0x3FDDB3D8)); // tan(pi/3) = 0x1.bb67bp+0
	CHECK_BITEXACT(streflop::tan(streflop::Simple(1e6f)),                FromBits(0xBEBF4BB4)); // tan(1e6) = -0x1.7e9768p-2
}

TEST_CASE("streflop::pow determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::pow(streflop::Simple(2.0f),  streflop::Simple(10.0f)),  FromBits(0x44800000)); // 0x1p+10
	CHECK_BITEXACT(streflop::pow(streflop::Simple(2.0f),  streflop::Simple(-1.0f)),  FromBits(0x3F000000)); // 0x1p-1
	CHECK_BITEXACT(streflop::pow(streflop::Simple(2.0f),  streflop::Simple(0.5f)),   FromBits(0x3FB504F3)); // 0x1.6a09e6p+0
	CHECK_BITEXACT(streflop::pow(streflop::Simple(10.0f), streflop::Simple(3.0f)),   FromBits(0x447A0000)); // 0x1.f4p+9
	CHECK_BITEXACT(streflop::pow(streflop::Simple(0.5f),  streflop::Simple(2.0f)),   FromBits(0x3E800000)); // 0x1p-2
	CHECK_BITEXACT(streflop::pow(streflop::Simple(1.0f),  streflop::Simple(999.0f)), FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::pow(streflop::Simple(0.0f),  streflop::Simple(1.0f)),   FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::pow(streflop::Simple(0.0f),  streflop::Simple(0.0f)),   FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::pow(streflop::Simple(-1.0f), streflop::Simple(2.0f)),   FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::pow(streflop::Simple(-1.0f), streflop::Simple(3.0f)),   FromBits(0xBF800000)); // -0x1p+0
	CHECK_BITEXACT(streflop::pow(streflop::Simple(1e10f), streflop::Simple(0.1f)),   FromBits(0x41200000)); // 0x1.4p+3
}

TEST_CASE("streflop::exp determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::exp(streflop::Simple(0.0f)),   FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::exp(streflop::Simple(1.0f)),   FromBits(0x402DF854)); // 0x1.5bf0a8p+1
	CHECK_BITEXACT(streflop::exp(streflop::Simple(-1.0f)),  FromBits(0x3EBC5AB2)); // 0x1.78b564p-2
	CHECK_BITEXACT(streflop::exp(streflop::Simple(2.0f)),   FromBits(0x40EC7326)); // 0x1.d8e64cp+2
	CHECK_BITEXACT(streflop::exp(streflop::Simple(10.0f)),  FromBits(0x46AC14EE)); // 0x1.5829dcp+14
	CHECK_BITEXACT(streflop::exp(streflop::Simple(-10.0f)), FromBits(0x383E6BCE)); // 0x1.7cd79cp-15
	CHECK_BITEXACT(streflop::exp(streflop::Simple(0.001f)), FromBits(0x3F8020C9)); // 0x1.004192p+0
	CHECK_BITEXACT(streflop::exp(streflop::Simple(88.0f)),  FromBits(0x7EF882B7)); // 0x1.f1056ep+126 (near overflow)
	CHECK_BITEXACT(streflop::exp(streflop::Simple(-87.0f)), FromBits(0x00B33687)); // 0x1.666d0ep-126 (near underflow)
}

TEST_CASE("streflop::log determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::log(streflop::Simple(1.0f)),    FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::log(streflop::Simple(2.0f)),    FromBits(0x3F317218)); // 0x1.62e43p-1
	CHECK_BITEXACT(streflop::log(streflop::Simple(0.5f)),    FromBits(0xBF317218)); // -0x1.62e43p-1
	CHECK_BITEXACT(streflop::log(streflop::Simple(10.0f)),   FromBits(0x40135D8E)); // 0x1.26bb1cp+1
	CHECK_BITEXACT(streflop::log(streflop::Simple(1e-10f)),  FromBits(0xC1B834F1)); // -0x1.7069e2p+4
	CHECK_BITEXACT(streflop::log(streflop::Simple(1e+10f)),  FromBits(0x41B834F1)); // 0x1.7069e2p+4
	CHECK_BITEXACT(streflop::log(streflop::Simple(1.0001f)), FromBits(0x38D1BD51)); // 0x1.a37aa2p-14
	CHECK_BITEXACT(streflop::log(FromBits(0x00800000)),      FromBits(0xC2AEAC50)); // log(FLT_MIN_NORM) = -0x1.5d58ap+6
	CHECK_BITEXACT(streflop::log(FromBits(0x7F7FFFFF)),      FromBits(0x42B17218)); // log(FLT_MAX) = 0x1.62e43p+6
}

TEST_CASE("streflop::floor determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::floor(streflop::Simple(0.0f)),   FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::floor(streflop::Simple(1.5f)),   FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::floor(streflop::Simple(-1.5f)),  FromBits(0xC0000000)); // -0x1p+1
	CHECK_BITEXACT(streflop::floor(streflop::Simple(0.999f)), FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::floor(streflop::Simple(-0.001f)),FromBits(0xBF800000)); // -0x1p+0
	CHECK_BITEXACT(streflop::floor(streflop::Simple(1e10f)),  FromBits(0x501502F9)); // 0x1.2a05f2p+33
	CHECK_BITEXACT(streflop::floor(streflop::Simple(-0.0f)),  FromBits(0x80000000)); // -0x0p+0
}

TEST_CASE("streflop::ceil determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::ceil(streflop::Simple(0.0f)),    FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::ceil(streflop::Simple(1.5f)),    FromBits(0x40000000)); // 0x1p+1
	CHECK_BITEXACT(streflop::ceil(streflop::Simple(-1.5f)),   FromBits(0xBF800000)); // -0x1p+0
	CHECK_BITEXACT(streflop::ceil(streflop::Simple(0.001f)),  FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::ceil(streflop::Simple(-0.999f)), FromBits(0x80000000)); // -0x0p+0
	CHECK_BITEXACT(streflop::ceil(streflop::Simple(1e10f)),   FromBits(0x501502F9)); // 0x1.2a05f2p+33
	CHECK_BITEXACT(streflop::ceil(streflop::Simple(-0.0f)),   FromBits(0x80000000)); // -0x0p+0
}

TEST_CASE("streflop::fabs determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::fabs(streflop::Simple(0.0f)),     FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::fabs(streflop::Simple(-0.0f)),    FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::fabs(streflop::Simple(1.0f)),     FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::fabs(streflop::Simple(-1.0f)),    FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::fabs(streflop::Simple(-1e20f)),   FromBits(0x60AD78EC)); // 0x1.5af1d8p+66
	CHECK_BITEXACT(streflop::fabs(FromBits(0x00800000)),       FromBits(0x00800000)); // fabs(FLT_MIN_NORM) = 0x1p-126
	CHECK_BITEXACT(streflop::fabs(-FromBits(0x00000001)),      FromBits(0x00000001)); // fabs(-subnormal) = 0x1p-149
}

TEST_CASE("streflop::atan2 determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(0.0f),  streflop::Simple(1.0f)),   FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(1.0f),  streflop::Simple(0.0f)),   FromBits(0x3FC90FDB)); // 0x1.921fb6p+0
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(1.0f),  streflop::Simple(1.0f)),   FromBits(0x3F490FDB)); // 0x1.921fb6p-1
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(-1.0f), streflop::Simple(-1.0f)),  FromBits(0xC016CBE4)); // -0x1.2d97c8p+1
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(1.0f),  streflop::Simple(-1.0f)),  FromBits(0x4016CBE4)); // 0x1.2d97c8p+1
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(-1.0f), streflop::Simple(1.0f)),   FromBits(0xBF490FDB)); // -0x1.921fb6p-1
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(0.0f),  streflop::Simple(-1.0f)),  FromBits(0x40490FDB)); // 0x1.921fb6p+1
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(1e10f), streflop::Simple(1e-10f)), FromBits(0x3FC90FDB)); // 0x1.921fb6p+0
	CHECK_BITEXACT(streflop::atan2(streflop::Simple(1e-10f),streflop::Simple(1e10f)),  FromBits(0x1E3CE509)); // 0x1.79ca12p-67
}

TEST_CASE("streflop::acos determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::acos(streflop::Simple(1.0f)),   FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::acos(streflop::Simple(0.0f)),   FromBits(0x3FC90FDB)); // 0x1.921fb6p+0
	CHECK_BITEXACT(streflop::acos(streflop::Simple(-1.0f)),  FromBits(0x40490FDB)); // 0x1.921fb6p+1
	CHECK_BITEXACT(streflop::acos(streflop::Simple(0.5f)),   FromBits(0x3F860A92)); // 0x1.0c1524p+0
	CHECK_BITEXACT(streflop::acos(streflop::Simple(-0.5f)),  FromBits(0x40060A92)); // 0x1.0c1524p+1
	CHECK_BITEXACT(streflop::acos(streflop::Simple(0.999f)), FromBits(0x3D37315A)); // 0x1.6e62b4p-5
	CHECK_BITEXACT(streflop::acos(streflop::Simple(-0.999f)),FromBits(0x40463315)); // 0x1.8c662ap+1
}

TEST_CASE("streflop::asin determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::asin(streflop::Simple(0.0f)),   FromBits(0x00000000)); // 0x0p+0
	CHECK_BITEXACT(streflop::asin(streflop::Simple(1.0f)),   FromBits(0x3FC90FDB)); // 0x1.921fb6p+0
	CHECK_BITEXACT(streflop::asin(streflop::Simple(-1.0f)),  FromBits(0xBFC90FDB)); // -0x1.921fb6p+0
	CHECK_BITEXACT(streflop::asin(streflop::Simple(0.5f)),   FromBits(0x3F060A92)); // 0x1.0c1524p-1
	CHECK_BITEXACT(streflop::asin(streflop::Simple(-0.5f)),  FromBits(0xBF060A92)); // -0x1.0c1524p-1
	CHECK_BITEXACT(streflop::asin(streflop::Simple(0.001f)), FromBits(0x3A831270)); // 0x1.0624ep-10
	CHECK_BITEXACT(streflop::asin(streflop::Simple(0.999f)), FromBits(0x3FC35650)); // 0x1.86acap+0
}

TEST_CASE("streflop::fmod determinism", "[fp][determinism]")
{
	CHECK_BITEXACT(streflop::fmod(streflop::Simple(5.0f),   streflop::Simple(3.0f)),   FromBits(0x40000000)); // 0x1p+1
	CHECK_BITEXACT(streflop::fmod(streflop::Simple(-5.0f),  streflop::Simple(3.0f)),   FromBits(0xC0000000)); // -0x1p+1
	CHECK_BITEXACT(streflop::fmod(streflop::Simple(5.0f),   streflop::Simple(-3.0f)),  FromBits(0x40000000)); // 0x1p+1
	CHECK_BITEXACT(streflop::fmod(streflop::Simple(10.5f),  streflop::Simple(0.7f)),   FromBits(0x34400000)); // 0x1.8p-23
	CHECK_BITEXACT(streflop::fmod(streflop::Simple(1e10f),  streflop::Simple(3.0f)),   FromBits(0x3F800000)); // 0x1p+0
	CHECK_BITEXACT(streflop::fmod(streflop::Simple(1.0f),   streflop::Simple(1e-20f)), FromBits(0x1BB71E00)); // 0x1.6e3cp-72
	CHECK_BITEXACT(streflop::fmod(streflop::Simple(0.0f),   streflop::Simple(1.0f)),   FromBits(0x00000000)); // 0x0p+0
}


// ============================================================
// FMA leak detection
//
// If FMA (fused multiply-add) is active, a*b+c is computed with
// a single rounding instead of two. For carefully chosen inputs
// the results differ. If they match, FMA is NOT leaking in.
// If they differ, then -ffp-contract=off is not working.
// ============================================================

TEST_CASE("FMA not active (fp-contract=off)", "[fp][fma]")
{
	// These inputs are chosen so that a*b produces a result that when
	// rounded to float and then added to c gives a different answer
	// than fma(a,b,c) which keeps full precision of the intermediate product.
	volatile float a = 1.0000001f;  // 0x1.000002p+0
	volatile float b = 1.0000001f;  // 0x1.000002p+0
	volatile float c = -1.0000002f; // -0x1.000004p+0

	// Separate multiply then add - should round intermediate product
	volatile float mul = a * b;
	volatile float separate = mul + c;

	// If FMA were active, the compiler could contract a*b+c into a
	// single FMA instruction, giving a different (more precise) result.
	// Using volatile prevents the compiler from optimizing, but let's
	// also check a non-volatile path to catch -ffp-contract=fast leaking in.
	float direct = a * b + c;

	// Both paths should give the same result (no FMA contraction)
	CHECK(BitExact(separate, direct));

	// Additional cross-check: the separate result should NOT equal the
	// mathematically exact (FMA) result for these specific inputs.
	// a*b exactly = 1.00000020000001 (more precision than float can hold)
	// Rounded float intermediate: 1.0000002 (0x3f800002)
	// separate = 1.0000002 + (-1.0000002) = 0.0
	// fma result = 1.00000020000001 + (-1.0000002) = ~1e-14 (nonzero in float)
	//
	// We check the separate result is 0.0 - confirming no FMA contraction
	INFO("separate=" << separate << " direct=" << direct);
	CHECK(separate == 0.0f);
}
