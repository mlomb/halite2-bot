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
std::vector<std::string> Stopwatch::messages;

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

	// -- Init 60 seconds --

	map = new Map();
	map->FillMap(); // to calculate the message offset

	// MessageOffset calculation
	{
		Stopwatch s("MessageOffset calculation");

		const Vector2 messageSize = { 140, 22 }; // aprox, calculated in Chlorine
		const Vector2 center = { map_width / 2.0, map_height / 2.0 };
		const double borderSeparation = 8; // to avoid annoy the players praying for the 3rd place

		// this is very slow, maybe this can be done with DP, but we have 60s of init time that I'm not using... and.. of course, I'm lazy af
		messageOffset = { -1,-1 };
		double minDist = INF;

		for (int x = borderSeparation; x < map_width - messageSize.x - borderSeparation; x++) {
			for (int y = borderSeparation; y < map_height - messageSize.y - borderSeparation; y++) {
				bool blocked = false;
				for (int xx = 0; !blocked && xx < messageSize.x; xx++) {
					for (int yy = 0; !blocked && yy < messageSize.y; yy++) {
						if (map->cells[(y + yy) * MAP_DEFINITION][(x + xx) * MAP_DEFINITION].solid)
							blocked = true;
					}
				}
				if (!blocked) {
					const Vector2 offset = { (double)x, (double)y };
					const double dist = (offset + messageSize / 2.0).DistanceTo(center);
					if (messageOffset.x == -1 || dist < minDist) {
						messageOffset = offset;
						minDist = dist;
					}
				}
			}
		}

		// if we couldnt find a good spot for the message, we just default the position
		if (messageOffset.x == -1) {
			double middlePlanetRadius = 8;
			for (int i = 0; i < 4; i++) { // try to get the radius from the planets
				Planet* planet = GetPlanet(i);
				if (planet) {
					middlePlanetRadius = planet->radius;
					break;
				}
			}
			messageOffset = { map_width / 2.0 - messageSize.x / 2.0, map_height / 2.0 + middlePlanetRadius * 2 + 7 };
		}
	}
}

void Instance::Play()
{
	rush_phase = num_players == 2;
	game_over = false;
	writing = false;

	while (true) {
		NextTurn();

		std::vector<Move> moves;
		{
			Stopwatch s("Turn took");
			moves = Frame();
		}

		Stopwatch::FlushMessages();

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
	for (auto& kv : ships) { kv.second->alive = false; }
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
			ship->frozen = true;
			ship->task_id = -1;
			ship->task_priority = -INF;
			ship->closest_defend_ship = 0;
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

		planet->docked_ships.clear();
		planet->docked_ships.reserve(num_docked_ships);
		for (unsigned int i = 0; i < num_docked_ships; ++i) {
			iss >> entity_id;
			planet->docked_ships.push_back(GetShip(entity_id));
		}
	}

	// Remove dead ships and planets
	for (auto it = ships.begin(); it != ships.end();)
	{
		Ship* ship = (*it).second;
		if (!ship->alive)
		{
			delete ship;
			it = ships.erase(it);
		}
		else
			++it;
	}

	for (auto it = planets.begin(); it != planets.end();)
	{
		Planet* planet = (*it).second;
		if (!planet->alive)
		{
			delete planet;
			it = planets.erase(it);
		}
		else
			++it;
	}

	// Update myShips
	myShips.clear();
	for (auto& kv : ships) {
		Ship* ship = kv.second;
		if (ship->IsOur())
			myShips.push_back(ship);
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

std::vector<Move> Instance::Frame()
{
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
					if (planetsCount.at(kv.first) > 0) {
						players++;
						maxEnemyShipsFromOnePlayer = std::max(maxEnemyShipsFromOnePlayer, kv.second);
					}
				}
			}

			if (myShips < 25 && players >= 4) { // we only give up if we need to fight the 3rd place
				if (maxEnemyShipsFromOnePlayer > myShips + (maxEnemyShipsFromOnePlayer * 0.6 /* 60% of the ships */))
					game_over = true;
			}
		}
	}
	if (rush_phase) { // we check if the rush phase should end
		Log::log("We're in rush phase!");

		bool threatsFound = false;

		for (auto& kv : ships) {
			if (kv.second->IsOur()) continue;

			if (kv.second->docking_status == ShipDockingStatus::Undocked) {
				threatsFound = true;
				break;
			}
		}

		if (threatsFound) {
			if ((planetsCount.at(player_id) >= 1 && shipsCount.at(player_id) >= 4)) {
				// just end the rushing detection phase if we generated at least 4 ships with a planet
				Log::log("Ending the rush phase because we have at least 4 ships");
				threatsFound = false;
			}
		}

		rush_phase = threatsFound;
		Log::log() << "Rushing detection: " << (rush_phase ? "Continues" : "Ended") << std::endl;
	}
	if (!writing) {
		if (shipsCount.at(player_id) > 90) { // we should have at least 90 ships to write
			writing = (double)planetsCount.at(player_id) / (double)planets.size() > 0.85 || planetsCount.at(player_id) >= planets.size() - 1;
			turns_writing = 0;
		}
	}
	else
		turns_writing++;

	// Generate all the tasks
	GenerateTasks();

	// detect rushers

	// Organize ships; assign tasks
	AssignTasks();

	// Execute the tasks assigned by priority
	std::sort(myShips.begin(), myShips.end(), [](const Ship* shipPtrA, const Ship* shipPtrB) {
		return shipPtrA->task_priority < shipPtrB->task_priority;
	});

	std::vector<Move> moves;
	std::vector<NavigationRequest*> navigationRequests;

	{
		Stopwatch s("Compute " + std::to_string(myShips.size()) + " actions");
		for (Ship* ship : myShips) {
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
	}

	if (navigationRequests.size() > 300) { // thats too much
		// sort the nav requests by importance
		std::sort(navigationRequests.begin(), navigationRequests.end(), [&](const NavigationRequest* a, const NavigationRequest* b) {
			const Task* taskA = GetTask(a->ship->task_id);
			const Task* taskB = GetTask(b->ship->task_id);

			if (taskA && !taskB) return true; // a
			else if (!taskA && taskB) return false; // b
			else if (!taskA && !taskB) return true; // anywho
			else {
				if (taskA->type == taskB->type)
					return a->ship->task_priority > b->ship->task_priority;
				else
					return taskA->type > taskB->type;
			}
		});
	}
	std::set<NavigationRequest*> navigationRequestsSet;
	for (int i = 0; i < std::min(300, (int)navigationRequests.size()); i++) {
		navigationRequestsSet.insert(navigationRequests[i]);
	}

	std::vector<Move> navMoves = Navigation::NavigateShips(navigationRequestsSet);

	for (NavigationRequest* navReq : navigationRequests)
		delete navReq;
	navigationRequests.clear();

	Log::log() << "Navigation requests: " << navigationRequests.size() << " Navigation moves: " << navMoves.size() << std::endl;

	for (Move navMove : navMoves) {
		// Add a message in the angle (for Chlorine)
		//navMove.move_angle_deg += ((GetTask(GetShip(navMove.ship_id)->task_id)->target + 1) * 360);
		moves.push_back(navMove);
	}

	return moves;
}

void Instance::GenerateTasks()
{
	Stopwatch s("Generate tasks");

	// Clear old tasks
	current_task = 0;
	for (Task* task : tasks)
		delete task;
	tasks.clear();

	// Generate new tasks
	for (auto& kv : planets) {
		Planet* planet = kv.second;

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
		// Defend tasks
		std::set<Task*> defendTasks;
		for (Ship* ship : myShips) {
			if (ship->IsOur() && !ship->IsCommandable()) {
				Ship* enemyShip = GetClosestShip(ship->location, false);

				if (!enemyShip) continue;

				double distance = ship->location.DistanceTo(enemyShip->location);
				if (distance < 35) {
					Task* taskDefend = 0;
					for (Task* td : defendTasks) {
						if (td->defendingFromShip == enemyShip) {
							taskDefend = td;
							break;
						}
					}

					if (taskDefend == 0) {
						taskDefend = CreateTask(TaskType::DEFEND);
						taskDefend->target = -1;
						taskDefend->defendingFromShip = enemyShip;
						taskDefend->radius = 0;
						taskDefend->max_ships = 1;
						defendTasks.insert(taskDefend);
					}

					if (distance < taskDefend->defendingDistance) {
						taskDefend->defendingDistance = distance;
						taskDefend->location = enemyShip->location.ClosestPointTo(ship->location, ship->radius, 1);
					}
				}
			}
		}

		// Ship attack tasks
		for (auto& kv : ships) {
			Ship* ship = kv.second;
			if (ship->IsOur()) continue;
			// every enemy ship alive

			bool in_defend_task = false;
			for (Task* td : defendTasks) {
				if (td->defendingFromShip == ship) {
					in_defend_task = true;
					break;
				}
			}
			if (in_defend_task) continue;

			if (num_players == 4 && turn > 50 && planetsCount.at(player_id) > 0 && planetsCount.at(ship->owner_id) == 0) {
				// we don't care about this player

				// but we must to be sure that this ships is not attacking us
				bool is_attacking = false;
				for (auto& kv2 : ships) {
					Ship* ship2 = kv.second;
					if (ship2->IsOur() && !ship2->IsCommandable()) {
						if (ship2->location.DistanceTo(ship->location) < 35) {
							is_attacking = true;
							break;
						}
					}
				}
				if (!is_attacking)
					continue;
			}

			Task* taskAttack = CreateTask(TaskType::ATTACK);
			taskAttack->target = ship->entity_id;
			taskAttack->location = ship->location;
			taskAttack->radius = 0;
			taskAttack->indefense = ship->docking_status != ShipDockingStatus::Undocked;

			double r = ((double)rand() / (RAND_MAX)) + 1;
			taskAttack->max_ships = 1 + (r < 0.2 ? 1 : 0);
		}


		if (writing) {
			const Vector2 writeMessagePoints[] = {
				// H
				Vector2{ 0, 0 },
				Vector2{ 0, 1 },
				Vector2{ 0, 2 },
				Vector2{ 0, 3 },
				Vector2{ 1, 1.5 },
				Vector2{ 2, 1.5 },
				Vector2{ 3, 0 },
				Vector2{ 3, 1 },
				Vector2{ 3, 2 },
				Vector2{ 3, 3 },

				// A
				Vector2{ 4, 3 },
				Vector2{ 4.5, 2 },
				Vector2{ 5, 1 },
				Vector2{ 5.5, 0 },
				Vector2{ 5.5, 2 },
				Vector2{ 6, 1 },
				Vector2{ 6.5, 2 },
				Vector2{ 7, 3 },

				// L
				Vector2{ 8, 0 },
				Vector2{ 8, 1 },
				Vector2{ 8, 2 },
				Vector2{ 8, 3 },
				Vector2{ 9, 3 },

				// I
				Vector2{ 11, 0 },
				Vector2{ 11, 1 },
				Vector2{ 11, 2 },
				Vector2{ 11, 3 },

				// T
				Vector2{ 13, 0 },
				Vector2{ 14, 0 },
				Vector2{ 15, 0 },
				Vector2{ 14, 1 },
				Vector2{ 14, 2 },
				Vector2{ 14, 3 },

				// E
				Vector2{ 17, 0 },
				Vector2{ 17, 1 },
				Vector2{ 17, 2 },
				Vector2{ 17, 3 },
				Vector2{ 18, 0 },
				Vector2{ 18, 3 },
				Vector2{ 19, 0 },
				Vector2{ 19, 3 },
				Vector2{ 17.5, 1.5 },
				Vector2{ 18.5, 1.5 },
			};
			const int writeMessagePointsCount = sizeof(writeMessagePoints) / sizeof(writeMessagePoints[0]);
			const double scale = 7;

			for (int i = 0; i < writeMessagePointsCount; i++) {
				Task* writeTask = CreateTask(TaskType::WRITE);
				writeTask->target = -1;
				writeTask->location = messageOffset + writeMessagePoints[i] * scale;
				writeTask->radius = 0;
				writeTask->max_ships = 1;
			}
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
	Stopwatch s("Assign tasks");

	std::queue<Ship*> qShips;

	// non undocked ships have a fixed task
	for (Ship* ship : myShips) {
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
			}
		} else
			qShips.push(ship);
	}

	std::set<Ship*> unsuitableShips;

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
			{
				d += 8;

				if (distance < 10) {
					int enemies = CountNearbyShips(target, task->radius, 25, false);
					int friends = CountNearbyShips(target, task->radius, 25, true);
					if (enemies != 0 && friends != 0 && enemies > friends) {
						d += 1000; // we should not dock
					}
				}
				break;
			}
			case DEFEND:
				d -= 25; // very very important
				break;
			case ATTACK:
				if (task->indefense) {
					d -= 5;

					double arriveDist = ship->location.DistanceTo(target);
					int enemiesWhenArrive = CountNearbyShips(target, ship->radius, arriveDist, false);
					int friendsWhenArrive = CountNearbyShips(target, ship->radius, arriveDist, true);
					if (friendsWhenArrive >= enemiesWhenArrive) {
						d -= 10;
					}

					int enemiesClose = CountNearbyShips(target, ship->radius, 10, false);
					if (enemiesClose == 0)
						d -= 20;
					else 
						d -= 17 / (enemiesClose + 1);
				}
				// if we are in early game, we even priorize this more
				/*
				if (!rush_phase && turn < 30) {
					d -= 35;
				}
				*/
				break;
			case WRITE:
				// writing is less important
				d += 30;
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
			Log::log("Ship " + std::to_string(ship->entity_id) + " couldn't find a suitable task.");
			unsuitableShips.insert(ship);
			continue;
		}

		if (priorizedTask == 0) // shouldnt happen
			continue;

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

	Log::log() << "Unsuitable Ships: " << unsuitableShips.size() << std::endl;

	while (unsuitableShips.size() != 0) {
		bool atLeastOne = false;
		for (Task* task : tasks) {
			if (unsuitableShips.size() == 0) break;
			if (task->ships.size() > 15) continue;
			if (task->type == ATTACK || task->type == DEFEND) {
				atLeastOne = true;

				Ship* closestShip = 0;
				double minDist = INF;
				for (Ship* ship : unsuitableShips) {
					double dist = ship->location.DistanceTo(task->location);
					if (dist < minDist) {
						minDist = dist;
						closestShip = ship;
					}
				}

				if (closestShip == 0) { // this shouldnt happen
					unsuitableShips.clear();
					break;
				}

				closestShip->task_id = task->task_id;
				closestShip->task_priority = 0;
				task->ships.insert(closestShip);
				unsuitableShips.erase(closestShip);
			}
		}

		if (!atLeastOne) {
			unsuitableShips.clear();
			break;
		}
	}

	Log::log() << "Tasks have been assigned" << std::endl;
}

int Instance::CountNearbyShips(Vector2 location, double radius, double range, bool friends)
{
	int count = 0;

	if (friends) {
		for (Ship* ship : myShips) {
			if (ship->IsCommandable()) {
				if (ship->task_id != -1 && GetTask(ship->task_id)->type == TaskType::DOCK)
					continue;
				if (ship->location.DistanceTo(location) - radius < range)
					count++;
			}
		}
	}
	else {
		for (auto& kv : ships) {
			Ship* ship = kv.second;
			if (ship->IsOur()) continue;
			if (ship->IsCommandable()) {
				if (ship->location.DistanceTo(location) - radius < range)
					count++;
			}
		}
	}

	return count;
}

Ship* Instance::GetClosestShip(Vector2 location, bool friends)
{
	Ship* closest = nullptr;
	double minDist = INF;
	double dist;

	if (friends) {
		for (Ship* ship : myShips) {
			dist = ship->location.DistanceTo(location);
			if (dist < minDist) {
				minDist = dist;
				closest = ship;
			}
		}
	}
	else {
		for (auto& kv : ships) {
			Ship* ship = kv.second;
			if (ship->IsOur()) continue;

			dist = ship->location.DistanceTo(location);
			if (dist < minDist) {
				minDist = dist;
				closest = ship;
			}
		}
	}

	return closest;
}

long long Instance::CurrentTurnTime()
{
	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = finish - turn_start;
	return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}
