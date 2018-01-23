#pragma once

#include <vector>

#include "hlt/constants.hpp"

#include "Types.hpp"
#include "Move.hpp"
#include "Vector2.hpp"

class Ship;

struct NavigationRequest {
	Vector2 targetLocation;
	bool avoid_enemies;
	Ship* ship;
};

class Navigation {
public:

	static bool IsOutsideTheMap(const Vector2& location);
	static std::vector<std::pair<Vector2, Vector2>> CalculateDangerTrajectories(const Vector2& position, const Vector2& velocity);

	static possibly<std::pair<double, Vector2>> GetBestAngleNavigateShipTowardsTarget(const Vector2& location, const Vector2& target);

	static possibly<Move> NavigateShipTowardsTarget(Ship* ship, const Vector2& target, const unsigned int max_thrust);

	// angle thrust
	static possibly<std::pair<int, int>> AStarPathfinding(const Vector2& start, const Vector2& end);

	static std::vector<Move> NavigateShips(const std::vector<NavigationRequest>& navigationRequests);
private:
	Navigation();
};