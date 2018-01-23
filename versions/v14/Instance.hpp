#pragma once

#include <set>
#include <unordered_map>
#include <chrono>

#include "Types.hpp"
#include "Move.hpp"
#include "Planet.hpp"
#include "Ship.hpp"
#include "Task.hpp"
#include "Map.hpp"

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
	
	// Useful functions
	int CountNearbyShips(Vector2 location, double radius, double range, bool friends);
	Ship* FindClosestShip(Vector2 location, bool indefense);

	// ms
	long long CurrentTurnTime();

	PlayerId player_id;
	unsigned int map_width, map_height;
	unsigned int turn;
	unsigned int num_players;

	std::chrono::time_point<std::chrono::high_resolution_clock> turn_start;

	std::unordered_map<EntityId, Ship*> ships;
	std::unordered_map<EntityId, Planet*> planets;

	std::unordered_map<PlayerId, int> shipsCount; // alive ships
	std::unordered_map<PlayerId, int> planetsCount; // planets owned

	Map* map;

	// Task System
	std::vector<Task*> tasks;
	unsigned int current_task = 0;

	bool rush_phase;
	bool game_over;
private:
	static Instance* s_Instance;
};