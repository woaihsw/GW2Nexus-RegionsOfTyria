#ifndef MAP_RETRY_H
#define MAP_RETRY_H

#include <chrono>

inline constexpr std::chrono::seconds kMapRetryInitialDelay{30};
inline constexpr std::chrono::seconds kMapRetryMaxDelay{300};

struct MapLoadRetryState {
	int failureCount = 0;
	std::chrono::steady_clock::time_point nextRetryAt{};
};

inline std::chrono::seconds mapRetryDelay(int failureCount) {
	if (failureCount <= 0) {
		return std::chrono::seconds{0};
	}

	std::chrono::seconds delay = kMapRetryInitialDelay;
	for (int step = 1; step < failureCount; ++step) {
		if (delay >= kMapRetryMaxDelay / 2) {
			return kMapRetryMaxDelay;
		}
		delay *= 2;
	}
	if (delay > kMapRetryMaxDelay) {
		return kMapRetryMaxDelay;
	}
	return delay;
}

inline bool mapRetryIsDue(
	std::chrono::steady_clock::time_point now,
	const MapLoadRetryState& state) {
	return now >= state.nextRetryAt;
}

#endif
