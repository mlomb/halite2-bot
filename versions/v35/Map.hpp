#pragma once

#include <functional>

#include "Vector2.hpp"
#include "Ship.hpp"

#define MAP_MAX_WIDTH 384
#define MAP_MAX_HEIGHT 256
#define MAP_DEFINITION 4
#define MAP_WIDTH MAP_MAX_WIDTH * MAP_DEFINITION
#define MAP_HEIGHT MAP_MAX_HEIGHT * MAP_DEFINITION

struct MapCell {
	bool ship = false;
	bool solid = false;

	double nextTurnEnemyShipsTakingDamage = 0;
	double nextTurnFriendlyShipsTakingDamage = 0;
	
	double nextTurnEnemyShipsAttackInRange = 0;
	double nextTurnFriendlyShipsAttackInRange = 0;
};

/* The navigation map */
class Map {
public:
	Map();

	void ClearMap();
	void FillMap();
	void ModifyShip(Ship* ship, int direction = 1);

	MapCell& GetCell(const Vector2& location);
	void IterateMap(Vector2 location, double radius, std::function<void(Vector2, MapCell&, double)> action);


	// -
	MapCell cells[MAP_HEIGHT][MAP_WIDTH];
};