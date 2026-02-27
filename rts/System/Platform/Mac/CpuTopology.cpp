/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "System/Platform/CpuTopology.h"
#include "System/Log/ILog.h"

#include <sys/sysctl.h>
#include <unistd.h>

namespace cpu_topology {

static uint32_t getSysctlInt(const char* name, uint32_t fallback = 0) {
	uint32_t value = fallback;
	size_t size = sizeof(value);
	if (sysctlbyname(name, &value, &size, nullptr, 0) != 0)
		return fallback;
	return value;
}

ProcessorMasks GetProcessorMasks() {
	ProcessorMasks masks;

	const uint32_t numCpus = getSysctlInt("hw.logicalcpu", 1);
	const uint32_t nperflevels = getSysctlInt("hw.nperflevels", 1);

	if (nperflevels >= 2) {
		// Apple Silicon with P-cores and E-cores
		const uint32_t pcoreCount = getSysctlInt("hw.perflevel0.logicalcpu", numCpus);
		const uint32_t ecoreCount = getSysctlInt("hw.perflevel1.logicalcpu", 0);

		// Assign P-cores to the lower bits, E-cores to the upper bits.
		// This is a logical assignment since macOS doesn't expose core-to-bit mapping.
		for (uint32_t i = 0; i < pcoreCount && i < 32; ++i)
			masks.performanceCoreMask |= (1u << i);
		for (uint32_t i = 0; i < ecoreCount && (pcoreCount + i) < 32; ++i)
			masks.efficiencyCoreMask |= (1u << (pcoreCount + i));
	} else {
		// All cores are performance cores (Intel Mac or single-tier Apple Silicon)
		for (uint32_t i = 0; i < numCpus && i < 32; ++i)
			masks.performanceCoreMask |= (1u << i);
	}

	// macOS does not expose SMT/HT topology
	masks.hyperThreadLowMask = 0;
	masks.hyperThreadHighMask = 0;

	return masks;
}

ProcessorCaches GetProcessorCache() {
	ProcessorCaches caches;
	ProcessorGroupCaches group;

	const uint32_t numCpus = getSysctlInt("hw.logicalcpu", 1);
	const uint32_t l1d = getSysctlInt("hw.l1dcachesize", 0);
	const uint32_t l2 = getSysctlInt("hw.l2cachesize", 0);

	// macOS doesn't expose per-core cache topology; treat all cores as one group
	for (uint32_t i = 0; i < numCpus && i < 32; ++i)
		group.groupMask |= (1u << i);

	group.cacheSizes[0] = l1d;
	group.cacheSizes[1] = l2;
	group.cacheSizes[2] = 0; // L3 not always present on Apple Silicon

	caches.groupCaches.push_back(group);
	return caches;
}

ThreadPinPolicy GetThreadPinPolicy() {
	// macOS does not support thread-to-core pinning
	return THREAD_PIN_POLICY_NONE;
}

} // namespace cpu_topology
