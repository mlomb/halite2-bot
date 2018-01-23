#include "Navigation.hpp"

#include <deque>
#include <unordered_set>
#include <algorithm>

#include "Instance.hpp"
#include "Log.hpp"
#include "Image.hpp"

#include "hlt/collision.hpp"

static void check_and_add_entity_between(
	std::vector<const Entity*>& entities_found,
	const Vector2& start,
	const Vector2& target,
	const Entity* entity_to_check)
{
	const Vector2& location = entity_to_check->location;
	if (location == start || location == target) {
		return;
	}
	if (hlt::collision::segment_circle_intersect({ start.x, start.y }, { target.x, target.y }, { entity_to_check->location.x, entity_to_check->location.y }, entity_to_check->radius, hlt::constants::FORECAST_FUDGE_FACTOR)) {
		entities_found.push_back(entity_to_check);
	}
}

static std::vector<const Entity*> objects_between(const Vector2& start, const Vector2& target) {
	Instance* instance = Instance::Get();

	std::vector<const Entity*> entities_found;

	for (auto& kv : instance->planets) {
		if (kv.second->alive) {
			check_and_add_entity_between(entities_found, start, target, kv.second);
		}
	}

	for (auto& kv : instance->ships) {
		if (kv.second->alive) {
			//if (kv.second->frozen) { // docked ships
				check_and_add_entity_between(entities_found, start, target, kv.second);
			//}
		}
	}

	return entities_found;
}

bool Navigation::IsOutsideTheMap(const Vector2& location)
{
	Instance* instance = Instance::Get();
	return location.x <= 0 || location.y <= 0 || location.x >= instance->map_width - 1 || location.y >= instance->map_height - 1;
}

std::vector<std::pair<Vector2, Vector2>> Navigation::CalculateDangerTrajectories(const Vector2& position, const Vector2& velocity)
{
	Instance* instance = Instance::Get();

	std::vector<std::pair<Vector2, Vector2>> dangerTrajectories;
	for (std::pair<Vector2, Vector2>& st : instance->shipTrajectories) {
		const double r = hlt::constants::SHIP_RADIUS * 2;
		auto t = hlt::collision::collision_time(r, { st.first.x, st.first.y }, { position.x, position.y }, { st.second.x, st.second.y }, { velocity.x, velocity.y });
		if (t.first && t.second >= 0 && t.second <= 1)
			dangerTrajectories.push_back(st);
	}
	return dangerTrajectories;
}

possibly<std::pair<double, Vector2>> Navigation::GetBestAngleNavigateShipTowardsTarget(const Vector2& location, const Vector2& target)
{
	const double angular_step_rad = M_PI / 180.0 / 2.0; // half degree

	const double distance = location.DistanceTo(target);
	double target_angle_rad = location.OrientTowardsRad(target);

	for (int i = 0; i < 360 * 2; i++) {
		const double direction = i % 2 == 0 ? 1 : -1;
		double angle_rad = target_angle_rad + angular_step_rad * (i / 2.0) * direction;

		const double new_target_dx = cos(angle_rad) * distance;
		const double new_target_dy = sin(angle_rad) * distance;
		const Vector2 new_target = location + Vector2{ new_target_dx, new_target_dy };
		
		if (objects_between(location, new_target).empty()) {
			return { { angle_rad, new_target }, true };
		}
	}

	return { { 0.0, Vector2() }, false };
}

possibly<Move> Navigation::NavigateShipTowardsTarget(Ship* ship, const Vector2& target, const unsigned int max_thrust)
{
	possibly<std::pair<double, Vector2>> nav = GetBestAngleNavigateShipTowardsTarget(ship->location, target);

	if (!nav.second) {
		Log::log() << "Ship " << ship->entity_id << ": No angle found" << std::endl;
		return { Move::noop(), false };
	}

	double angle_rad = nav.first.first;
	double distance = ship->location.DistanceTo(nav.first.second);

	int thrust;
	if (distance < max_thrust) {
		// Do not round up, since overshooting might cause collision.
		thrust = (int)distance;
	}
	else {
		thrust = max_thrust;
	}

	int angle_deg = radToDegClipped(angle_rad);
	angle_rad = (angle_deg * M_PI) / 180.0; // clamped angle

	Vector2 velocity = Vector2::Velocity(angle_rad, thrust);
	Vector2 futurePosition = ship->location + velocity;

	// check if we are not crossing another ship's trajectory
	std::vector<std::pair<Vector2, Vector2>> dangerTrajectories = CalculateDangerTrajectories(ship->location, velocity);

	if (!dangerTrajectories.empty() || IsOutsideTheMap(futurePosition)) {
		// we'll need to reduce the thrust
		while (thrust > 0 && (!dangerTrajectories.empty() || IsOutsideTheMap(futurePosition))) {
			thrust--;
			if (thrust == 0) break;
			Log::log("Ship " + std::to_string(ship->entity_id) + " have to reduce thrust to " + std::to_string(thrust) + " because its intersecting " + std::to_string(dangerTrajectories.size()) + " other trajectories");
			velocity = Vector2::Velocity(angle_rad, thrust);
			futurePosition = ship->location + velocity;
			dangerTrajectories = CalculateDangerTrajectories(ship->location, velocity);
		}
	}

	if (thrust == 0)
		return { Move::noop(), false };

	Instance::Get()->shipTrajectories.push_back(std::make_pair(ship->location, velocity));

	return { Move::thrust(ship->entity_id, thrust, angle_deg), true };
}

std::vector<Move> Navigation::NavigateShips(const std::vector<NavigationRequest>& navigationRequests)
{
	Instance* instance = Instance::Get();

	const double movementRadius = hlt::constants::SHIP_RADIUS * 2 + hlt::constants::MAX_SPEED;

	std::vector<Move> moves;

	for (const NavigationRequest& navReq : navigationRequests) {
		possibly<Move> move = { Move::noop(), false };

		if (navReq.avoid_enemies) {
			move = Navigation::NavigateShipTowardsTarget(navReq.ship, navReq.targetLocation, hlt::constants::MAX_SPEED);
#if 0
			Vector2 bestLocation;
			double minDistTarget = INF;
			double minDistShip = INF;
			int minEnemies = INF;

			instance->IterateMap(navReq.ship->location, movementRadius + hlt::constants::MAX_SPEED /* one movement ahead */, [&](Vector2 position, MapCell& cell, double distance) {
				if (cell.solid) return;

				if (cell.nextTurnEnemyShipsAttackInRange <= minEnemies) {
					double distTarget = position.DistanceTo(navReq.targetLocation);
					double distShip = position.DistanceTo(navReq.ship->location);
					if (cell.nextTurnEnemyShipsAttackInRange < minEnemies || distTarget < minDistTarget || distShip < minDistShip) {
						bestLocation = position;
						minDistTarget = distTarget;
						minDistShip = distShip;
						minEnemies = cell.nextTurnEnemyShipsAttackInRange;
					}
				}
			});

			move = Navigation::NavigateShipTowardsTarget(navReq.ship, bestLocation, hlt::constants::MAX_SPEED);
#endif
		}
		else {
			move = Navigation::NavigateShipTowardsTarget(navReq.ship, navReq.targetLocation, hlt::constants::MAX_SPEED);
#if 0
			Vector2 bestLocation;
			double bestRatio = -1;
			double minDist = INF;

			instance->IterateMap(navReq.ship->location, movementRadius, [&](Vector2 position, MapCell& cell, double distance) {
				if (cell.solid) return;

				if (cell.friendlyShipsThatCanReachThere > cell.nextTurnEnemyShipsAttackInRange) {
					double ratio = 1;
					if (cell.nextTurnEnemyShipsTakingDamage > 0)
						ratio = cell.friendlyShipsThatCanReachThere / cell.nextTurnEnemyShipsTakingDamage;

					if (ratio >= bestRatio) {
						double dist = position.DistanceTo(navReq.targetLocation);
						if (ratio > bestRatio || dist < minDist) {
							bestRatio = ratio;
							bestLocation = position;
							minDist = dist;
						}
					}
				}
			});

			if (bestRatio >= 0) {
				move = Navigation::NavigateShipTowardsTarget(navReq.ship, bestLocation, hlt::constants::MAX_SPEED);
			}
			else {
				Log::log() << "Couldn't find any movement spot!" << std::endl;
			}
#endif
		}

		if (move.second) {
			moves.push_back(move.first);
		}
		else
			Log::log() << "Ship " << navReq.ship->entity_id <<": Navigation is impossible" << std::endl;

	}
	return moves;
}























static bool AlmostEqual(double v1, double v2)
{
	return (std::fabs(v1 - v2) < std::fabs(std::min(v1, v2)) * std::numeric_limits<double>::epsilon());
}

possibly<std::pair<int, int>> Navigation::AStarPathfinding(const Vector2& start, const Vector2& end)
{
	Log::log() << "Calculating path form " << start << " to " << end << std::endl;

	Instance* instance = Instance::Get();

	MapCell* endCell = &instance->GetCell(end);
	MapCell* startCell = &instance->GetCell(start);

	if (endCell->solid || startCell->solid)
		return { {0, 0}, false };

	startCell->astar_visited = true;
	startCell->astar_G = 0;
	startCell->astar_F = 0;
	startCell->astar_parent = 0;

	static std::deque<MapCell*> q;
	static std::unordered_set<MapCell*> checked;

	q.clear();
	checked.clear();

	q.push_back(startCell);
	checked.insert(startCell);

	while (!q.empty()) {
		std::sort(q.begin(), q.end(), [](const MapCell* a, const MapCell* b) {
			if (AlmostEqual(a->astar_F, b->astar_F))
				return a->astar_G < b->astar_G;
			return a->astar_F < b->astar_F;
		});

		MapCell* cell = q.front();
		q.pop_front();

		cell->astar_visited = true;
		
		if (cell == endCell) {
			Log::log() << "PATH FOUND" << std::endl;
			break;
		}

		instance->IterateMap(cell->location, hlt::constants::SHIP_RADIUS * 2 + hlt::constants::MAX_SPEED, [&](Vector2 position, MapCell& ncell, double distance) {
			if (ncell.solid) return;
			if (ncell.astar_visited) return;

			double ncellG = cell->astar_G + cell->location.DistanceTo(ncell.location);

			if (ncellG < ncell.astar_G) {
				ncell.astar_G = ncellG;
				ncell.astar_F = ncellG + ncell.location.DistanceTo(endCell->location);
				ncell.astar_parent = cell;

				if (std::find(q.begin(), q.end(), &ncell) == q.end()) {
					q.push_front(&ncell);
				}

				checked.insert(&ncell);
			}
		});
	}

	Log::log() << "Calculated in " << checked.size() << std::endl;

	// reconstruct paht
	MapCell* pathNode = endCell;
	while (pathNode != nullptr) {

		Log::log() << "Path cell " << pathNode->location << std::endl;

		pathNode = pathNode->astar_parent;
	}
	
	/*
	static int path = 0;
	Image::WriteImage(std::string("turns/path_turn_") + std::to_string(instance->turn) + "_" + std::to_string(path++) + ".bmp", MAP_WIDTH, MAP_HEIGHT, [&](int x, int y) -> std::tuple<unsigned char, unsigned char, unsigned char> {
		y = MAP_HEIGHT - y - 1;

		if (instance->map[y][x].ship)
			return std::make_tuple(0, 0, 255);

		if (instance->map[y][x].solid)
			return std::make_tuple(0, 0, 0);


		MapCell* pathNode = endCell;
		while (pathNode != nullptr) {
			if (pathNode->location.x == x && pathNode->location.y == y) {
				if (pathNode == endCell)
					return std::make_tuple(0, 255, 0);
				return std::make_tuple(255, 0, 0);
			}

			pathNode = pathNode->astar_parent;
		}

		for (MapCell* c : checked) {
			if (c->astar_visited && c->location.x == x && c->location.y == y) {
				return std::make_tuple(255, 0, 255);
			}
		}
		return std::make_tuple(255, 255, 255);
	});
	*/

	// reset cells
	for (MapCell* cell : checked) {
		cell->astar_visited = false;
		cell->astar_parent = 0;
		cell->astar_F = INF;
		cell->astar_G = INF;
	}

	return possibly<std::pair<int, int>>();
}
