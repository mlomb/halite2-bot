#include "Navigation.hpp"

#include <deque>
#include <queue>
#include <unordered_set>
#include <algorithm>

#include "Instance.hpp"
#include "Log.hpp"
#include "Image.hpp"

const double angular_step_rad = M_PI / 180.0; // 1 degree

double random(int min, int max) {
	static bool first = true;
	if (first)
	{
		srand(time(NULL)); //seeding for the first time only!
		first = false;
	}
	return min + rand() % ((max + 1) - min);
}

bool Navigation::CheckEntityBetween(const Vector2& start, const Vector2& target, const Entity* entity_to_check)
{
	if (Navigation::segment_circle_intersect(start, target, entity_to_check->location, entity_to_check->radius, hlt::constants::FORECAST_FUDGE_FACTOR))
		return true;
	return false;
}

bool Navigation::AreObjectsBetween(const Vector2& start, const Vector2& target) {
	Instance* instance = Instance::Get();

	for (auto& kv : instance->planets) {
		if (CheckEntityBetween(start, target, kv.second))
			return true;
	}

	for (auto& kv : instance->ships) {
		if (kv.second->frozen || !kv.second->IsCommandable() || !kv.second->IsOur()) { // frozen ships or enemy ships
			if (CheckEntityBetween(start, target, kv.second))
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

double Navigation::GetPositionScore(const Vector2& position, const Vector2& targetLocation, bool avoiding_enemies)
{
	Map* map = Instance::Get()->map;

	if (Navigation::IsOutsideTheMap(position)) {
		return -99;
	}
	else {
		MapCell& cell = map->GetCell(position);
		if (avoiding_enemies) {
			return (100 - cell.nextTurnEnemyShipsAttackInRange) * 10000 + MAX_DISTANCE - position.DistanceTo(targetLocation);
		}
		else {
			if (cell.nextTurnFriendlyShipsTakingDamage > cell.nextTurnEnemyShipsAttackInRange) {
				return cell.nextTurnEnemyShipsTakingDamage * 10000 + MAX_DISTANCE - position.DistanceTo(targetLocation);
			}
			else {
				return -99;
			}
		}
	}
}

void CalculateScores(std::set<NavigationRequest*>& navigationRequests) {
	Instance* instance = Instance::Get();

	for (NavigationRequest* navReq : navigationRequests) {
		Ship* ship = navReq->ship;
		
		for (NavigationOption& option : ship->navigationOptions) {
			if (option.thrust > ship->max_thrusts[option.angle] || (option.future && !navReq->avoid_enemies)) {
				option.score = -99;
			}
			else {
				double bestScore = -INF;
				for (int t_off = 0; t_off < (option.future ? 8 : 1); t_off++) {
					Vector2 position = ship->location + instance->velocityCache[option.angle][option.thrust + t_off];
					double score = Navigation::GetPositionScore(position, navReq->targetLocation, navReq->avoid_enemies);
					if (score > bestScore) {
						bestScore = score;
					}
					else {
						break;
					}
				}
				option.score = bestScore;
			}
			//Log::log() << "Ship " << ship->entity_id << " angle: " << option.angle << " thrust: " << option.thrust << " max_thrust: " << max_thrusts[option.angle] << " future: " << option.future << " score: " << option.score << std::endl;
		}

		std::sort(ship->navigationOptions.begin(), ship->navigationOptions.end());
		ship->optionSelected = 0;
	}
}

std::vector<Move> Navigation::NavigateShips(std::set<NavigationRequest*> navigationRequests)
{
	Stopwatch s("Navigate " + std::to_string(navigationRequests.size()) + " ships");
	Log::log() << "Navigating " << navigationRequests.size() << " ships." << std::endl;

	Instance* instance = Instance::Get();
	Map* map = instance->map;

	
	{
		Stopwatch s("Filling event horizons");
		for (auto it1 = navigationRequests.begin(); it1 != navigationRequests.end(); it1++) {
			NavigationRequest* navReqA = *it1;

			for (auto it2 = std::next(it1); it2 != navigationRequests.end(); it2++) {
				NavigationRequest* navReqB = *it2;

				if (navReqA->ship->location.DistanceTo(navReqB->ship->location) < (hlt::constants::SHIP_RADIUS + hlt::constants::MAX_SPEED) * 2 + hlt::constants::SHIP_RADIUS) {
					navReqA->eventHorizon.insert(navReqB);
					navReqB->eventHorizon.insert(navReqA);
				}
			}

			Ship* ship = navReqA->ship;
			ship->collisionEventHorizon.clear();
			ship->collisionEventHorizon = instance->GetEntitiesInside(ship, hlt::constants::MAX_SPEED);
		}
	}

	{
		Stopwatch s("Calculating max thrusts");
		for (NavigationRequest* navReq : navigationRequests) {
			navReq->ship->UpdateMaxThrusts();
		}
	}

	{
		Stopwatch s("Clear and fill the map");
		map->ClearMap();
		map->FillMap();
	}

	{
		Stopwatch s("Calculating scores 1st time");
		CalculateScores(navigationRequests);
	}

	// pick options
	{
		Stopwatch s("Picking options");
		std::deque<NavigationRequest*> q;
		for (NavigationRequest* navReq : navigationRequests) {
			q.push_front(navReq);
		}

		while (!q.empty()) {
			NavigationRequest* navReq = q.front();
			Ship* ship = navReq->ship;
			q.pop_front();

			for (ship->optionSelected = 0; ship->optionSelected < ship->navigationOptions.size(); ship->optionSelected++) {
				const NavigationOption& option = ship->navigationOptions[ship->optionSelected];
				const Vector2& velocity = instance->velocityCache[option.angle][option.thrust];
				const Vector2 futurePosition = ship->location + velocity;

				bool conflict = Navigation::IsOutsideTheMap(futurePosition);
				conflict |= option.score <= -99;

				if (!conflict) {
					for (NavigationRequest* navReqOther : navigationRequests) {
						if (navReq == navReqOther) continue;

						Ship* shipOther = navReqOther->ship;
						const NavigationOption& otherOption = shipOther->navigationOptions[shipOther->optionSelected];

						const double r = hlt::constants::SHIP_RADIUS * 2;
						auto t = Navigation::collision_time(r, ship->location, shipOther->location, velocity, instance->velocityCache[otherOption.angle][otherOption.thrust]);
						if (t.first && t.second >= 0 && t.second <= 1) { // collision
							conflict = true;
							break;
						}
					}
				}

				if (!conflict)
					break; // yay!
			}

			if (ship->optionSelected > ship->navigationOptions.size() - 1) {
				// If the conflict couldnt be resolved, this ship will stand still

				ship->optionSelected = -1;
				
				// remove the ship
				map->ModifyShip(navReq->ship, -1);

				navReq->ship->frozen = true;

				// remove this navigation request
				navigationRequests.erase(navReq);
				for (NavigationRequest* navReqOther : navReq->eventHorizon) {
					navReqOther->eventHorizon.erase(navReq);
					navReqOther->ship->collisionEventHorizon.push_back(navReq->ship);
					navReqOther->ship->UpdateMaxThrusts();
				}

				// add the frozen ship
				map->ModifyShip(navReq->ship);

				// update the affected ships
				CalculateScores(navReq->eventHorizon);
				for (NavigationRequest* navReqOther : navReq->eventHorizon) {
					auto it = std::find(q.begin(), q.end(), navReqOther);
					if (it == q.end())
						q.push_front(navReqOther);
				}
				//Log::log() << "Ship " << navReq->ship->entity_id << " couldn't solve the conflicts." << std::endl;
			}
			else {
				//Log::log() << "Ship " << navReq->ship->entity_id << " picked option " << navReq->ship->optionSelected << std::endl;
			}
		}
	}

	std::vector<Move> moves;

	moves.reserve(navigationRequests.size());
	for (NavigationRequest* navReq : navigationRequests) {
		Ship* ship = navReq->ship;

		Log::log() << "Ship " << ship->entity_id << ": " << ship->optionSelected << std::endl;

		if (ship->optionSelected == -1)
			continue;

		const NavigationOption& option = ship->navigationOptions[ship->optionSelected];

		moves.push_back(Move::thrust(ship->entity_id, option.thrust, option.angle));
	}

	return moves;
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
