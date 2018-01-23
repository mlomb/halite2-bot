#pragma once

#include <algorithm>

#include "entity.hpp"
#include "location.hpp"

namespace hlt {
    namespace collision {
        static double square(const double num) {
            return num * num;
        }

        /**
         * Test whether a given line segment intersects a circular area.
         *
         * @param start  The start of the segment.
         * @param end    The end of the segment.
         * @param circle The circle to test against.
         * @param fudge  An additional safety zone to leave when looking for collisions. Probably set it to ship radius.
         * @return true if the segment intersects, false otherwise
         */
        static bool segment_circle_intersect(
                const Location& start,
                const Location& end,
                const Entity& circle,
                const double fudge)
        {
            // Parameterize the segment as start + t * (end - start),
            // and substitute into the equation of a circle
            // Solve for t
            const double circle_radius = circle.radius;
            const double start_x = start.pos_x;
            const double start_y = start.pos_y;
            const double end_x = end.pos_x;
            const double end_y = end.pos_y;
            const double center_x = circle.location.pos_x;
            const double center_y = circle.location.pos_y;
            const double dx = end_x - start_x;
            const double dy = end_y - start_y;

            const double a = square(dx) + square(dy);

            const double b =
                    -2 * (square(start_x) - (start_x * end_x)
                    - (start_x * center_x) + (end_x * center_x)
                    + square(start_y) - (start_y * end_y)
                    - (start_y * center_y) + (end_y * center_y));

            if (a == 0.0) {
                // Start and end are the same point
                return start.get_distance_to(circle.location) <= circle_radius + fudge;
            }

            // Time along segment when closest to the circle (vertex of the quadratic)
            const double t = std::min(-b / (2 * a), 1.0);
            if (t < 0) {
                return false;
            }

            const double closest_x = start_x + dx * t;
            const double closest_y = start_y + dy * t;
            const double closest_distance = Location{ closest_x, closest_y }.get_distance_to(circle.location);

            return closest_distance <= circle_radius + fudge;
        }

		static possibly<Location> segment_segment_intersect(Location p0, Location p1, Location p2, Location p3) {
			double A1 = p1.pos_y - p0.pos_y,
				   B1 = p0.pos_x - p1.pos_x,
				   C1 = A1 * p0.pos_x + B1 * p0.pos_y,
				   A2 = p3.pos_y - p2.pos_y,
				   B2 = p2.pos_x - p3.pos_x,
				   C2 = A2 * p2.pos_x + B2 * p2.pos_y,
				   denominator = A1 * B2 - A2 * B1;

			if (denominator == 0) {
				return { Location(), false };
			}

			double intersectX = (B2 * C1 - B1 * C2) / denominator,
				   intersectY = (A1 * C2 - A2 * C1) / denominator,
				   rx0 = (intersectX - p0.pos_x) / (p1.pos_x - p0.pos_x),
				   ry0 = (intersectY - p0.pos_y) / (p1.pos_y - p0.pos_y),
				   rx1 = (intersectX - p2.pos_x) / (p3.pos_x - p2.pos_x),
				   ry1 = (intersectY - p2.pos_y) / (p3.pos_y - p2.pos_y);

			if (((rx0 >= 0 && rx0 <= 1) || (ry0 >= 0 && ry0 <= 1)) &&
				((rx1 >= 0 && rx1 <= 1) || (ry1 >= 0 && ry1 <= 1))) {
				return { Location(intersectX, intersectY), true };
			}
			else {
				return { Location(), false };
			}
		}


		auto collision_time(
			long double r,
			const hlt::Location& loc1, const hlt::Location& loc2,
			const hlt::Location& vel1, const hlt::Location& vel2
		) -> std::pair<bool, double> {
			// With credit to Ben Spector
			// Simplified derivation:
			// 1. Set up the distance between the two entities in terms of time,
			//    the difference between their velocities and the difference between
			//    their positions
			// 2. Equate the distance equal to the event radius (max possible distance
			//    they could be)
			// 3. Solve the resulting quadratic

			const auto dx = loc1.pos_x - loc2.pos_x;
			const auto dy = loc1.pos_y - loc2.pos_y;
			const auto dvx = vel1.pos_x - vel2.pos_x;
			const auto dvy = vel1.pos_y - vel2.pos_y;

			// Quadratic formula
			const auto a = std::pow(dvx, 2) + std::pow(dvy, 2);
			const auto b = 2 * (dx * dvx + dy * dvy);
			const auto c = std::pow(dx, 2) + std::pow(dy, 2) - std::pow(r, 2);

			const auto disc = std::pow(b, 2) - 4 * a * c;

			if (a == 0.0) {
				if (b == 0.0) {
					if (c <= 0.0) {
						// Implies r^2 >= dx^2 + dy^2 and the two are already colliding
						return { true, 0.0 };
					}
					return { false, 0.0 };
				}
				const auto t = -c / b;
				if (t >= 0.0) {
					return { true, t };
				}
				return { false, 0.0 };
			}
			else if (disc == 0.0) {
				// One solution
				const auto t = -b / (2 * a);
				return { true, t };
			}
			else if (disc > 0) {
				const auto t1 = -b + std::sqrt(disc);
				const auto t2 = -b - std::sqrt(disc);

				if (t1 >= 0.0 && t2 >= 0.0) {
					return { true, std::min(t1, t2) / (2 * a) };
				}
				else if (t1 <= 0.0 && t2 <= 0.0) {
					return { true, std::max(t1, t2) / (2 * a) };
				}
				else {
					return { true, 0.0 };
				}
			}
			else {
				return { false, 0.0 };
			}
		}
    }
}
