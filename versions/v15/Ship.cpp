#include "Ship.hpp"

#include <algorithm>

#include "Instance.hpp"
#include "Navigation.hpp"
#include "Log.hpp"

Ship::Ship(EntityId id) : Entity(id)
{
}

bool Ship::IsCommandable() const {
	bool undocked = docking_status == ShipDockingStatus::Undocking && docking_progress == 1;
	return !(docking_status != ShipDockingStatus::Undocked && !undocked);
	return docking_status == ShipDockingStatus::Undocked;
}

bool Ship::CanDockToAnyPlanet() {
	Instance* instance = Instance::Get();

	for (auto& kv : instance->planets) {
		Planet* planet = kv.second;
		if (CanDock(planet)) {
			return true;
		}
	}
	return false;
}

bool Ship::CanDock(Planet* planet)
{
	return location.DistanceTo(planet->location) <= (hlt::constants::SHIP_RADIUS + hlt::constants::DOCK_RADIUS + planet->radius);
}

int Ship::TurnsToBeUndocked() {
	switch (docking_status) {
	default:
	case ShipDockingStatus::Undocked:
		return 12;
	case ShipDockingStatus::Docking:
		return docking_progress + 5;
	case ShipDockingStatus::Docked:
		return 5;
	case ShipDockingStatus::Undocking:
		return docking_progress;
	}
}

std::pair<possibly<Move>, possibly<NavigationRequest*>> Ship::ComputeAction()
{
	NavigationRequest* navRequest = new NavigationRequest();

	if (task_id == -1)
		goto nomove;

	{
		Instance* instance = Instance::Get();

		Task* task = instance->GetTask(task_id);

		navRequest->ship = this;

		switch (task->type) {
		case DOCK:
		{
			if (docking_status == ShipDockingStatus::Docking ||
				docking_status == ShipDockingStatus::Undocking)
				goto nomove; // we can't do much

			bool threatened = false;
			if (instance->rush_phase) {
				int turnsToBeUndocked = TurnsToBeUndocked() + 1;
				double shipSafeRange = turnsToBeUndocked * hlt::constants::MAX_SPEED;

				int threats = instance->CountNearbyShips(location, radius, shipSafeRange, false);
				int friends = instance->CountNearbyShips(location, radius, shipSafeRange, true);

				threatened = threats >= friends;
			}

			if (docking_status == ShipDockingStatus::Docked) {
				// we're already docked

				if (threatened || instance->game_over) {
					delete navRequest;
					return { { Move::undock(entity_id), true }, { 0, false } };
				}

				goto nomove;
			}

			Planet* planet = instance->GetPlanet(task->target);

			// if we can dock, we dock
			if (CanDock(planet)) {
				if (!threatened && !instance->game_over) {
					delete navRequest;
					return { { Move::dock(entity_id, planet->entity_id), true },{ 0, false } };
				}
				else {
					Log::log("Ship " + std::to_string(entity_id) + " can't dock because of threats!");
					goto nomove;
				}
			}

			// continue navigating to the planet
			navRequest->targetLocation = location.ClosestPointTo(planet->location, planet->radius);
			navRequest->avoid_enemies = true;
			break;
		}
		case ATTACK:
		{
			Ship* shipTarget = instance->GetShip(task->target);

			navRequest->targetLocation = location.ClosestPointTo(shipTarget->location, shipTarget->radius, shipTarget->IsCommandable() ? hlt::constants::MIN_DISTANCE_FOR_CLOSEST_POINT : 1);
			navRequest->avoid_enemies = false;
			break;
		}
		case ESCAPE: // run bitch
			navRequest->targetLocation = task->location;
			navRequest->avoid_enemies = true;
			break;
		default:
		case NOTHING:
			Log::log() << "Ship " << entity_id << " has an invalid task!" << std::endl;
			goto nomove;
		}

		Log::log() << "Ship " << entity_id << " is moving from " << navRequest->ship->location << " towards " << navRequest->targetLocation << " avoiding enemies: " << navRequest->avoid_enemies << std::endl;
		if (navRequest->targetLocation.DistanceTo(location) < sqrt(2) - 0.1) {
			Log::log() << "... but ship it's already there..." << std::endl;
			goto nomove;
		}

		return { { Move::noop(), false },{ navRequest, true } };
	}

nomove:
	delete navRequest;
	return { { Move::noop(), false },{ 0, false } };
}
