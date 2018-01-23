#include "Entity.hpp"

#include "Instance.hpp"

bool Entity::IsOur() {
	return Instance::Get()->player_id == owner_id;
}
