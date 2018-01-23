#include "Navigation.hpp"

#include <deque>
#include <queue>
#include <unordered_set>
#include <algorithm>

#include "Instance.hpp"
#include "Log.hpp"
#include "Image.hpp"

static void check_and_add_entity_between(
	std::vector<const Entity*>& entities_found,
	const Vector2& start,
	const Vector2& target,
	const Entity* entity_to_check)
{
	const Vector2& location = entity_to_check->location;
	//if (location == start || location == target) {
	//	return;
	//}
	if (Navigation::segment_circle_intersect(start, target, entity_to_check->location, entity_to_check->radius, hlt::constants::FORECAST_FUDGE_FACTOR)) {
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
			if (kv.second->frozen || kv.second->owner_id != instance->player_id) { // frozen ships or enemy ships
				check_and_add_entity_between(entities_found, start, target, kv.second);
			}
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
		auto t = collision_time(r, st.first, position, st.second, velocity);
		if (t.first && t.second >= 0 && t.second <= 1)
			dangerTrajectories.push_back(st);
	}
	return dangerTrajectories;
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

		if (objects_between(location, new_target).empty()) {
			return { new_target, true };
		}
	}

	return { Vector2(), false };
}

possibly<int> Navigation::ReduceThrustUntilSafe(const Vector2& location, const Vector2& target, const int max_thrust)
{
	double angle_rad = location.OrientTowardsRad(target);
	double distance = location.DistanceTo(target);

	int thrust;
	if (distance < max_thrust) {
		// Do not round up, since overshooting might cause collision.
		thrust = (int)distance;
	}
	else {
		thrust = max_thrust;
	}

	Vector2 velocity = Vector2::Velocity(angle_rad, thrust);
	Vector2 futurePosition = location + velocity;

	// check if we are not crossing another ship's trajectory
	std::vector<std::pair<Vector2, Vector2>> dangerTrajectories = CalculateDangerTrajectories(location, velocity);

	if (!dangerTrajectories.empty() || IsOutsideTheMap(futurePosition)) {
		// we'll need to reduce the thrust
		while (thrust > 0 && (!dangerTrajectories.empty() || IsOutsideTheMap(futurePosition))) {
			thrust--;
			if (thrust == 0) break;
			//Log::log("... reducing thrust to " + std::to_string(thrust) + " because its intersecting " + std::to_string(dangerTrajectories.size()) + " other trajectories");
			velocity = Vector2::Velocity(angle_rad, thrust);
			futurePosition = location + velocity;
			dangerTrajectories = CalculateDangerTrajectories(location, velocity);
		}
	}

	return { thrust, thrust != 0 };
}

static void CalculateDesiredMovement(std::vector<NavigationRequest*>& navigationRequests) {
	bool requireRecalculation = false;

	auto it = navigationRequests.begin();
	for (; it != navigationRequests.end();) {
		NavigationRequest* navReq = *it;

		possibly<Vector2> navTarget = Navigation::GetNavigationTargetTowardsTarget(navReq->ship->location, navReq->targetLocation);
		if (navTarget.second) {
			double distance = navReq->ship->location.DistanceTo(navTarget.first);
			// Do not round up, since overshooting might cause collision.
			int thrust = std::min((int)distance,  hlt::constants::MAX_SPEED);

			if (thrust > 0) {
				navReq->desiredMovement = NavigationState(navReq->ship->location.OrientTowardsRad(navTarget.first), thrust, navTarget.first);
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
	else {
		for (NavigationRequest* navReq : navigationRequests) {
			navReq->ComputeConflicts(navigationRequests);
		}
	}
}

std::vector<Move> Navigation::NavigateShips(std::vector<NavigationRequest*> navigationRequests)
{
	Log::log() << "Navigating " << navigationRequests.size() << " ships." << std::endl;

	Instance* instance = Instance::Get();

	// 1. Calculate the desired movement (the desired direction, without trajectory collisions)
	CalculateDesiredMovement(navigationRequests);

	std::deque<NavigationRequest*> q;
	for (NavigationRequest* navReq : navigationRequests) {
		q.push_front(navReq);
	}

	const double angular_step_rad = M_PI / 180.0; // 1 degree

	while (!q.empty()) {
		std::sort(q.begin(), q.end(), [](const NavigationRequest* a, const NavigationRequest* b) {
			return a->conflicts.size() > b->conflicts.size();
		});

		NavigationRequest* navReq = q.front();
		q.pop_front();

		const double distance = navReq->ship->location.DistanceTo(navReq->targetLocation);

		bool conflict = true;
		for (int t = navReq->desiredMovement.thrust; conflict && t > 0; t--) {
			for (int i = 0; conflict && i < 15 * 2; i++) {
				const double direction = i % 2 == 0 ? 1 : -1;
				double angle_rad = navReq->desiredMovement.angle_rad + angular_step_rad * (i / 2.0) * direction;

				const double new_target_dx = cos(angle_rad) * distance;
				const double new_target_dy = sin(angle_rad) * distance;
				const Vector2 new_target = navReq->ship->location + Vector2{ new_target_dx, new_target_dy };

				navReq->currentMovement = NavigationState(angle_rad, t, new_target);

				//Log::log() << "Ship " << navReq->ship->entity_id << " " << navReq->ship->location << " is testing angle_deg=" << radToDegClipped(angle_rad) << ", thrust=" << t << std::endl;

				navReq->ComputeConflicts(navigationRequests);
				if (!navReq->HasConflicts()) {
					//Log::log() << "No conflict with " << navReq->currentMovement.thrust << " of thrust!" << std::endl;
					conflict = false;
				}
			}
		}
		if (conflict) {
			navReq->ship->frozen = true;

			navigationRequests.erase(std::find(navigationRequests.begin(), navigationRequests.end(), navReq));

			Log::log() << "Ship " << navReq->ship->entity_id << " couldn't solve the conflicts." << std::endl;
			
			return NavigateShips(navigationRequests);
		}
	}

	std::vector<Move> moves;
	for (NavigationRequest* navReq : navigationRequests) {
		int angle_deg = radToDegClipped(navReq->currentMovement.angle_rad);

		Log::log() << "Ship " << navReq->ship->entity_id << " will move with angle_deg=" << angle_deg << ", thrust=" << navReq->currentMovement.thrust << std::endl;
		moves.push_back(Move::thrust(navReq->ship->entity_id, navReq->currentMovement.thrust, angle_deg));
	}

	return moves;
}


void NavigationRequest::ComputeConflicts(std::vector<NavigationRequest*>& otherNavigationRequests)
{
	conflicts.clear();

	for (NavigationRequest* otherNavReq : otherNavigationRequests) {
		if (this == otherNavReq) continue;
		if (otherNavReq->ship->frozen) continue;

		if (NavigationRequest::Conflict(this, otherNavReq)) {
			conflicts.push_back(otherNavReq);
		}
	}

}

bool NavigationRequest::HasConflicts()
{
	Vector2 futurePosition = ship->location + currentMovement.velocity;

	if (Navigation::IsOutsideTheMap(futurePosition))
		return true;

	if (conflicts.size() > 0)
		return true;

	if (!objects_between(ship->location, futurePosition).empty())
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