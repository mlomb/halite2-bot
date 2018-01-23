#pragma once

#include "hlt/constants.hpp"

#include "Types.hpp"
#include "Move.hpp"
#include "Ship.hpp"

class Navigation {
public:

	static bool IsOutsideTheMap(const Vector2& location);
	static std::vector<std::pair<Vector2, Vector2>> CalculateDangerTrajectories(const Vector2& position, const Vector2& velocity);
	static possibly<Move> NavigateShipTowardsTarget(Ship* ship, const Vector2& target, const unsigned int max_thrust, const bool avoid_obstacles = true, const int max_corrections = hlt::constants::MAX_NAVIGATION_CORRECTIONS);

private:
	Navigation();
};