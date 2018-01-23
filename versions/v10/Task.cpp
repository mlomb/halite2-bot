#include "Task.hpp"

#include <sstream>

Task::Task(unsigned int task_id)
	: task_id(task_id)
{
}

bool Task::IsFull() {
	return max_ships != -1 && ships.size() >= max_ships;
}

std::string Task::Info() {
	std::stringstream ss;
	ss << "type: ";
	switch (type) {
	case DOCK: ss << "DOCK"; break;
	case ATTACK: ss << "ATTACK"; break;
	case ESCAPE: ss << "ESCAPE"; break;
	}
	ss << " target: " << target;
	ss << " location: " << location;
	ss << " radius: " << radius;

	switch (type) {
	case DOCK:
		ss << " to_undock: " << to_undock;
		break;
	case ATTACK:
		ss << " indefense: " << indefense;
		break;
	}

	return ss.str();
}
