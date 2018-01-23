#include "Instance.hpp"

#include "constants.hpp"

#include "Input.hpp"
#include "Log.hpp"
#include "Navigation.hpp"
#include "Image.hpp"

#include <queue>
#include <string.h>
#include <algorithm>

Instance* Instance::s_Instance = nullptr;

Instance::Instance()
{
	s_Instance = this;
}

void Instance::Initialize(const std::string& bot_name)
{
	std::cout.sync_with_stdio(false);

	in::GetSString() >> player_id;
	in::GetSString() >> map_width >> map_height;

	Log::Get()->Open(std::to_string(player_id) + "_" + bot_name + ".log");

	Log::log() << "-- " << bot_name << " --" << std::endl;
	Log::log() << "Our player id: " << player_id << std::endl;
	Log::log() << "Map size: " << map_width << "x" << map_height << std::endl;

	turn = 0;
	NextTurn();

	Log::log() << "Players: " << num_players << std::endl
			   << "Planets: " << planets.size() << std::endl;

	std::cout << bot_name << std::endl;
}

void Instance::Play()
{
	rush_phase = num_players == 2;
	game_over = false;

	while (true) {
		NextTurn();

		auto start = std::chrono::high_resolution_clock::now();
		
		std::vector<Move> moves = Frame();
		
		auto finish = std::chrono::high_resolution_clock::now();
		auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);
		Log::log("Turn took: " + std::to_string(millis.count()) + "ms");

		// generate moves string
		std::ostringstream oss;
		for (const Move& move : moves) {
			switch (move.type) {
			case MoveType::Noop:
				continue;
			case MoveType::Undock:
				oss << "u " << move.ship_id << " ";
				break;
			case MoveType::Dock:
				oss << "d " << move.ship_id << " "
					<< move.dock_to << " ";
				break;
			case MoveType::Thrust:
				oss << "t " << move.ship_id << " "
					<< move.move_thrust << " "
					<< move.move_angle_deg << " ";
				break;
			}
		}
		std::cout << oss.str() << std::endl;

		if (!std::cout.good()) {
			Log::log("Error sending movements, aborting");
			std::exit(0);
		}
	}
}

void Instance::NextTurn()
{
	const std::string input = in::GetString();

	if (!std::cin.good()) {
		// This is needed on Windows to detect that game engine is done.
		std::exit(0);
	}

	if (turn == 0)
		Log::log("--- PRE-GAME ---");
	else
		Log::log() << "--- TURN " << turn << " ---" << std::endl;
	turn_start = std::chrono::high_resolution_clock::now();

	// process the map
	ParseMap(input);

	++turn;
}

void Instance::ParseMap(const std::string& input) {
	std::stringstream iss(input);

	iss >> num_players;

	EntityId entity_id;

	// mark all the entities as dead
	for (auto& kv : ships) { kv.second->alive = false; kv.second->task_id = -1; kv.second->task_priority = 0; kv.second->frozen = true; }
	for (auto& kv : planets) { kv.second->alive = false; }

	shipsCount.clear();
	planetsCount.clear();

	for (int i = 0; i < num_players; ++i) {
		PlayerId player_id;
		unsigned int num_ships;

		iss >> player_id >> num_ships;
		shipsCount.insert(std::make_pair(player_id, num_ships));
		planetsCount.insert(std::make_pair(player_id, 0));

		for (int j = 0; j < num_ships; j++) {
			iss >> entity_id;

			Ship* ship = GetShip(entity_id);
			if (ship == nullptr) {
				ship = new Ship(entity_id);
				ships.insert(std::make_pair(entity_id, ship));
			}

			ship->alive = true;
			iss >> ship->location.x;
			iss >> ship->location.y;
			iss >> ship->health;

			double vel_x_deprecated, vel_y_deprecated;
			iss >> vel_x_deprecated >> vel_y_deprecated;

			int docking_status;
			iss >> docking_status;
			ship->docking_status = static_cast<ShipDockingStatus>(docking_status);

			iss >> ship->docked_planet;
			iss >> ship->docking_progress;
			iss >> ship->weapon_cooldown;

			ship->owner_id = player_id;
			ship->radius = hlt::constants::SHIP_RADIUS;
		}
	}

	unsigned int num_planets;
	iss >> num_planets;

	for (unsigned int i = 0; i < num_planets; ++i) {
		iss >> entity_id;

		Planet* planet = GetPlanet(entity_id);
		if (planet == nullptr) {
			planet = new Planet(entity_id);
			planets.insert(std::make_pair(entity_id, planet));
		}

		planet->alive = true;
		iss >> planet->location.x;
		iss >> planet->location.y;
		iss >> planet->health;
		iss >> planet->radius;
		iss >> planet->docking_spots;
		iss >> planet->current_production;
		iss >> planet->remaining_production;

		int owned;
		iss >> owned;
		iss >> planet->owner_id;

		if (owned != 1)
			planet->owner_id = -1;
		else {
			int& ownedPlanets = planetsCount.at(planet->owner_id);
			ownedPlanets++;
		}

		unsigned int num_docked_ships;
		iss >> num_docked_ships;

		planet->docked_ships.reserve(num_docked_ships);
		for (unsigned int i = 0; i < num_docked_ships; ++i) {
			iss >> entity_id;
			planet->docked_ships.push_back(GetShip(entity_id));
		}
	}

	Log::log() << "Map parsed -- ships: " << ships.size() << " planets: " << planets.size() << std::endl;
}

Instance* Instance::Get()
{
	return s_Instance;
}

Ship* Instance::GetShip(EntityId shipId) {
	auto it = ships.find(shipId);
	if (it != ships.end())
		return (*it).second;
	return nullptr;
}

Planet* Instance::GetPlanet(EntityId planetId) {
	auto it = planets.find(planetId);
	if (it != planets.end())
		return (*it).second;
	return nullptr;
}

Task* Instance::CreateTask(TaskType type)
{
	Task* task = new Task(current_task++);
	task->type = type;
	tasks.push_back(task);
	return task;
}

Task* Instance::GetTask(unsigned int task_id)
{
	return tasks.at(task_id);
}

void Instance::UpdateMap()
{
	if (CurrentTurnTime() > 1800) {
		Log::log("We'll not recalculate the map because we're running out of time");
		return;
	}

	// clear the map
	memset(&map[0][0], 0, MAP_HEIGHT * MAP_WIDTH * sizeof(MapCell));
	for (int i = 0; i < MAP_HEIGHT * MAP_WIDTH; i++) {
		int y = (i / MAP_WIDTH);
		int x = (i % MAP_WIDTH);
		if (x < 0 || y < 0 || x >= MAP_WIDTH || y >= MAP_HEIGHT)
			continue;
		MapCell& cell = map[y][x];
		cell.location = { (double)x / MAP_DEFINITION, (double)y / MAP_DEFINITION };
	}

	// mark the planets as solids
	for (auto&kv : planets) {
		Planet* planet = kv.second;
		if (!planet->alive) continue;

		IterateMap(planet->location, planet->radius + hlt::constants::SHIP_RADIUS, [&](Vector2 position, MapCell& cell, double distance) {
			cell.solid = true;
		});
	}

	// mark ships
	for (auto&kv : ships) {
		Ship* ship = kv.second;
		if (!ship->alive) continue;

		double radius;

		if (ship->IsOur()) {
			if (!ship->IsCommandable()) continue; // we can't control this ship
			radius = ship->radius * 2 + hlt::constants::MAX_SPEED;
		}
		else {
			if (ship->IsCommandable()) 
				radius = ship->radius * 2 + hlt::constants::MAX_SPEED + hlt::constants::WEAPON_RADIUS + 1;
			else
				radius = ship->radius * 2 + hlt::constants::WEAPON_RADIUS;
		}

		IterateMap(ship->location, radius, [&](Vector2 position, MapCell& cell, double distance) {
			if (distance < ship->radius)
				cell.ship = true;

			if (ship->IsOur()) {
				if (ship->frozen) {
					if(distance < ship->radius * 2 + hlt::constants::WEAPON_RADIUS)
						cell.nextTurnFriendlyShipsTakingDamage++;
				}
				else {
					cell.nextTurnFriendlyShipsTakingDamage++;
					if (ship->IsCommandable())
						cell.nextTurnFriendlyShipsAttackInRange++;
				}
			}
			else {
				if (ship->frozen) {
					if (distance < ship->radius * 2 + hlt::constants::WEAPON_RADIUS)
						cell.nextTurnEnemyShipsTakingDamage++;
				}
				else {
					cell.nextTurnEnemyShipsTakingDamage++;
					if (ship->IsCommandable())
						cell.nextTurnEnemyShipsAttackInRange++;
				}
			}
		});
	}

	/*
	Image::WriteImage(std::string("turns/turn_") + std::to_string(turn) + "_map.bmp", MAP_WIDTH, MAP_HEIGHT, [&](int x, int y) -> std::tuple<unsigned char, unsigned char, unsigned char> {
		y = MAP_HEIGHT - y - 1;

		if (map[y][x].ship)
			return std::make_tuple(0, 0, 255);

		if (map[y][x].solid)
			return std::make_tuple(0, 0, 0);

		int d1 = 200 - map[y][x].nextTurnEnemyShipsAttackInRange * 15;
		auto enemyColor = std::make_tuple(255, d1, d1);
		int d2 = 200 - map[y][x].friendlyShipsThatCanReachThere * 15;
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

MapCell& Instance::GetCell(const Vector2& location)
{
	int x = location.x * MAP_DEFINITION;
	int y = location.y * MAP_DEFINITION;
	if (x < 0 || y < 0 || x >= MAP_WIDTH || y >= MAP_HEIGHT)
		return map[0][0]; // strange
	return map[y][x];
}

void Instance::IterateMap(Vector2 location, double radius, std::function<void(Vector2, MapCell&, double)> action) {
	if (!action) return;

	Vector2 startPoint = location - radius;
	Vector2 endPoint = location + radius;

	const double borderSeparation = 1;

	startPoint.x = std::fmin(std::fmax(startPoint.x, borderSeparation), map_width - borderSeparation);
	startPoint.y = std::fmin(std::fmax(startPoint.y, borderSeparation), map_height - borderSeparation);

	endPoint.x = std::fmin(std::fmax(endPoint.x, borderSeparation), map_width - borderSeparation);
	endPoint.y = std::fmin(std::fmax(endPoint.y, borderSeparation), map_height - borderSeparation);

	const double step = 1.0 / MAP_DEFINITION;

	for (double ix = startPoint.x; ix < endPoint.x; ix += step) {
		for (double iy = startPoint.y; iy < endPoint.y; iy += step) {
			Vector2 position = { (double)ix, (double)iy };
			double d = location.DistanceTo(position);
			if (d < radius) {
				action(position, GetCell(position), d);
			}
		}
	}
}

std::vector<Move> Instance::Frame()
{
	shipTrajectories.clear();

	if (num_players == 4) {
		rush_phase = false;

		// check for a game over
		if (!game_over) {
			// we are not in game over yet
			int myShips = shipsCount.at(player_id);
			int players = 1;
			int maxEnemyShipsFromOnePlayer = 0;
			for (auto& kv : shipsCount) {
				if (kv.first == player_id) continue;
				if (kv.second > 0) {
					int planets_owned = 0;
					for (auto& kv : planets) {
						if (kv.second->owner_id == kv.first)
							planets_owned++;
					}
					if (planets_owned > 0) {
						players++;
						maxEnemyShipsFromOnePlayer = std::max(maxEnemyShipsFromOnePlayer, kv.second);
					}
				}
			}

			if (players >= 4) { // we only give up if we need to fight the 3rd place
				if (maxEnemyShipsFromOnePlayer > myShips + (maxEnemyShipsFromOnePlayer * 0.5 /* 50% of the ships */))
					game_over = true;
			}
		}
	}
	else { // num_player == 2
		if (rush_phase) { // we check if the rush phase should end
			Log::log("We're in rush phase!");

			bool threatsFound = false;

			for (auto& kv : ships) {
				if (!kv.second->alive) continue;
				if (kv.second->owner_id == player_id) continue;

				if (kv.second->docking_status == ShipDockingStatus::Undocked) {
					threatsFound = true;
					break;
				}
			}

			if (!threatsFound) {
				if (shipsCount.at(player_id) > 6 || turn > 30) {
					// just end the rushing detection phase if we generated at least 6 ships
					Log::log("Ending the rush phase because we have at least 6 ships or turn > 30");
					threatsFound = false;
				}
			}

			rush_phase = threatsFound;
			Log::log() << "Rushing detection: " << (rush_phase ? "Continues" : "Ended") << std::endl;
		}
	}

	// Generate all the tasks
	GenerateTasks();

	// detect rushers

	// Organize ships; assign tasks
	AssignTasks();

	// Execute the tasks assigned by priority
	std::vector<Ship*> requireComputeShips;
	for (auto&kv : ships) {
		Ship* ship = kv.second;
		if (!ship->alive) continue;
		if (!ship->IsOur()) continue;

		requireComputeShips.push_back(ship);
	}

	// sort by priority
	std::sort(requireComputeShips.begin(), requireComputeShips.end(), [](const Ship* shipPtrA, const Ship* shipPtrB) {
		return shipPtrA->task_priority < shipPtrB->task_priority;
	});

	std::vector<Move> moves;
	std::vector<NavigationRequest*> navigationRequests;

	for (Ship* ship : requireComputeShips) {
		auto action = ship->ComputeAction();
		if (action.second.second) {
			ship->frozen = false;
			navigationRequests.push_back(action.second.first);
		}
		else {
			if (action.first.second) {
				moves.push_back(action.first.first);
			}
		}
	}

	//UpdateMap();
	
	std::vector<Move> navMoves = Navigation::NavigateShips(navigationRequests);

	for (NavigationRequest* navReq : navigationRequests)
		delete navReq;
	navigationRequests.clear();

	Log::log() << "Navigation requests: " << navigationRequests.size() << " Navigation moves: " << navMoves.size() << std::endl;

	for (Move navMove : navMoves) {
		// Add a message in the angle
		//navMove.move_angle_deg += ((GetTask(GetShip(navMove.ship_id)->task_id)->target + 1) * 360);
		moves.push_back(navMove);
	}

	return moves;
}

void Instance::GenerateTasks()
{
	// Clear old tasks
	current_task = 0;
	for (Task* task : tasks)
		delete task;
	tasks.clear();

	// Generate new tasks
	for (auto& kv : planets) {
		Planet* planet = kv.second;
		if (!planet->alive) continue; // if the planet was destroyed 'it doesnt exist'

		if (planet->owner_id == -1 || planet->IsOur()) { // the planet is not owned or it's owned by us
			// Task DOCK
			Task* taskDock = CreateTask(TaskType::DOCK);
			taskDock->target = planet->entity_id;
			taskDock->location = planet->location;
			taskDock->radius = planet->radius;
			taskDock->max_ships = game_over ? 0 : planet->docking_spots;
		}
	}

	if (!game_over) {
		// Ship tasks
		for (auto& kv : ships) {
			Ship* ship = kv.second;
			if (!ship->alive) continue;
			if (ship->IsOur()) continue;
			// every enemy ship alive

			if (num_players == 4 && turn > 50 && planetsCount.at(player_id) > 0 && planetsCount.at(ship->owner_id) == 0) {
				// we don't care about this player
				continue;
			}

			Task* taskAttack = CreateTask(TaskType::ATTACK);
			taskAttack->target = ship->entity_id;
			taskAttack->location = ship->location;
			taskAttack->radius = 0;
			taskAttack->indefense = ship->docking_status != ShipDockingStatus::Undocked;
			taskAttack->max_ships = 6;// TODO See
		}
	}
	else { // game_over
		// we've lost
		// try to get 3rd place
		for (int i = 0; i < 4; i++) {
			Task* taskEscape = CreateTask(TaskType::ESCAPE);
			taskEscape->target = -1;
			taskEscape->location = { i % 2 == 0 ? 0 : (double)map_width, (int)(i / 2.0) == 0 ? 0 : (double)map_height };
			taskEscape->radius = 0;
			taskEscape->max_ships = -1; // All ships must escape
		}
	}

	Log::log() << "Generated " << tasks.size() << " tasks" << std::endl;
}

void Instance::AssignTasks()
{
	std::queue<Ship*> qShips;
	std::set<Task*> dockTasksWithShips;

	// non undocked ships have a fixed task
	for (auto& kv : ships) {
		Ship* ship = kv.second;

		if (!ship->alive) continue;
		if (!ship->IsOur()) continue;

		if (!ship->IsCommandable()) {
			// ship is docking, docked or undocking

			Task* dockTask = 0;

			// we're docked to a planet, find the task that match
			for (Task* task : tasks) {
				if (task->type == TaskType::DOCK && task->target == ship->docked_planet) {
					dockTask = task;
					break;
				}
			}

			if (dockTask == 0) {
				Log::log("Dock task for ship " + std::to_string(ship->entity_id) + " not found.");
				qShips.push(ship);
			}
			else {
				std::string status_name = "UNKNOWN";
				switch (ship->docking_status)
				{
				case ShipDockingStatus::Docking: status_name = "DOCKING"; break;
				case ShipDockingStatus::Docked: status_name = "DOCKED"; break;
				case ShipDockingStatus::Undocking: status_name = "UNDOCKING"; break;
				}

				Log::log("Ship " + std::to_string(ship->entity_id) + " continues " + status_name + " to planet " + std::to_string(dockTask->target) + " -- docking progress: " + std::to_string(ship->docking_progress));

				ship->task_id = dockTask->task_id;
				ship->task_priority = INF; // docked ships cant be relevated
				dockTask->ships.insert(ship);

				dockTasksWithShips.insert(dockTask);
			}
		} else
			qShips.push(ship);
	}

	// for the rest of the ships (which are now on qShips)
	while (!qShips.empty()) {
		Ship* ship = qShips.front();
		qShips.pop();

		Task* priorizedTask = 0;
		double maxPriority = -INF;
		Ship* otherShipPtrOverriding = 0;

		for (Task* task : tasks) {
			if (task->max_ships == 0) continue;

			// this is a priority relative to the ship, aka how important this task is for this ship
			Vector2 target = task->location;
			if (task->radius != 0)
				target = ship->location.ClosestPointTo(task->location, task->radius);
			double distance = ship->location.DistanceTo(task->location);

			/* PRIORITY CALCULATION */
			double d = distance;
			switch (task->type)
			{
			case DOCK:
				d += 8;
				break;
			case ATTACK:
				if (task->indefense) {
					d -= 9;

					double arriveDist = ship->location.DistanceTo(target);
					int enemiesWhenArrive = CountNearbyShips(target, ship->radius, arriveDist, false);
					int friendsWhenArrive = CountNearbyShips(target, ship->radius, arriveDist, true);
					if (friendsWhenArrive >= enemiesWhenArrive) {
						d -= 13;
					}
				}
				// if we are in early game, we even priorize this more
				/*
				if (!rush_phase && turn < 30) {
					d -= 35;
				}
				*/
				break;
			}

			double distancePriority = 100 - d / 100;
			double priority = distancePriority;
			/* PRIORITY CALCULATION */

			Ship* otherShipPtrWithLessPriority = 0;
			if (task->IsFull()) { // task is full, no other ship can help
								  // but we check if we have a higher priority than one of the ships already assigned to this task
				double otherShipPtrMinPriority = INF; // less is better
				for (Ship* otherShipPtr : task->ships) {
					double otherShipPtrPriority = otherShipPtr->task_priority;

					if (priority > otherShipPtrPriority) {
						if (otherShipPtrPriority < otherShipPtrMinPriority) {
							otherShipPtrWithLessPriority = otherShipPtr;
							otherShipPtrMinPriority = otherShipPtrPriority;
						}
					}
				}
				if (!otherShipPtrWithLessPriority)
					continue;
			}

			if (priority > maxPriority) {
				maxPriority = priority;
				priorizedTask = task;
				otherShipPtrOverriding = otherShipPtrWithLessPriority;
			}
		}

		if (priorizedTask == 0) {
			Log::log("Ship " + std::to_string(ship->entity_id) + " couldn't find a suitable task!");
		}
		else {
			Log::log("Ship " + std::to_string(ship->entity_id) + " assigned to task " + std::to_string(priorizedTask->task_id) + " (" + priorizedTask->Info() + ") with priority " + std::to_string(maxPriority));

			if (otherShipPtrOverriding) {
				Log::log("... while overriding ship " + std::to_string(otherShipPtrOverriding->entity_id) + " in task " + std::to_string(otherShipPtrOverriding->task_id));
				priorizedTask->ships.erase(otherShipPtrOverriding);
				qShips.push(otherShipPtrOverriding);
				otherShipPtrOverriding->task_id = -1;
				otherShipPtrOverriding->task_priority = 0;
			}

			ship->task_id = priorizedTask->task_id;
			ship->task_priority = maxPriority;
			priorizedTask->ships.insert(ship);
		}
	}

	Log::log() << "Tasks have been assigned" << std::endl;
}

int Instance::CountNearbyShips(Vector2 location, double radius, double range, bool friends)
{
	int count = 0;

	for (auto& kv : ships) {
		Ship* ship = kv.second;
		if (!ship->alive) continue;
		if (ship->IsOur() == friends) {
			if (ship->IsCommandable()) {
				if (ship->location.DistanceTo(location) - radius < range) {
					count++;
				}
			}
		}
	}

	return count;
}

Ship* Instance::FindClosestShip(Vector2 location, bool indefense)
{
	double minDistance = INF;
	Ship* result;

	for (auto& kv : ships) {
		Ship* ship = kv.second;
		if (!ship->alive) continue;
		if (ship->IsOur()) continue;

		if (!ship->IsCommandable() == indefense) {
			double distance = ship->location.DistanceTo(location);
			if (distance < minDistance) {
				minDistance = distance;
				result = ship;
			}
		}
	}

	return result;
}

long long Instance::CurrentTurnTime()
{
	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = finish - turn_start;
	return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}
