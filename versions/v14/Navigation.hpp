#pragma once

#include <vector>

#include "constants.hpp"

#include "Types.hpp"
#include "Move.hpp"
#include "Vector2.hpp"

class Ship;

struct NavigationState {
public:
	NavigationState() { }
	NavigationState(double angle_rad, int thrust) : angle_rad(angle_rad), thrust(thrust) {
		velocity = Vector2::Velocity(angle_rad, thrust);
	}

	double angle_rad;
	int thrust = 0;
	Vector2 velocity;
};

struct NavigationRequest {
public:
	NavigationRequest() { }

	Ship* ship;

	int cluster_id = -1;

	Vector2 targetLocation;
	bool avoid_enemies;
	bool avoid_obstacles = true;

	Vector2 movementPoint;
	std::vector<NavigationRequest*> eventHorizon;

	NavigationState desiredMovement;

	NavigationState currentMovement;
	std::vector<NavigationRequest*> conflicts;

	void ComputeConflicts();
	bool HasConflicts();

	static bool Conflict(const NavigationRequest* navReqA, const NavigationRequest* navReqB);
};

class Navigation {
public:
	// hlt functiosn
	static bool segment_circle_intersect(const Vector2& start, const Vector2& end, const Vector2& circlePosition, const double circleRadius, const double fudge);
	static std::pair<bool, double> collision_time(long double r, const Vector2& loc1, const Vector2& loc2, const Vector2& vel1, const Vector2& vel2);

	static bool IsOutsideTheMap(const Vector2& location);

	static possibly<Vector2> GetNavigationTargetTowardsTarget(const Vector2& location, const Vector2& target);

	static void CalculateMovementPoints(std::vector<NavigationRequest*>& navigationRequests);
	static std::vector<Move> NavigateShips(std::vector<NavigationRequest*> navigationRequests);
private:
	Navigation();
};