#pragma once

#include <set>
#include <unordered_map>
#include <functional>

#include "Types.hpp"
#include "Move.hpp"
#include "Planet.hpp"
#include "Ship.hpp"
#include "Task.hpp"

#define MAP_MAX_WIDTH 384
#define MAP_MAX_HEIGHT 256
#define MAP_DEFINITION 5
#define MAP_WIDTH MAP_MAX_WIDTH * MAP_DEFINITION
#define MAP_HEIGHT MAP_MAX_HEIGHT * MAP_DEFINITION

struct MapCell {
	Vector2 location;

	bool ship = false;
	bool solid = false;

	int nextTurnEnemyShipsAttackInRange = 0; // non indefense ships in range
	int nextTurnEnemyShipsTakingDamage = 0; // indefense and non indefense ships
	int friendlyShipsThatCanReachThere = 0; // firendly ships within range
};

/* A Halite match instance */
class Instance {
public:
	Instance();

	static Instance* Get();

	void Initialize(const std::string& bot_name);
	void Play();
	void NextTurn();
	void ParseMap(const std::string& input);

	std::vector<Move> Frame();

	Ship* GetShip(EntityId shipId);
	Planet* GetPlanet(EntityId planetId);

	Task* CreateTask(TaskType type = TaskType::NOTHING);
	Task* GetTask(unsigned int task_id);

	void GenerateTasks();
	void AssignTasks();

	void UpdateMap();
	MapCell& GetCell(const Vector2& location);
	void IterateMap(Vector2 location, double radius, std::function<void(Vector2, MapCell&, double)> action);

	// Useful functions
	int CountNearbyShips(Vector2 location, double radius, double range, bool friends);
	Ship* FindClosestShip(Vector2 location, bool indefense);



	PlayerId player_id;
	unsigned int map_width, map_height;
	unsigned int turn;
	unsigned int num_players;

	std::unordered_map<EntityId, Ship*> ships;
	std::unordered_map<EntityId, Planet*> planets;

	std::unordered_map<PlayerId, int> shipsCount; // alive ships
	std::unordered_map<PlayerId, int> planetsCount; // planets owned

	// Navigation
	std::vector<std::pair<Vector2, Vector2>> shipTrajectories;

	// Task System
	std::vector<Task*> tasks;
	unsigned int current_task = 0;

	// -
	MapCell map[MAP_HEIGHT][MAP_WIDTH];

	bool rush_phase;
	bool game_over;
private:
	static Instance* s_Instance;
};