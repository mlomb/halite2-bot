#pragma once

#include "Types.hpp"
#include "Vector2.hpp"

class Entity {
public:
	Entity(EntityId id) : entity_id(id)
	{
	}

	bool IsOur();
	bool IsClose(const Entity* other, const double range) const;

	Vector2 location;
	EntityId entity_id;
	PlayerId owner_id;
	double radius;
	bool alive;
	int health;
};
