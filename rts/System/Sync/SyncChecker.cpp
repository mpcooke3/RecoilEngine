/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#ifdef SYNCCHECK

#include "SyncChecker.h"

// This cannot be included in the header file (SyncChecker.h) because include conflicts will occur.
#include "System/Threading/ThreadPool.h"

#include <cstdio>
#include <cstring>
#include <execinfo.h>


unsigned CSyncChecker::g_checksum;
int CSyncChecker::inSyncedCode;
int CSyncChecker::syncFrameNum = -1;

void CSyncChecker::NewFrame()
{
	g_checksum = 0xfade1eaf;
#ifdef SYNC_HISTORY
	LogHistory();
#endif // SYNC_HISTORY
}

void CSyncChecker::debugSyncCheckThreading()
{
	assert(ThreadPool::GetThreadNum() == 0);
}

void CSyncChecker::Sync(const void* p, unsigned size)
{
#ifdef DEBUG_SYNC_MT_CHECK
	// Sync calls should not be occurring in multi-threaded sections
	debugSyncCheckThreading();
#endif
	// most common cases first, make it easy for compiler to optimize for it
	// simple xor is not enough to detect multiple zeroes, e.g.
	g_checksum = spring::LiteHash(p, size, g_checksum);
	//LOG("[Sync::Checker] chksum=%u\n", g_checksum);

#ifdef SYNC_HISTORY
	LogHistory();
#endif // SYNC_HISTORY
}

void CSyncChecker::TraceOp(const void* p, unsigned size, const char* msg)
{
	const int frame = syncFrameNum;
	if (!((frame >= 0 && frame <= 2) || (frame >= 21440 && frame <= 21485)))
		return;

	static FILE* tf = nullptr;
	static int seqNum = 0;

	if (tf == nullptr) {
		tf = fopen("/tmp/sync_trace_out.txt", "w");
		if (tf == nullptr)
			return;
		setvbuf(tf, nullptr, _IOLBF, 0);
	}

	const unsigned int crc = g_checksum;
	if (size == sizeof(float)) {
		float val;
		memcpy(&val, p, sizeof(float));
		fprintf(tf, "[ST] %d f=%d op=%s chk=%08x val=%a\n", seqNum, frame, msg, crc, val);
	} else if (size == sizeof(int)) {
		int val;
		memcpy(&val, p, sizeof(int));
		fprintf(tf, "[ST] %d f=%d op=%s chk=%08x ival=%d\n", seqNum, frame, msg, crc, val);
	} else if (size == sizeof(short)) {
		short val;
		memcpy(&val, p, sizeof(short));
		fprintf(tf, "[ST] %d f=%d op=%s chk=%08x sval=%d\n", seqNum, frame, msg, crc, val);
	} else {
		fprintf(tf, "[ST] %d f=%d op=%s chk=%08x sz=%u\n", seqNum, frame, msg, crc, size);
	}
	// Print stack trace at the exact divergence point
	if (frame == 21460 && seqNum == 2566781) {
		fprintf(tf, "[ST] === STACK TRACE at divergence point (seq=%d frame=%d) ===\n", seqNum, frame);
		void* callstack[32];
		int frames = backtrace(callstack, 32);
		char** symbols = backtrace_symbols(callstack, frames);
		if (symbols) {
			for (int i = 0; i < frames; i++)
				fprintf(tf, "[ST]   %s\n", symbols[i]);
			free(symbols);
		}
		fprintf(tf, "[ST] === END STACK TRACE ===\n");
		fflush(tf);
	}

	++seqNum;

	// Flush and close after last traced frame to ensure output is complete
	if (frame == 21485) {
		static int closeCountdown = 100000; // allow some ops in last frame
		if (--closeCountdown <= 0) {
			fflush(tf);
		}
	}
}

#ifdef SYNC_HISTORY

unsigned CSyncChecker::nextHistoryIndex = 0;
unsigned CSyncChecker::nextFrameIndex = 0;
std::array<unsigned, MAX_SYNC_HISTORY> CSyncChecker::logs;
std::array<unsigned, MAX_SYNC_HISTORY_FRAMES> CSyncChecker::logFrames;

void CSyncChecker::NewGameFrame()
{
	logFrames[nextFrameIndex++] = nextHistoryIndex;
	if (nextFrameIndex == MAX_SYNC_HISTORY_FRAMES)
		nextFrameIndex = 0;
}

void CSyncChecker::LogHistory()
{
	logs[nextHistoryIndex++] = g_checksum;
	if (nextHistoryIndex == MAX_SYNC_HISTORY)
		nextHistoryIndex = 0;
}

std::tuple<unsigned, unsigned, unsigned*> CSyncChecker::GetFrameHistory(unsigned rewindFrames)
{
	int endFrameIndex = nextFrameIndex - rewindFrames;
	int startFrameIndex = endFrameIndex - 1;

	if (endFrameIndex < 0)
		endFrameIndex = MAX_SYNC_HISTORY_FRAMES + endFrameIndex;
	if (startFrameIndex < 0)
		startFrameIndex = MAX_SYNC_HISTORY_FRAMES + startFrameIndex;

	return std::make_tuple(logFrames[startFrameIndex], logFrames[endFrameIndex], logs.data());
}

#endif // SYNC_HISTORY

#endif // SYNCCHECK
