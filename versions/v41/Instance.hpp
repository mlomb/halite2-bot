#pragma once

#include <set>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>

#include "Types.hpp"
#include "Move.hpp"
#include "Planet.hpp"
#include "Ship.hpp"
#include "Task.hpp"
#include "Map.hpp"
#include "Log.hpp"

class Stopwatch {
public:
	Stopwatch(const std::string& identifier) : identifier(identifier) {
#if HALITE_LOCAL
		start = std::chrono::high_resolution_clock::now();
#endif
	};

	~Stopwatch() {
#if HALITE_LOCAL
		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = end - start;
		long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

		messages.push_back(identifier + ": " + std::to_string(ms) + "ms");
#endif
	}

	std::string identifier;
	std::chrono::time_point<std::chrono::high_resolution_clock> start;

	static std::vector<std::string> messages;

	static void FlushMessages() {
#if HALITE_LOCAL
		for (std::string s : messages) {
			Log::log(s);
		}
		messages.clear();
#endif
	}
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
	
	// Useful functions
	int CountNearbyShips(Vector2 location, double radius, double range, bool friends);
	Ship* GetClosestShip(Vector2 location, bool friends);
	std::vector<Entity*> GetEntitiesInside(const Entity* entity, const double range);

	// ms
	long long CurrentTurnTime();

	PlayerId player_id;
	unsigned int map_width, map_height;
	unsigned int turn;
	unsigned int num_players;

	std::chrono::time_point<std::chrono::high_resolution_clock> turn_start;

	std::unordered_map<EntityId, Ship*> ships;
	std::unordered_map<EntityId, Planet*> planets;
	std::vector<Ship*> myShips;

	std::unordered_map<PlayerId, int> shipsCount; // alive ships
	std::unordered_map<PlayerId, int> planetsCount; // planets owned

	Map* map;

	// Cache
	Vector2 velocityCache[360][hlt::constants::MAX_SPEED * 2];

	// Task System
	std::vector<Task*> tasks;
	unsigned int current_task = 0;

	bool rush_phase;
	bool writing;
	int turns_writing;
	bool game_over;

	// Message
	Vector2 messageOffset;
private:
	static Instance* s_Instance;
};