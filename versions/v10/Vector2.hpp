#pragma once

#include <math.h>
#include <ostream>

#include "hlt/constants.hpp"

static int radToDegClipped(const double angle_rad) {
	const long deg_unclipped = lround(angle_rad * 180.0 / M_PI);
	// Make sure return value is in [0, 360) as required by game engine.
	return static_cast<int>(((deg_unclipped % 360L) + 360L) % 360L);
}

struct Vector2 {
public:
	double x, y;

	double DistanceTo(const Vector2& other) const {
		const double dx = x - other.x;
		const double dy = y - other.y;
		return sqrt(dx*dx + dy * dy);
	}

	Vector2 ClosestPointTo(const Vector2& target, const double target_radius, const double min_dist = hlt::constants::MIN_DISTANCE_FOR_CLOSEST_POINT) {
		const double radius = target_radius + min_dist;
		const double angle_rad = target.OrientTowardsRad(*this);

		const double x = target.x + radius * std::cos(angle_rad);
		const double y = target.y + radius * std::sin(angle_rad);

		return { x, y };
	}

	double OrientTowardsRad(const Vector2& target) const {
		const double dx = target.x - x;
		const double dy = target.y - y;

		return atan2(dy, dx) + 2 * M_PI;
	}

	static Vector2 Velocity(const double angle_rad, const int thrust) {
		const double vel_x = cos(angle_rad) * (double)thrust;
		const double vel_y = sin(angle_rad) * (double)thrust;
		return { vel_x, vel_y };
	}

	friend std::ostream& operator<<(std::ostream& out, const Vector2& location);
};

static bool operator==(const Vector2& l1, const Vector2& l2) {
	return l1.x == l2.x && l1.y == l2.y;
}

static Vector2 operator+(const Vector2& l1, const Vector2& l2) {
	return { l1.x + l2.x, l1.y + l2.y };
}

static Vector2 operator-(const Vector2& l1, const Vector2& l2) {
	return { l1.x - l2.x, l1.y - l2.y };
}

static Vector2 operator/(const Vector2& l1, const Vector2& l2) {
	return { l1.x / l2.x, l1.y / l2.y };
}

static Vector2 operator/(const Vector2& l1, const double& l2) {
	return { l1.x / l2, l1.y / l2 };
}

static Vector2 operator*(const Vector2& l1, const double& l2) {
	return { l1.x * l2, l1.y * l2 };
}

static Vector2 operator-(const Vector2& l1, const double& l2) {
	return { l1.x - l2, l1.y - l2 };
}

static Vector2 operator+(const Vector2& l1, const double& l2) {
	return { l1.x + l2, l1.y + l2 };
}