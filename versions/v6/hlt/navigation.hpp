#pragma once

#include <math.h>

#include "collision.hpp"
#include "map.hpp"
#include "move.hpp"
#include "util.hpp"
#include "log.hpp"

namespace hlt {
    namespace navigation {
		static std::vector<std::pair<Location, Location>> shipTrajectories;

		static void clear_collision_map(const Map& map) {
			shipTrajectories.clear();
		}

        static void check_and_add_entity_between(
                std::vector<const Entity *>& entities_found,
                const Location& start,
                const Location& target,
                const Entity& entity_to_check)
        {
            const Location &location = entity_to_check.location;
            if (location == start || location == target) {
                return;
            }
            if (collision::segment_circle_intersect(start, target, entity_to_check, constants::FORECAST_FUDGE_FACTOR)) {
                entities_found.push_back(&entity_to_check);
            }
        }

        static std::vector<const Entity *> objects_between(const Map& map, const Location& start, const Location& target) {
            std::vector<const Entity *> entities_found;

            for (const Planet& planet : map.planets) {
                check_and_add_entity_between(entities_found, start, target, planet);
            }

            for (const auto& player_ship : map.ships) {
                for (const Ship& ship : player_ship.second) {
                    check_and_add_entity_between(entities_found, start, target, ship);
                }
            }

            return entities_found;
        }

		static std::vector<std::pair<Location, Location>> calculate_danger_trajectories(Location location, Location velocity) {
			std::vector<std::pair<Location, Location>> dangerTrajectories;
			for (std::pair<Location, Location>& st : shipTrajectories) {
				const double r = constants::SHIP_RADIUS * 2;
				auto t = collision::collision_time(r, st.first, location, st.second, velocity);
				if (t.first && t.second >= 0 && t.second <= 1)
					dangerTrajectories.push_back(st);
			}
			return dangerTrajectories;
		}

		static Location calculate_ship_velocity(Location location, double angle_rad, int thrust) {
			//const double futureX = location.pos_x + cos(angle_rad) * (double)thrust;
			//const double futureY = location.pos_y + sin(angle_rad) * (double)thrust;

			const double vel_x = cos(angle_rad) * (double)thrust;
			const double vel_y = sin(angle_rad) * (double)thrust;
			return Location(vel_x, vel_y);
		}

        static possibly<Move> navigate_ship_towards_target(
                const Map& map,
                const Ship& ship,
                const Location& target,
                const int max_thrust,
                const bool avoid_obstacles = true,
                const int max_corrections = constants::MAX_NAVIGATION_CORRECTIONS,
                const double angular_step_rad = M_PI / 180.0)
        {
            if (max_corrections <= 0) {
                return { Move::noop(), false };
            }

            const double distance = ship.location.get_distance_to(target);
            double angle_rad = ship.location.orient_towards_in_rad(target);

            if (avoid_obstacles && !objects_between(map, ship.location, target).empty()) {
				const double new_target_dx = cos(angle_rad + angular_step_rad) * distance;
				const double new_target_dy = sin(angle_rad + angular_step_rad) * distance;
				const Location new_target = { ship.location.pos_x + new_target_dx, ship.location.pos_y + new_target_dy };

                return navigate_ship_towards_target(
                        map, ship, new_target, max_thrust, true, (max_corrections - 1), angular_step_rad);
            }

            int thrust;
            if (distance < max_thrust) {
                // Do not round up, since overshooting might cause collision.
                thrust = (int) distance;
            } else {
                thrust = max_thrust;
            }

			int angle_deg = util::angle_rad_to_deg_clipped(angle_rad);
			angle_rad = (angle_deg * M_PI) / 180; // clamped angle

			Location velocity = calculate_ship_velocity(ship.location, angle_rad, thrust);

			// check if we are not crossing another ship's trajectory
			std::vector<std::pair<Location, Location>> dangerTrajectories = calculate_danger_trajectories(ship.location, velocity);

			if (!dangerTrajectories.empty()) {
				// we'll need to reduce the thrust
				while (thrust > 0 && !dangerTrajectories.empty()) {
					thrust--;
					if (thrust == 0) break;
					hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " have to reduce thrust to " + std::to_string(thrust) + " because its intersecting " + std::to_string(dangerTrajectories.size()) + " other trajectories");
					velocity = calculate_ship_velocity(ship.location, angle_rad, thrust);
					dangerTrajectories = calculate_danger_trajectories(ship.location, velocity);
				}
			}

			if(thrust == 0)
				return { Move::noop(), false };

			//hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " current position is (" + std::to_string(ship.location.pos_x) + ", " + std::to_string(ship.location.pos_y) + ")");
			//hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " next turn position should be (" + std::to_string(x) + ", " + std::to_string(y) + ")");

			shipTrajectories.push_back(std::make_pair(ship.location, velocity));

            return { Move::thrust(ship.entity_id, thrust, angle_deg), true };
        }

        static possibly<Move> navigate_ship_to_dock(
                const Map& map,
                const Ship& ship,
                const Entity& dock_target,
                const int max_thrust)
        {
            const int max_corrections = constants::MAX_NAVIGATION_CORRECTIONS;
            const bool avoid_obstacles = true;
            const double angular_step_rad = M_PI / 180.0;
            const Location& target = ship.location.get_closest_point(dock_target.location, dock_target.radius);

            return navigate_ship_towards_target(
                    map, ship, target, max_thrust, avoid_obstacles, max_corrections, angular_step_rad);
        }
    }
}
