#pragma once

#include <vector>
#include <set>

#include "constants.hpp"

#include "Types.hpp"
#include "Move.hpp"
#include "Vector2.hpp"
#include "Entity.hpp"

class Ship;
class NavigationRequest;

class NavigationOption {
public:
	NavigationOption(int angle, int thrust, bool future = false) : angle(angle), thrust(thrust), future(future), score(-99) {
	}

	int angle;
	int thrust;
	bool future;
	double score;

	bool operator<(const NavigationOption& other) const
	{
		if (score <= -99)
			return false;
		if (fabs(score - other.score) < 0.01)
			return future < other.future;
		return score > other.score;
	}
};

struct NavigationRequest {
public:
	NavigationRequest() { }

	Ship* ship;
	
	Vector2 targetLocation;
	bool avoid_enemies;
	bool avoid_obstacles = true;

	std::set<NavigationRequest*> eventHorizon;
};

class Navigation {
public:
	// hlt functiosn
	static bool segment_circle_intersect(const Vector2& start, const Vector2& end, const Vector2& circlePosition, const double circleRadius, const double fudge);
	static std::pair<bool, double> collision_time(long double r, const Vector2& loc1, const Vector2& loc2, const Vector2& vel1, const Vector2& vel2);

	static bool CheckEntityBetween(const Vector2& start, const Vector2& target, const Entity* entity_to_check);
	static bool AreObjectsBetween(const Vector2& start, const Vector2& target);
	static bool IsOutsideTheMap(const Vector2& location);

	static double GetPositionScore(const Vector2& position, const Vector2& targetLocation, bool avoiding_enemies);

	static std::vector<Move> NavigateShips(std::set<NavigationRequest*> navigationRequests);
private:
	Navigation();
};