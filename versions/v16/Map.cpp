#include "Map.hpp"

#include <string.h>

#include "Instance.hpp"
#include "Image.hpp"

Map::Map()
{
}

void Map::ClearMap() {
	memset(&cells[0][0], 0, MAP_HEIGHT * MAP_WIDTH * sizeof(MapCell));
}

void Map::FillMap()
{
	Instance* instance = Instance::Get();
	
	// mark the planets as solids
	for (auto&kv : instance->planets) {
		Planet* planet = kv.second;

		IterateMap(planet->location, planet->radius + hlt::constants::SHIP_RADIUS, [&](Vector2 position, MapCell& cell, double distance) {
			cell.solid = true;
		});
	}

	// mark ships
	for (auto&kv : instance->ships) {
		Ship* ship = kv.second;
		ModifyShip(ship);
	}

	/*
	Image::WriteImage(std::string("turns/turn_") + std::to_string(instance->turn) + "_map.bmp", MAP_WIDTH, MAP_HEIGHT, [&](int x, int y) -> std::tuple<unsigned char, unsigned char, unsigned char> {
		y = MAP_HEIGHT - y - 1;

		if (cells[y][x].ship)
			return std::make_tuple(0, 0, 255);

		if (cells[y][x].solid)
			return std::make_tuple(0, 0, 0);

		int d1 = 200 - cells[y][x].nextTurnEnemyShipsAttackInRange * 15;
		auto enemyColor = std::make_tuple(255, d1, d1);
		int d2 = 200 - cells[y][x].nextTurnFriendlyShipsAttackInRange * 15;
		auto friendColor = std::make_tuple(d2, 255, d2);

		if (d1 == 200 && d2 == 200)
			return std::make_tuple(255, 255, 255);
		else if (d1 != 200 && d2 == 200)
			return enemyColor;
		else if (d1 == 200 && d2 != 200)
			return friendColor;
		else
			return Image::Interpolate(enemyColor, friendColor);
	});
	*/
}

void Map::ModifyShip(Ship* ship, int direction)
{
	Instance* instance = Instance::Get();

	bool is_docking = false;
	double radius;

	if (ship->IsOur()) {
		radius = ship->radius * 2 + hlt::constants::MAX_SPEED;

		if (ship->task_id != -1) {
			is_docking = instance->GetTask(ship->task_id)->type == TaskType::DOCK;
		}
	}
	else {
		if (ship->IsCommandable())
			radius = hlt::constants::SHIP_RADIUS * 2 + hlt::constants::MAX_SPEED + hlt::constants::WEAPON_RADIUS + 1;
		else
			radius = hlt::constants::SHIP_RADIUS * 2 + hlt::constants::WEAPON_RADIUS;
	}

	IterateMap(ship->location, radius, [ship, direction, is_docking](Vector2 position, MapCell& cell, double distance) {
		if (distance < hlt::constants::SHIP_RADIUS)
			cell.ship = true;

		if (ship->IsOur()) {
			if (ship->frozen || is_docking) {
				if (distance < hlt::constants::SHIP_RADIUS * 2 + hlt::constants::WEAPON_RADIUS)
					cell.nextTurnFriendlyShipsTakingDamage += direction;
			}
			else {
				cell.nextTurnFriendlyShipsTakingDamage += direction;
				if (ship->IsCommandable())
					cell.nextTurnFriendlyShipsAttackInRange += direction;
			}
		}
		else {
			cell.nextTurnEnemyShipsTakingDamage += direction;
			if (ship->IsCommandable())
				cell.nextTurnEnemyShipsAttackInRange += direction;
		}
	});
}

MapCell& Map::GetCell(const Vector2& location)
{
	int x = location.x * MAP_DEFINITION;
	int y = location.y * MAP_DEFINITION;
	return cells[y][x];
}

void Map::IterateMap(Vector2 location, double radius, std::function<void(Vector2, MapCell&, double)> action) {
	if (!action) return;

	Instance* instance = Instance::Get();

	Vector2 startPoint = location - radius;
	Vector2 endPoint = location + radius;

	const double borderSeparation = 1;

	startPoint.x = std::fmin(std::fmax(startPoint.x, borderSeparation), instance->map_width - borderSeparation);
	startPoint.y = std::fmin(std::fmax(startPoint.y, borderSeparation), instance->map_height - borderSeparation);

	endPoint.x = std::fmin(std::fmax(endPoint.x, borderSeparation), instance->map_width - borderSeparation);
	endPoint.y = std::fmin(std::fmax(endPoint.y, borderSeparation), instance->map_height - borderSeparation);

	const double step = 1.0 / MAP_DEFINITION;

	for (double ix = startPoint.x; ix <= endPoint.x; ix += step) {
		for (double iy = startPoint.y; iy <= endPoint.y; iy += step) {
			Vector2 position = { (double)ix, (double)iy };
			double d = location.DistanceTo(position);
			if (d < radius) {
				action(position, GetCell(position), d);
			}
		}
	}
}
