#pragma once

#include <string>
#include <set>

#include "Ship.hpp"
#include "Vector2.hpp"

enum TaskType {
	NOTHING,
	// attack a target
	ATTACK,
	// write
	WRITE,
	// escape to a target
	ESCAPE,
	// travelling to dock, docked or undocking to a specific planet
	DOCK
};

class Task {
public:
	Task(unsigned int task_id);

	bool IsFull();

	std::string Info();

	unsigned int task_id;
	std::set<Ship*> ships; // ships assigned to this task
	int max_ships = -1; // -1 = infinite

	/* Specific task related */
	TaskType type = TaskType::NOTHING;
	EntityId target = -1;
	Vector2 location;
	double radius = 0;

	// DOCK
	int to_undock = 0;

	// ATTACK
	bool indefense = false;
};