#include "Navigation.hpp"

#include "Instance.hpp"
#include "Log.hpp"

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
			//if (!kv.second->IsCommandable()) // docked ships
			check_and_add_entity_between(entities_found, start, target, kv.second);
		}
	}

	return entities_found;
}

bool Navigation::IsOutsideTheMap(const Vector2& location)
{
	Instance* instance = Instance::Get();
	return location.x <= 0 || location.y <= 0 || location.x >= instance->map_width || location.y >= instance->map_height;
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

possibly<Move> Navigation::NavigateShipTowardsTarget(Ship* ship, const Vector2& target, const unsigned int max_thrust, const bool avoid_obstacles, const int max_corrections)
{
	if (max_corrections <= 0) {
		Log::log("Ship " + std::to_string(ship->entity_id) + " run out of corrections");
		return { Move::noop(), false };
	}

	const double angular_step_rad = M_PI / 180.0;

	const double distance = ship->location.DistanceTo(target);
	double angle_rad = ship->location.OrientTowardsRad(target);

	if (avoid_obstacles && !objects_between(ship->location, target).empty()) {
		const double new_target_dx = cos(angle_rad + angular_step_rad) * distance;
		const double new_target_dy = sin(angle_rad + angular_step_rad) * distance;
		const Vector2 new_target = { ship->location.x + new_target_dx, ship->location.y + new_target_dy };

		return NavigateShipTowardsTarget(ship, new_target, max_thrust, avoid_obstacles, (max_corrections - 1));
	}

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
