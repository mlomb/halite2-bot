#include "hlt/hlt.hpp"
#include "hlt/navigation.hpp"

#include <unordered_map>
#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <chrono>

/*
	TO-DO
	* 2p rush defense
	* 2p rush trigger
	* 4p rush defense
	* FixPathfinding with own ships
*/

#define INF 99999999

/* Forward declare */
void init(const hlt::Metadata& metadata);
std::vector<hlt::Move> frame(const hlt::Map& map);

int main() {
	const hlt::Metadata metadata = hlt::initialize("mlomb-bot-v6");
	init(metadata);

	while (true) {
		auto start = std::chrono::high_resolution_clock::now();

		const hlt::Map map = hlt::in::get_map();

		std::vector<hlt::Move> moves = frame(map);

		if (!hlt::out::send_moves(moves)) {
			hlt::Log::log("send_moves failed; exiting");
			break;
		}

		auto finish = std::chrono::high_resolution_clock::now();
		auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);
		hlt::Log::log("Frame took: " + std::to_string(millis.count()) + "ms");
	}
}

/////////////////////////////////////////////
// mlomb-bot
/////////////////////////////////////////////

class Ship;
class Planet;
class Task;

enum TaskType {
	NOTHING,
	// travelling to dock or already docked to a specific planet
	DOCK,
	// defend a planet
	DEFEND_PLANET,
	// defend a ship
	DEFEND_SHIP,
	// attack a ship
	ATTACK
};

class Instance {
public:
	Instance(const hlt::PlayerId ourId);
	void SyncMap(const hlt::Map& map);

	Ship* GetShip(hlt::EntityId shipId);
	Planet* GetPlanet(hlt::EntityId planetId);

	Task* CreateTask(TaskType type = TaskType::NOTHING);
	Task* GetTask(unsigned int task_id);

	void GenerateTasks();
	void AssignTasks();

	int CountNearbyShips(hlt::Location location, double radius, double range, bool friends);

	std::unordered_map<hlt::EntityId, Ship*> ships;
	std::unordered_map<hlt::EntityId, Planet*> planets;
	std::vector<Task*> tasks;

	unsigned int turn = -1;
	const hlt::Map* map;
	const hlt::PlayerId ourId;
	unsigned int current_task = 0;

	bool two_players = false;
	bool rushing_dectection_phase = false;
};
static Instance* instance;

class Entity {
public:
	Entity(hlt::EntityId id)
		: entity_id(id)
	{
	}

	bool IsOur() {
		return instance->ourId == owner;
	}

	bool alive = true;
	hlt::EntityId entity_id;
	Instance* instance;
	hlt::PlayerId owner;
};

class Planet : public Entity {
public:
	Planet(hlt::EntityId id)
		: Entity(id)
	{
	}

	const hlt::Planet& GetAsHLTPlanet() const {
		return instance->map->get_planet(entity_id);
	}
};

class Task {
public:
	Task(unsigned int task_id)
		: task_id(task_id)
	{
	}

	bool IsFull() {
		return max_ships != -1 && ships.size() >= max_ships;
	}

	std::string info() {
		std::string type_name = "UNKNOWN";
		switch (type) {
		case DOCK: type_name = "DOCK"; break;
		case DEFEND_PLANET: type_name = "DEFEND_PLANET"; break;
		case DEFEND_SHIP: type_name = "DEFEND_SHIP"; break;
		case ATTACK: type_name = "ATTACK"; break;
		}
		return "type: " + type_name + " target: " + std::to_string(target);
	}
	
	unsigned int task_id;
	std::set<Ship*> ships; // ships assigned to this task
	int max_ships = -1; // -1 = infinite

	/* Specific task related */
	TaskType type = TaskType::NOTHING;
	hlt::EntityId target = -1;
	hlt::Location location;
	double radius = 0;

	// DOCK
	int nearbyFriends = 0;
	int nearbyEnemies = 0;
	int to_undock = 0;

	// DEFEND, ATTACK
	bool indefense = false;
	std::vector<Ship*> enemyShips;
};

class Ship : public Entity {
public:
	Ship(hlt::EntityId id)
		: Entity(id)
	{
	}

	const hlt::Ship& GetAsHLTShip() const {
		return instance->map->get_ship(owner, entity_id);
	}

	bool IsCommandable() {
		const hlt::Ship& ship = GetAsHLTShip();
		bool undocked = ship.docking_status == hlt::ShipDockingStatus::Undocking && ship.docking_progress == 1;
		return !(ship.docking_status != hlt::ShipDockingStatus::Undocked && !undocked);
	}

	bool CanDockToAnyPlanet() {
		const hlt::Ship& ship = GetAsHLTShip();
		for (const hlt::Planet& planet : instance->map->planets) {
			if (ship.can_dock(planet)) {
				return true;
			}
		}
		return false;
	}

	int TurnsToBeUndocked() {
		const hlt::Ship& ship = GetAsHLTShip();

		switch (ship.docking_status) {
		default:
		case hlt::ShipDockingStatus::Undocked:
			return 10;
		case hlt::ShipDockingStatus::Docking:
			return ship.docking_progress + 5;
		case hlt::ShipDockingStatus::Docked:
			return 5;
		case hlt::ShipDockingStatus::Undocking:
			return ship.docking_progress;
		}
	}

	hlt::possibly<hlt::Move> ComputeMove() {
		if (task_id == -1)
			return { hlt::Move::noop(), false };

		Task* task = instance->GetTask(task_id);
		const hlt::Ship& ship = GetAsHLTShip();

		const hlt::Location& myLocation = ship.location;

		switch (task->type) {
		case DOCK:
		{
			if (ship.docking_status == hlt::ShipDockingStatus::Docking ||
				ship.docking_status == hlt::ShipDockingStatus::Undocking) break; // we can't do much
			bool threats = false;
			if (instance->rushing_dectection_phase) {
				int turnsToBeUndocked = TurnsToBeUndocked() + 2;
				double shipSafeRange = turnsToBeUndocked * hlt::constants::MAX_SPEED;

				threats = instance->CountNearbyShips(myLocation, ship.radius, shipSafeRange, false) > 0;
				//hlt::Log::log("shipSafeRange: " + std::to_string(shipSafeRange) + " threats:" + std::to_string(threats));
			}

			if (threats) {
				hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " can't dock because of threats!");
			}

			if (ship.docking_status == hlt::ShipDockingStatus::Docked) {
				// we're already docked
				if (threats) {
					return { hlt::Move::undock(ship.entity_id), true };
				}

				return { hlt::Move::noop(), false };
			}

			const hlt::Planet& planet = instance->map->get_planet(task->target);

			// if we can dock, we dock
			if (ship.can_dock(planet) && !threats) {
				/*
				if (instance->rushing_dectection_phase) {
					hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " arrived to the planet but we are waiting for rushers");
					return { hlt::Move::noop(), false };
				}
				*/

				return { hlt::Move::dock(ship.entity_id, planet.entity_id), true };
			}

			// continue navigating to the planet
			return hlt::navigation::navigate_ship_to_dock(*instance->map, ship, planet, hlt::constants::MAX_SPEED);
		}
		case ATTACK:
		{
			Ship* shipPtrTarget = instance->GetShip(task->target);
			const hlt::Ship& shipTarget = shipPtrTarget->GetAsHLTShip();

			hlt::Location attackPoint = ship.location.get_closest_point(shipTarget.location, shipTarget.radius);

			return hlt::navigation::navigate_ship_towards_target(*instance->map, ship, attackPoint, hlt::constants::MAX_SPEED);
		}
		case DEFEND_PLANET:
		{
			hlt::Log::log("Defend planet!");
			break;
		}
		}

		return { hlt::Move::noop(), false };
	}

	double taskPriority = 0;
	unsigned int task_id = -1; // assigned task
};

Instance::Instance(const hlt::PlayerId ourId)
	: ourId(ourId)
{
}

void Instance::SyncMap(const hlt::Map& map) {
	turn++;
	this->map = &map;

	for (auto& kv : ships) { kv.second->alive = false; }
	for (auto& kv : planets) { kv.second->alive = false; }

	// Ships
	for (auto& kv : map.ships) {
		for (const hlt::Ship& ship : kv.second) {
			Ship* shipPtr = 0;
			auto it = ships.find(ship.entity_id);
			if (it == ships.end()) {
				shipPtr = new Ship(ship.entity_id);
				shipPtr->owner = kv.first;
				shipPtr->instance = this;
				ships.insert(std::make_pair(ship.entity_id, shipPtr));
			}
			else
				shipPtr = (*it).second;
			shipPtr->task_id = -1;
			shipPtr->alive = ship.is_alive();
		}
	}

	// Planets
	for (const hlt::Planet& planet : map.planets) {
		Planet* planetPtr = 0;
		auto it = planets.find(planet.entity_id);
		if (it == planets.end()) {
			planetPtr = new Planet(planet.entity_id);
			planetPtr->instance = this;
			planets.insert(std::make_pair(planet.entity_id, planetPtr));
		}
		else
			planetPtr = (*it).second;
		planetPtr->alive = planet.is_alive();
		planetPtr->owner = planet.owned ? planet.owner_id : -1;
	}

	/* Clear old tasks */
	current_task = 0;
	for (Task* task : tasks)
		delete task;
	tasks.clear();

	/* Generate new tasks */
	GenerateTasks();

	hlt::Log::log("Map synced OK -- planets: " + std::to_string(planets.size()) + " ships: " + std::to_string(ships.size()) + " tasks: " + std::to_string(tasks.size()));
}

Ship* Instance::GetShip(hlt::EntityId shipId) {
	auto it = ships.find(shipId);
	if (it != ships.end())
		return (*it).second;
	return nullptr;
}

Planet* Instance::GetPlanet(hlt::EntityId planetId) {
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

void Instance::GenerateTasks()
{
	// Each planet will have at least one task
	for (auto& kv : planets) {
		Planet* planetPtr = kv.second;
		if (!planetPtr->alive) continue; // if the planet was destroyed 'it doesnt exist'

		const hlt::Planet& planet = planetPtr->GetAsHLTPlanet();
		if (planet.owned && !planetPtr->IsOur()) { // if the planet is owned and it's not by us the task will be ATTACK
			// Task ATTACK (planet)
			///Task* taskAttack = CreateTask(TaskType::ATTACK);
			///taskAttack->data.target = planet.entity_id;
			///taskAttack->data.location = planet.location;
		}
		else { // the planet is not owned or it's owned by us
			// Task DOCK
			Task* taskDock = CreateTask(TaskType::DOCK);
			taskDock->target = planet.entity_id;
			taskDock->location = planet.location;
			taskDock->radius = planet.radius;
			taskDock->max_ships = planet.docking_spots;
			
			double range = hlt::constants::DOCK_TURNS * hlt::constants::MAX_SPEED;
			taskDock->nearbyFriends = CountNearbyShips(planet.location, planet.radius + hlt::constants::DOCK_RADIUS, 10, true);
			taskDock->nearbyEnemies = CountNearbyShips(planet.location, planet.radius + hlt::constants::DOCK_RADIUS, range, false);

			/*
			if (taskDock->nearbyEnemies > 0) {
				if (taskDock->Dock_ShouldUndock()) {
					hlt::Log::log("Planet " + std::to_string(planet.entity_id) + " -- friends: " + std::to_string(taskDock->nearbyFriends) + " enemies: " + std::to_string(taskDock->nearbyEnemies));
					taskDock->max_ships = 0; // no ship should dock to this planet
				}
			}
			*/

			if (planetPtr->IsOur()) { // if the planet is our, we'll defend it
				/*
				std::vector<Ship*> nearbyEnemyShips;

				double detectRange = planet.radius * 0.35;
				// check if there are enemies nearby
				for (auto& kv : ships) {
					Ship* shipPtr = kv.second;
					if (!shipPtr->alive) continue;
					if (shipPtr->IsOur()) continue;

					const hlt::Ship& ship = shipPtr->GetAsHLTShip();
					double distance = ship.location.get_distance_to(planet.location) - planet.radius;
					if (distance < detectRange) {
						// WE ARE UNDER ATTACK
						hlt::Log::log("Our planet " + std::to_string(planet.entity_id) + " is under attack by the ship " + std::to_string(shipPtr->entity_id));
						nearbyEnemyShips.push_back(shipPtr);
					}
				}

				if (nearbyEnemyShips.size() > 0) {
					// Task DEFEND
					/*
					Task* taskDefend = CreateTask(TaskType::DEFEND);
					taskDefend->target = planet.entity_id;
					taskDefend->location = planet.location;
					taskDefend->radius = planet.radius;
					taskDefend->enemyShips = nearbyEnemyShips;
					* /
				}
				*/
			}
		}
	}

	// Ship tasks
	for (auto& kv : ships) {
		Ship* shipPtr = kv.second;
		if (!shipPtr->alive) continue;
		if (shipPtr->IsOur()) continue;
		// every enemy ship alive

		Task* taskAttack = CreateTask(TaskType::ATTACK);
		taskAttack->target = shipPtr->entity_id;
		taskAttack->location = shipPtr->GetAsHLTShip().location;
		taskAttack->radius = 0;
		taskAttack->indefense = shipPtr->GetAsHLTShip().docking_status != hlt::ShipDockingStatus::Undocked;
		taskAttack->max_ships = 2;
	}
}

void Instance::AssignTasks()
{
	std::queue<Ship*> qShips;
	std::set<Task*> dockTasksWithShips;

	// non undocked ships have a fixed task
	for (const hlt::Ship& ship : map->ships.at(ourId)) {
		Ship* shipPtr = GetShip(ship.entity_id);

		if (!shipPtr->alive) continue;

		if (!shipPtr->IsCommandable()) {
			// ship is docking, docked or undocking

			Task* dockTask = 0;

			// we're docked to a planet, find the task that match
			for (Task* task : tasks) {
				if (task->type == TaskType::DOCK && task->target == ship.docked_planet) {
					dockTask = task;
					break;
				}
			}
			
			if (dockTask == 0) {
				hlt::Log::log("Dock task for ship " + std::to_string(ship.entity_id) + " not found.");
				qShips.push(shipPtr);
			}
			else {
				std::string status_name = "UNKNOWN";
				switch (ship.docking_status)
				{
				case hlt::ShipDockingStatus::Docking: status_name = "DOCKING"; break;
				case hlt::ShipDockingStatus::Docked: status_name = "DOCKED"; break;
				case hlt::ShipDockingStatus::Undocking: status_name = "UNDOCKING"; break;
				}

				hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " continues " + status_name + " to planet " + std::to_string(dockTask->target) + " -- docking progress: " + std::to_string(ship.docking_progress));

				shipPtr->task_id = dockTask->task_id;
				shipPtr->taskPriority = INF; // docked ships cant be relevated
				dockTask->ships.insert(shipPtr);

				dockTasksWithShips.insert(dockTask);
			}
		}
		else {
			qShips.push(shipPtr);
		}
	}

	/*
	for (Task* task : dockTasksWithShips) {
		if (task->nearbyEnemies == 0) continue; // there is more friendly ships than enemy ships, we dont need to undock
		if (task->Dock_ShouldUndock()) {
			// we're in trouble

			int to_undock = std::min((int)task->ships.size(), task->nearbyEnemies + 1);

			hlt::Log::log("Planet " + std::to_string(task->target) + " is under attack and we should undock at least " + std::to_string(to_undock) + " ships -- " + std::to_string(task->nearbyFriends) + " vs " + std::to_string(task->nearbyEnemies));

			task->to_undock = to_undock;
			/*
			for (Ship* shipPtr : task->ships) {
				const hlt::Ship& ship = shipPtr->GetAsHLTShip();
				if (ship.docking_status == hlt::ShipDockingStatus::Docked) {
					// undock if we have an incoming attack and we don't have nearby friend ships to defend us :(

				}
			}
			* /
			task->max_ships = 0;
		}
	}
	*/

	const int mapSize = sqrt(map->map_width * map->map_width + map->map_height * map->map_height) * 2;

	// for the rest of the ships (which are now on qShips)
	while (!qShips.empty()) {
		Ship* shipPtr = qShips.front();
		qShips.pop();
		const hlt::Ship& ship = shipPtr->GetAsHLTShip();

		Task* priorizedTask = 0;
		double maxPriority = 0;
		Ship* otherShipPtrOverriding = 0;

		for (Task* task : tasks) {
			if (task->max_ships == 0) continue;

			// this is a priority relative to the ship, aka how important this task is for this ship
			hlt::Location target = task->location;
			if (task->radius != 0)
				target = ship.location.get_closest_point(task->location, task->radius);
			double distance = ship.location.get_distance_to(task->location);

			/* PRIORITY CALCULATION */
			double d = distance;
			switch (task->type)
			{
			case ATTACK:
				if (task->indefense) d -= 8;
				break;
			case DEFEND_SHIP:
			{
				/*
				Ship * shipPtrToDefend = GetShip(task->target);
				if (shipPtrToDefend->GetAsHLTShip().docking_status != hlt::ShipDockingStatus::Undocked) { // indefense
					d -= 10;
				}
				*/
				break;
			}
			case DOCK:
				// dont dock if nearby friend ships are less than nearby enemy ships
				//if (task->Dock_ShouldUndock()) {
					//hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " will not dock to planet " + std::to_string(planet.entity_id) + "!");
				//	continue;
				//}
				break;
			}

			double distancePriority = 10 - d / 10;
			double priority = distancePriority;
			/* PRIORITY CALCULATION */

			Ship* otherShipPtrWithLessPriority = 0;
			if (task->IsFull()) { // task is full, no other ship can help
				// but we check if we have a higher priority than one of the ships already assigned to this task
				double otherShipPtrMinPriority = INF; // less is better
				for (Ship* otherShipPtr : task->ships) {
					double otherShipPtrPriority = otherShipPtr->taskPriority;

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
			hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " couldn't find a suitable task!");
		}
		else {
			hlt::Log::log("Ship " + std::to_string(ship.entity_id) + " assigned to task " + std::to_string(priorizedTask->task_id) + " (" + priorizedTask->info() + ") with priority " + std::to_string(maxPriority));

			//if (priorizedTask->type == TaskType::DOCK) {
			//	priorizedTask->nearbyFriends--;
			//}

			if (otherShipPtrOverriding) {
				hlt::Log::log("... while overriding ship " + std::to_string(otherShipPtrOverriding->entity_id) + " in task " + std::to_string(otherShipPtrOverriding->task_id));
				priorizedTask->ships.erase(otherShipPtrOverriding);
				qShips.push(otherShipPtrOverriding);
				otherShipPtrOverriding->task_id = -1;
				otherShipPtrOverriding->taskPriority = 0;
			}

			shipPtr->task_id = priorizedTask->task_id;
			shipPtr->taskPriority = maxPriority;
			priorizedTask->ships.insert(shipPtr);
		}
	}
}

int Instance::CountNearbyShips(hlt::Location location, double radius, double range, bool friends)
{
	int count = 0;

	for (auto& kv : ships) {
		Ship* shipPtr = kv.second;
		if (!shipPtr->alive) continue;
		if (shipPtr->IsOur() == friends) {
			if (shipPtr->IsCommandable()) {
				const hlt::Ship& ship = shipPtr->GetAsHLTShip();

				if (ship.location.get_distance_to(location) - radius < range) {
					count++;
				}
			}
		}
	}

	return count;
}

double random01() {
	return ((double)rand() / (RAND_MAX));
}

void init(const hlt::Metadata& metadata)
{
	std::ostringstream initial_map_intelligence;
	initial_map_intelligence
		<< "width: " << metadata.initial_map.map_width
		<< "; height: " << metadata.initial_map.map_height
		<< "; players: " << metadata.initial_map.ship_map.size()
		<< "; my ships: " << metadata.initial_map.ship_map.at(metadata.player_id).size()
		<< "; planets: " << metadata.initial_map.planets.size();
	hlt::Log::log(initial_map_intelligence.str());

	instance = new Instance(metadata.player_id);
	instance->SyncMap(metadata.initial_map);
	instance->two_players = metadata.initial_map.ship_map.size() == 2;
	instance->rushing_dectection_phase = instance->two_players; // only detect rushing on 2p games
}

std::vector<hlt::Move> frame(const hlt::Map& map)
{
	instance->SyncMap(map);

	/* Detect rushers */
	if (instance->rushing_dectection_phase) {
		hlt::Log::log("We're in rushing detection phase!");

		bool threatsFound = false;

		for (auto& kv : map.ships) {
			if (kv.first == instance->ourId || threatsFound) continue;

			for (const hlt::Ship& ship : kv.second) {
				if (ship.docking_status != hlt::ShipDockingStatus::Undocked) {
					threatsFound = true;
					break;
				}
			}
		}

		if (!threatsFound) {
			if (map.ship_map.at(instance->ourId).size() > 6 || instance->turn > 30) {
				// just end the rushing detection phase if we generated at lease 6 ships
				threatsFound = false;
			}
		}

		instance->rushing_dectection_phase = !threatsFound;
		hlt::Log::log(std::string("Rushing detection: ") + (instance->rushing_dectection_phase ? "Continues" : "Ended"));
	}

	/* Organize ships; assign tasks */
	instance->AssignTasks();

	/* Execute the tasks assigned by priority */
	std::vector<Ship*> requireComputeShips;
	for (auto& kv : instance->ships) {
		Ship* shipPtr = kv.second;
		if (!shipPtr->IsOur()) continue;
		if (!shipPtr->alive) continue;

		requireComputeShips.push_back(shipPtr);
	}

	// sort by priority
	std::sort(requireComputeShips.begin(), requireComputeShips.end(), [](const Ship* shipPtrA, const Ship* shipPtrB) {
		return shipPtrA->taskPriority > shipPtrB->taskPriority;
	});

	std::vector<hlt::Move> moves;

	hlt::navigation::clear_collision_map(map);
	for (Ship* shipPtr : requireComputeShips) {
		auto move = shipPtr->ComputeMove();
		if (move.second) {
			// Add a message in the angle
			move.first.move_angle_deg += ((shipPtr->task_id + 1) * 360);
			moves.push_back(move.first);
		}
	}

	return moves;
}