#pragma once

#include <vector>

#include "Entity.hpp"
#include "Move.hpp"

class Ship;

class Planet : public Entity {
public:
	Planet(EntityId id);

	int remaining_production;
	int current_production;
	unsigned int docking_spots;
	std::vector<Ship*> docked_ships;
};