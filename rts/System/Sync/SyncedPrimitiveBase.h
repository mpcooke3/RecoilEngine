/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef SYNCED_PRIMITIVE_BASSE_H
#define SYNCED_PRIMITIVE_BASSE_H

#ifdef SYNCCHECK
	#include "SyncChecker.h"
#endif

#ifdef SYNCDEBUG
	#include "SyncDebugger.h"
#endif

#include <assert.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>


// NOTE: lowercase sync clashes with extern void sync(...) from unistd.h
namespace Sync {

	static inline void AssertDebugger(const void* p, unsigned size, const char* msg) {
	#ifdef SYNCDEBUG
		CSyncDebugger::GetInstance()->Sync(p, size, msg);
	#endif
	}

	/**
	 * @brief Check sync of the argument x.
	 */
	template<typename T>
	static inline void AssertDebugger(const T& x, const char* msg = "assert") {
		AssertDebugger(&x, sizeof(T), msg);
	}


	/**
	 * @brief Assert a contiguous memory area is still synchronized.
	 * @param p    Start of the memory area
	 * @param size Size of the memory area
	 * @param msg  An arbitrary debugging text (preferably short)
	 *
	 * (A checksum of) the memory may be passed to either the CSyncDebugger,
	 * or the CSyncChecker, depending on the enabled @code #defines @endcode.
	 */
	static inline void Assert(const void* p, unsigned size, const char* msg) {
		AssertDebugger(p, size, msg);
#ifdef SYNCCHECK
		assert(CSyncChecker::InSyncedCode());
		CSyncChecker::Sync(p, size);
	#ifdef TRACE_SYNC
		unsigned int crc = CSyncChecker::GetChecksum();
		fprintf(stderr, "[Sync::%s] msg=%s chksum=%u\n", __func__, msg, crc);
	#endif
		if (CSyncChecker::IsTracing()) {
			int idx = CSyncChecker::GetWriteIndex();
			CSyncChecker::IncrementWriteIndex();
			unsigned chk = CSyncChecker::GetChecksum();
			FILE* tf = CSyncChecker::GetTraceFile();
			fprintf(tf, "%d W %d %08x sz=%u msg=%s hex=",
				CSyncChecker::GetFrameNum(), idx, chk, size, msg);
			const uint8_t* bytes = static_cast<const uint8_t*>(p);
			for (unsigned i = 0; i < size && i < 32; i++)
				fprintf(tf, "%02x", bytes[i]);
			// If 4 bytes, interpret as float for easier analysis
			if (size == 4) {
				float fv;
				std::memcpy(&fv, p, 4);
				uint32_t iv;
				std::memcpy(&iv, p, 4);
				int flagDenorm = (iv & 0x7f800000) == 0 && (iv & 0x007fffff) != 0;
				fprintf(tf, " f=%.9g%s", (double)fv, flagDenorm ? " DENORM" : "");
			}
			// If 2 bytes, interpret as short
			if (size == 2) {
				int16_t sv;
				std::memcpy(&sv, p, 2);
				fprintf(tf, " s=%d", (int)sv);
			}
			fprintf(tf, "\n");
		}
#endif
	}

	static inline void Assert(uint32_t val, const char* msg) {
		AssertDebugger(val, msg);
#ifdef SYNCCHECK
		assert(CSyncChecker::InSyncedCode());
		CSyncChecker::Sync(val);
	#ifdef TRACE_SYNC
		unsigned int crc = CSyncChecker::GetChecksum();
		fprintf(stderr, "[Sync::%s] msg=%s chksum=%u\n", __func__, msg, crc);
	#endif
		if (CSyncChecker::IsTracing()) {
			int idx = CSyncChecker::GetWriteIndex();
			CSyncChecker::IncrementWriteIndex();
			unsigned chk = CSyncChecker::GetChecksum();
			FILE* tf = CSyncChecker::GetTraceFile();
			// Also interpret as float in case it's a float written via hash_combine
			float fv;
			std::memcpy(&fv, &val, 4);
			int flagDenorm = (val & 0x7f800000) == 0 && (val & 0x007fffff) != 0;
			fprintf(tf, "%d W %d %08x sz=4u msg=%s val=%u (0x%08x) f=%.9g%s\n",
				CSyncChecker::GetFrameNum(), idx, chk, msg,
				val, val, (double)fv, flagDenorm ? " DENORM" : "");
		}
#endif
	}

	/**
	 * @brief Check sync of the argument x.
	 */
	template<typename T>
	static inline void Assert(const T& x, const char* msg = "assert") {
		Assert(&x, sizeof(T), msg);
	}

}

#if !defined(NDEBUG) && defined(SYNCCHECK)
#  define ENTER_SYNCED_CODE() CSyncChecker::EnterSyncedCode()
#  define LEAVE_SYNCED_CODE() CSyncChecker::LeaveSyncedCode()
#else
#  define ENTER_SYNCED_CODE()
#  define LEAVE_SYNCED_CODE()
#endif

#ifdef SYNCDEBUG
#  define ASSERT_SYNCED(x) Sync::AssertDebugger(x, "assert(" #x ")")
#else
#  define ASSERT_SYNCED(x)
#endif

#endif
