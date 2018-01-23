#pragma once

#include "Entity.hpp"
#include "Planet.hpp"
#include "Navigation.hpp"

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
	void UpdateMaxThrusts();

	std::pair<possibly<Move>, possibly<NavigationRequest*>> ComputeAction();

	ShipDockingStatus docking_status;
	int docking_progress;
	EntityId docked_planet;

	int weapon_cooldown;

	// Task System
	unsigned int task_id = -1;
	double task_priority = 0;

	// Navigation
	bool frozen = false; // this turn, the ship will not move

	int max_thrusts[360];
	std::vector<Entity*> collisionEventHorizon;

	std::vector<NavigationOption> navigationOptions;
	int optionSelected = 0;

	// DEFEND task
	Ship* closest_defend_ship;
};