#include "Ship.hpp"

#include <algorithm>

#include "Instance.hpp"
#include "Navigation.hpp"
#include "Log.hpp"

Ship::Ship(EntityId id) : Entity(id)
{
}

bool Ship::IsCommandable() const {
	/*
	bool undocked = docking_status == ShipDockingStatus::Undocking && docking_progress == 1;
	return !(docking_status != ShipDockingStatus::Undocked && !undocked);
	*/
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
		return 10;
	case ShipDockingStatus::Docking:
		return docking_progress + 5;
	case ShipDockingStatus::Docked:
		return 5;
	case ShipDockingStatus::Undocking:
		return docking_progress;
	}
}

possibly<Move> Ship::ComputeMove()
{
	if (task_id == -1)
		return { Move::noop(), false };

	Instance* instance = Instance::Get();

	Task* task = instance->GetTask(task_id);

	Vector2 targetLocation;
	bool avoid_enemies = false;

	switch (task->type) {
	case DOCK:
	{
		if (docking_status == ShipDockingStatus::Docking ||
			docking_status == ShipDockingStatus::Undocking) break; // we can't do much

		bool threatened = false;
		if (instance->rush_phase) {
			int turnsToBeUndocked = TurnsToBeUndocked() + 2;
			double shipSafeRange = turnsToBeUndocked * hlt::constants::MAX_SPEED;

			int threats = instance->CountNearbyShips(location, radius, shipSafeRange, false);
			int friends = instance->CountNearbyShips(location, radius, shipSafeRange, true);

			threatened = threats >= friends;
		}

		if (docking_status == ShipDockingStatus::Docked) {
			// we're already docked

			if (threatened || instance->game_over) {
				return { Move::undock(entity_id), true };
			}

			return { Move::noop(), false };
		}

		Planet* planet = instance->GetPlanet(task->target);

		// if we can dock, we dock
		if (CanDock(planet)) {
			if (!threatened && !instance->game_over) {
				return { Move::dock(entity_id, planet->entity_id), true };
			}
			else {
				Log::log("Ship " + std::to_string(entity_id) + " can't dock because of threats!");
			}
		}

		// continue navigating to the planet
		targetLocation = location.ClosestPointTo(planet->location, planet->radius);
		avoid_enemies = true;
		break;
	}
	case ATTACK:
	{
		Ship* shipTarget = instance->GetShip(task->target);

		targetLocation = location.ClosestPointTo(shipTarget->location, shipTarget->radius);
		avoid_enemies = false;
		break;
	}
	case ESCAPE: // run bitch
		targetLocation = task->location;
		avoid_enemies = true;
		break;
	default:
	case NOTHING:
		Log::log() << "Ship " << entity_id << " has an invalid task!" << std::endl;
		return { Move::noop(), false };
	}

	Log::log() << "Ship " << entity_id << " is moving towards " << targetLocation << " avoiding enemies: " << avoid_enemies << std::endl;

	possibly<Move> move = { Move::noop(), false };

	if (avoid_enemies) {
		Vector2 bestLocation;
		double minDist = INF;
		int minEnemies = INF;

		instance->IterateMap(location, radius * 2 + hlt::constants::MAX_SPEED * 2, [&](Vector2 position, MapCell& cell, double distance) {
			if (cell.solid) return;

			if (cell.nextTurnEnemyShipsAttackInRange <= minEnemies) {
				double dist = position.DistanceTo(targetLocation);
				if (cell.nextTurnEnemyShipsAttackInRange < minEnemies || dist < minDist) {
					bestLocation = position;
					minDist = dist;
					minEnemies = cell.nextTurnEnemyShipsAttackInRange;
				}
			}
		});

		move = Navigation::NavigateShipTowardsTarget(this, bestLocation, std::min(7, (int)ceil(location.DistanceTo(bestLocation)) + 1), true, 500);
	}
	else {
		/*
		//targetLocation = { 50, 50 };

		// Move towards target
		const double movementRadius = radius * 2 + hlt::constants::MAX_SPEED;

		Vector2 bestLocation;
		double bestRatio = -1;
		double minDist = INF;

		instance->IterateMap(location, movementRadius, [&](Vector2 position, MapCell& cell, double distance) {
			if (cell.solid) return;

			if (cell.friendlyShipsThatCanReachThere > cell.nextTurnEnemyShipsAttackInRange) {
				double ratio = 1;
				if (cell.nextTurnEnemyShipsTakingDamage > 0)
					ratio = cell.friendlyShipsThatCanReachThere / cell.nextTurnEnemyShipsTakingDamage;

				if (ratio >= bestRatio) {
					double dist = position.DistanceTo(targetLocation);
					if (ratio > bestRatio || dist < minDist) {
						bestRatio = ratio;
						bestLocation = position;
						minDist = dist;
					}
				}
			}
		});

		if (bestRatio == -1) {
			Log::log() << "Couldn't find any movement spot!" << std::endl;
			return { Move::noop(), false };
		}

		auto move = Navigation::NavigateShipTowardsTarget(this, bestLocation, std::min(hlt::constants::MAX_SPEED, (int)ceil(location.DistanceTo(bestLocation))));
		*/

		move = Navigation::NavigateShipTowardsTarget(this, targetLocation, hlt::constants::MAX_SPEED, true, 500);
	}

	if (!move.second) {
		Log::log() << "Navigation is impossible" << std::endl;
	}

	return move;
}
