#pragma once

#include "Entity.hpp"
#include "Planet.hpp"

enum class ShipDockingStatus {
	Undocked = 0,
	Docking = 1,
	Docked = 2,
	Undocking = 3,
};

class Ship : public Entity {
public:
	Ship(EntityId id);

	bool IsCommandable() const;
	bool CanDockToAnyPlanet();
	bool CanDock(Planet* planet);
	int TurnsToBeUndocked();

	possibly<Move> ComputeMove();

	ShipDockingStatus docking_status;
	int docking_progress;
	EntityId docked_planet;

	int weapon_cooldown;

	// Task System
	unsigned int task_id = -1;
	double task_priority = 0;
};