#ifndef SECTOR_GEOMETRY_H
#define SECTOR_GEOMETRY_H

#include <cstddef>
#include <vector>

template <typename Point>
inline bool pointInPolygon(float x, float y, const std::vector<Point>& bounds) {
	const size_t count = bounds.size();
	if (count < 3) {
		return false;
	}

	bool inside = false;
	for (size_t i = 0, j = count - 1; i < count; j = i++) {
		const float yi = bounds[i].y;
		const float yj = bounds[j].y;
		const float xi = bounds[i].x;
		const float xj = bounds[j].x;
		if ((yi > y) != (yj > y) &&
			(x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
			inside = !inside;
		}
	}
	return inside;
}

#endif
