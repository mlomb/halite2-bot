#include "Navigation.hpp"

#include <deque>
#include <queue>
#include <unordered_set>
#include <algorithm>

#include "Instance.hpp"
#include "Log.hpp"
#include "Image.hpp"

const double angular_step_rad = M_PI / 180.0; // 1 degree

static bool check_entity_between(const Vector2& start, const Vector2& target, const Entity* entity_to_check)
{
	if (Navigation::segment_circle_intersect(start, target, entity_to_check->location, entity_to_check->radius, hlt::constants::FORECAST_FUDGE_FACTOR))
		return true;
	return false;
}

static bool are_objects_between(const Vector2& start, const Vector2& target) {
	Instance* instance = Instance::Get();

	for (auto& kv : instance->planets) {
		if (check_entity_between(start, target, kv.second))
			return true;
	}

	for (auto& kv : instance->ships) {
		if (kv.second->frozen || !kv.second->IsOur()) { // frozen ships or enemy ships
			if (check_entity_between(start, target, kv.second))
				return true;
		}
	}

	return false;
}

bool Navigation::IsOutsideTheMap(const Vector2& location)
{
	Instance* instance = Instance::Get();
	return location.x <= 0 || location.y <= 0 || location.x >= instance->map_width - 1 || location.y >= instance->map_height - 1;
}

possibly<Vector2> Navigation::GetNavigationTargetTowardsTarget(const Vector2& location, const Vector2& target)
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

		if (!are_objects_between(location, new_target)) {
			return { new_target, true };
		}
	}

	return { Vector2(), false };
}

void Navigation::CalculateMovementPoint(NavigationRequest* navReq) {
	Map* map = Instance::Get()->map;

	const double movementRadius = hlt::constants::SHIP_RADIUS * 2 + hlt::constants::MAX_SPEED;

	navReq->movementPoint = navReq->targetLocation;

	if (navReq->avoid_enemies) {
		double minDistTarget = INF;
		int minEnemies = INF;

		Vector2 turnAheadPos;
		double minDistShipTurnAhead = INF;
		int minEnemiesTurnAhead = INF;

		map->IterateMap(navReq->ship->location, movementRadius + hlt::constants::MAX_SPEED * 2 /* two turns ahead */, [&](Vector2 position, MapCell& cell, double distance) {
			if (cell.solid) return;

			if (distance <= movementRadius) {
				if (cell.nextTurnEnemyShipsAttackInRange <= minEnemies) {
					double distTarget = position.DistanceTo(navReq->targetLocation);
					if (cell.nextTurnEnemyShipsAttackInRange < minEnemies || distTarget < minDistTarget) {
						navReq->movementPoint = position;
						minDistTarget = distTarget;
						minEnemies = cell.nextTurnEnemyShipsAttackInRange;
					}
				}
			}
			else { // we're in a turn ahead
				if (cell.nextTurnEnemyShipsAttackInRange <= minEnemiesTurnAhead) {
					double distShip = position.DistanceTo(navReq->ship->location);
					if (cell.nextTurnEnemyShipsAttackInRange < minEnemiesTurnAhead || distShip < minDistShipTurnAhead) {
						turnAheadPos = position;
						minDistShipTurnAhead = distShip;
						minEnemiesTurnAhead = cell.nextTurnEnemyShipsAttackInRange;
					}
				}
			}
		});

		if (minEnemiesTurnAhead < minEnemies) {
			navReq->movementPoint = turnAheadPos;
		}
	}
	else {
		double minDist = INF;
		int maxEnemies = 0;
		bool foundAttackPoint = false;

		map->IterateMap(navReq->ship->location, movementRadius, [&](Vector2 position, MapCell& cell, double distance) {
			if (cell.solid) return;

			if (cell.nextTurnFriendlyShipsTakingDamage > cell.nextTurnEnemyShipsAttackInRange) {
				if (cell.nextTurnEnemyShipsTakingDamage >= maxEnemies) {
					double dist = position.DistanceTo(navReq->targetLocation);
					if (cell.nextTurnEnemyShipsTakingDamage > maxEnemies || dist < minDist) {
						maxEnemies = cell.nextTurnEnemyShipsTakingDamage;
						navReq->movementPoint = position;
						minDist = dist;
						foundAttackPoint = true;
					}
				}
			}
		});

		if (!foundAttackPoint) {
			Log::log() << "Ship " << navReq->ship->entity_id << " couldn't find an attack point, this ship will instead run!" << std::endl;
			navReq->avoid_enemies = true;
			CalculateMovementPoint(navReq);
		}
	}
}

void Navigation::CalculateMovementPoints(std::set<NavigationRequest*>& navigationRequests)
{
	for (NavigationRequest* navReq : navigationRequests) {
		CalculateMovementPoint(navReq);
	}
}

static void CalculateDesiredMovement(std::set<NavigationRequest*>& navigationRequests) {
	bool requireRecalculation = false;

	auto it = navigationRequests.begin();
	for (; it != navigationRequests.end();) {
		NavigationRequest* navReq = *it;

		possibly<Vector2> navTarget = Navigation::GetNavigationTargetTowardsTarget(navReq->ship->location, navReq->movementPoint);
		if (navTarget.second) {
			double distance = navReq->ship->location.DistanceTo(navTarget.first);
			// Do not round up, since overshooting might cause collision.
			int thrust = std::min((int)distance,  hlt::constants::MAX_SPEED);

			if (thrust > 0) {
				navReq->desiredMovement = NavigationState(navReq->ship->location.OrientTowardsRad(navTarget.first), thrust);
				navReq->currentMovement = navReq->desiredMovement;
				++it;
				continue;
			}
		}

		Log::log() << "Ship " << navReq->ship->entity_id << ": Navigation is impossible" << std::endl;
		navReq->ship->frozen = true;
		requireRecalculation = true;

		it = navigationRequests.erase(it);
	}
	if (requireRecalculation)
		CalculateDesiredMovement(navigationRequests);
}

static void AlignShips(std::set<NavigationRequest*>& navigationRequests) {
	int global_cluster_id = 0;
	for (NavigationRequest* navReqA : navigationRequests) {
		if (navReqA->cluster_id == -1) {
			navReqA->cluster_id = global_cluster_id++;

			std::vector<NavigationRequest*> cluster;
			cluster.push_back(navReqA);

			for (NavigationRequest* navReqB : navigationRequests) {
				if (navReqB->cluster_id == -1) {
					if (navReqA->ship->location.DistanceTo(navReqB->ship->location) < 5 && fabs(navReqA->desiredMovement.angle_rad - navReqB->desiredMovement.angle_rad) < angular_step_rad * 30) {
						navReqB->cluster_id = navReqA->cluster_id;
						cluster.push_back(navReqB);
					}
				}
			}

			if (cluster.size() > 1) {
				double averageAngle = 0;
				double averageThrust = 0;
				for (NavigationRequest* navReqCluster : cluster) {
					averageAngle += navReqCluster->desiredMovement.angle_rad;
					averageThrust += navReqCluster->desiredMovement.thrust;
				}

				averageAngle /= cluster.size();
				averageThrust /= cluster.size();

				NavigationState clusterNavigationState = NavigationState(averageAngle, (int)ceil(averageThrust));

				for (NavigationRequest* navReqCluster : cluster) {
					navReqCluster->desiredMovement = clusterNavigationState;
					navReqCluster->currentMovement = navReqCluster->desiredMovement;
				}
			}
		}
	}
}

/*

	README:
	This function is very inefficient, I know
	With 320+ ships I get timeout :(

*/
std::vector<Move> Navigation::NavigateShips(std::set<NavigationRequest*> navigationRequests)
{
	Stopwatch s("Navigate " + std::to_string(navigationRequests.size()) + " ships");
	Log::log() << "Navigating " << navigationRequests.size() << " ships." << std::endl;

	Instance* instance = Instance::Get();
	Map* map = instance->map;
	
	// 0. Fill event horizons
	for (auto it1 = navigationRequests.begin(); it1 != navigationRequests.end(); it1++) {
		NavigationRequest* navReqA = *it1;

		for (auto it2 = std::next(it1); it2 != navigationRequests.end(); it2++) {
			NavigationRequest* navReqB = *it2;

			if (navReqA->ship->location.DistanceTo(navReqB->ship->location) < (hlt::constants::SHIP_RADIUS + hlt::constants::MAX_SPEED) * 2 + hlt::constants::SHIP_RADIUS) {
				navReqA->eventHorizon.insert(navReqB);
				navReqB->eventHorizon.insert(navReqA);
			}
		}
	}

	// 1 Clear and fill the map
	{
		Stopwatch s("Clear and fill the map");
		map->ClearMap();
		map->FillMap();
	}

	// 2. Calculate the movement points
	CalculateMovementPoints(navigationRequests);

	// 3. Calculate the desired movement (the desired direction, without trajectory collisions)
	CalculateDesiredMovement(navigationRequests);

	// 4. Align ships with similar trajectories
	AlignShips(navigationRequests);

	// 5 Compute conflicts and prepare the queue
	std::deque<NavigationRequest*> q;
	for (NavigationRequest* navReq : navigationRequests) {
		navReq->ComputeConflicts();
		q.push_front(navReq);
	}

	int seekDegrees = 30;
	
	while (!q.empty()) {
		// 6. Pick the request with more conflicts
		std::sort(q.begin(), q.end(), [](const NavigationRequest* a, const NavigationRequest* b) {
			if(a->conflicts.size() == b->conflicts.size())
				return a->ship->task_priority < b->ship->task_priority;
			else
				return a->conflicts.size() > b->conflicts.size();
		});

		NavigationRequest* navReq = q.front();
		q.pop_front();

		const double distance = navReq->ship->location.DistanceTo(navReq->movementPoint);

		// 7. Solve the conflict
		bool conflict = true;
		for (int t = navReq->desiredMovement.thrust; conflict && t > 0; t--) {
			for (int i = 1; conflict && i < seekDegrees * 2; i++) {
				const double direction = i % 2 == 0 ? 1 : -1;
				double angle_rad = navReq->desiredMovement.angle_rad + angular_step_rad * (i / 2.0) * direction;

				const double new_target_dx = cos(angle_rad) * distance;
				const double new_target_dy = sin(angle_rad) * distance;
				const Vector2 new_target = navReq->ship->location + Vector2{ new_target_dx, new_target_dy };

				navReq->currentMovement = NavigationState(angle_rad, t);

				//Log::log() << "Ship " << navReq->ship->entity_id << " " << navReq->ship->location << " is testing angle_deg=" << radToDegClipped(angle_rad) << ", thrust=" << t << std::endl;

				navReq->ComputeConflicts();
				if (!navReq->HasConflicts()) {
					//Log::log() << "No conflict with " << navReq->currentMovement.thrust << " of thrust!" << std::endl;
					conflict = false;
				}
			}
		}
		if (conflict) {
			// If the conflict couldnt be resolved, this ship will stand still

			if (seekDegrees != 3 && instance->CurrentTurnTime() > 1500) { // we're running out of time, lets give up some accuarity
				Log::log("We're running out of time, decreasing accuarity");
				seekDegrees = 3;
			}

			// remove the ship
			map->ModifyShip(navReq->ship, -1);

			navReq->ship->frozen = true;

			// add the frozen ship
			map->ModifyShip(navReq->ship);

			// remove this navigation request
			navigationRequests.erase(navReq);
			for (NavigationRequest* navReqOther : navReq->eventHorizon) {
				navReqOther->eventHorizon.erase(navReq);
			}

			// update the affected ships
			CalculateMovementPoints(navReq->eventHorizon);
			CalculateDesiredMovement(navReq->eventHorizon);
			AlignShips(navReq->eventHorizon);
			for (NavigationRequest* navReqOther : navReq->eventHorizon) {
				navReqOther->ComputeConflicts();

				auto it = std::find(q.begin(), q.end(), navReqOther);
				if (it == q.end())
					q.push_back(navReqOther);
			}

			//Log::log() << "Ship " << navReq->ship->entity_id << " couldn't solve the conflicts." << std::endl;
		}
	}

	// 8. Populate the move commands
	std::vector<Move> moves;
	for (NavigationRequest* navReq : navigationRequests) {
		int angle_deg = radToDegClipped(navReq->currentMovement.angle_rad);

		Log::log() << "Ship " << navReq->ship->entity_id << " will move with angle_deg=" << angle_deg << ", thrust=" << navReq->currentMovement.thrust << std::endl;
		moves.push_back(Move::thrust(navReq->ship->entity_id, navReq->currentMovement.thrust, angle_deg));
	}

	return moves;
}


void NavigationRequest::ComputeConflicts()
{
	conflicts.clear();

	for (NavigationRequest* otherNavReq : eventHorizon) {
		if (NavigationRequest::Conflict(this, otherNavReq)) {
			conflicts.insert(otherNavReq);
		}
	}
}

bool NavigationRequest::HasConflicts()
{
	if (conflicts.size() > 0)
		return true;

	Vector2 futurePosition = ship->location + currentMovement.velocity;

	if (Navigation::IsOutsideTheMap(futurePosition))
		return true;

	if (are_objects_between(ship->location, futurePosition))
		return true;

	return false;
}

bool NavigationRequest::Conflict(const NavigationRequest* navReqA, const NavigationRequest* navReqB)
{
	const double r = hlt::constants::SHIP_RADIUS * 2;
	auto t = Navigation::collision_time(r, navReqA->ship->location, navReqB->ship->location, navReqA->currentMovement.velocity, navReqB->currentMovement.velocity);
	return t.first && t.second >= 0 && t.second <= 1;
}















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
bool Navigation::segment_circle_intersect(const Vector2& start, const Vector2& end, const Vector2& circlePosition, const double circleRadius, const double fudge)
{
	// Parameterize the segment as start + t * (end - start),
	// and substitute into the equation of a circle
	// Solve for t
	const double circle_radius = circleRadius;
	const double start_x = start.x;
	const double start_y = start.y;
	const double end_x = end.x;
	const double end_y = end.y;
	const double center_x = circlePosition.x;
	const double center_y = circlePosition.y;
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
		return start.DistanceTo(circlePosition) <= circle_radius + fudge;
	}

	// Time along segment when closest to the circle (vertex of the quadratic)
	const double t = std::min(-b / (2 * a), 1.0);
	if (t < 0) {
		return false;
	}

	const double closest_x = start_x + dx * t;
	const double closest_y = start_y + dy * t;
	const double closest_distance = Vector2{ closest_x, closest_y }.DistanceTo(circlePosition);

	return closest_distance <= circle_radius + fudge;
}

std::pair<bool, double> Navigation::collision_time(long double r, const Vector2& loc1, const Vector2& loc2, const Vector2& vel1, const Vector2& vel2)
{
	// With credit to Ben Spector
	// Simplified derivation:
	// 1. Set up the distance between the two entities in terms of time,
	//    the difference between their velocities and the difference between
	//    their positions
	// 2. Equate the distance equal to the event radius (max possible distance
	//    they could be)
	// 3. Solve the resulting quadratic

	const auto dx = loc1.x - loc2.x;
	const auto dy = loc1.y - loc2.y;
	const auto dvx = vel1.x - vel2.x;
	const auto dvy = vel1.y - vel2.y;

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