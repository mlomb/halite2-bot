#include "Entity.hpp"

#include "Instance.hpp"

bool Entity::IsOur() {
	return Instance::Get()->player_id == owner_id;
}

bool Entity::IsClose(const Entity* other, const double range) const
{
	return location.DistanceTo(other->location) - radius - other->radius < range;
}
