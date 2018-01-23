#include "Ship.hpp"

#include <algorithm>

#include "Instance.hpp"
#include "Navigation.hpp"
#include "Log.hpp"

Ship::Ship(EntityId id) : Entity(id)
{
	navigationOptions.reserve(360 * 7 + 1);
	navigationOptions.emplace_back(NavigationOption(0, 0));
	for (int angle = 0; angle < 360; angle++) {
		for (int thrust = 1; thrust <= 8; thrust++) {
			navigationOptions.emplace_back(NavigationOption(angle, thrust >= 7 ? 7 : thrust, thrust == 8));
		}
	}
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

void Ship::UpdateMaxThrusts()
{
	Instance* instance = Instance::Get();

	for (int angle_deg = 0; angle_deg < 360; angle_deg++) {
		max_thrusts[angle_deg] = 0;
		while (max_thrusts[angle_deg] < 7) {
			bool collision = false;
			const Vector2& futurePosition = location + instance->velocityCache[angle_deg][max_thrusts[angle_deg] + 1];
			for (const Entity* e : collisionEventHorizon) {
				if (Navigation::CheckEntityBetween(location, futurePosition, e)) {
					collision = true;
					break;
				}
			}
			if (collision)
				break;
			else
				max_thrusts[angle_deg]++;
		}
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
				double shipSafeRange = turnsToBeUndocked * hlt::constants::MAX_SPEED + hlt::constants::WEAPON_RADIUS;

				int threats = instance->CountNearbyShips(location, radius, shipSafeRange, false);
				int friends = instance->CountNearbyShips(location, radius, shipSafeRange, true);

				threatened = threats != 0 && threats >= friends;
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
				bool waiting_for_write = instance->writing && instance->turns_writing < 25;

				if (!threatened && !instance->game_over && !waiting_for_write) {
					delete navRequest;
					return { { Move::dock(entity_id, planet->entity_id), true },{ 0, false } };
				}
				else {
					Log::log("Ship " + std::to_string(entity_id) + " can't dock because of threats!");
					// at this point we can dock but we are threatened, so we try to get the furthest from the enemy ships while being able to dock

					Ship* closestEnemyShip = instance->GetClosestShip(location, false);
					if (!closestEnemyShip) goto nomove; // what?

					Vector2 enemyRelativePosition = closestEnemyShip->location - planet->location;
					Vector2 enemyOpposite = planet->location + Vector2{ -enemyRelativePosition.x, -enemyRelativePosition.y };

					navRequest->targetLocation = enemyOpposite.ClosestPointTo(planet->location, planet->radius);
					navRequest->avoid_enemies = true;
					break;
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
			/*
			if (location.DistanceTo(shipTarget->location) > (hlt::constants::SHIP_RADIUS + hlt::constants::MAX_SPEED + hlt::constants::WEAPON_RADIUS) * 2) {
				Log::log() << "Ship " << entity_id << " is too far away, we'll avoid enemies until we reach the target " << shipTarget->entity_id << std::endl;
				navRequest->avoid_enemies = true;
			}
			*/
			break;
		}
		case DEFEND:
		{
			if (location.DistanceTo(task->defendingFromShip->location) < 7) { // attack the ship
				navRequest->targetLocation = location.ClosestPointTo(task->defendingFromShip->location, task->defendingFromShip->radius);
			}
			else { // stay close to the planet
				navRequest->targetLocation = task->location;
			}
			navRequest->avoid_enemies = false;
			break;
		}
		case SUICIDE:
		{
			navRequest->targetLocation = task->location + Vector2::Velocity(location.OrientTowardsRad(task->location), hlt::constants::MAX_SPEED);
			navRequest->avoid_enemies = false;
			navRequest->avoid_obstacles = false;
			break;
		}
		case WRITE:
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
			// goto nomove; don't enable this, the ship should avoid enemies if necessary
		}

		return { { Move::noop(), false },{ navRequest, true } };
	}

nomove:
	delete navRequest;
	return { { Move::noop(), false },{ 0, false } };
}
