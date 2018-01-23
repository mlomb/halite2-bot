#include "Vector2.hpp"

std::ostream& operator<<(std::ostream& out, const Vector2& location)
{
	out << '(' << location.x << ", " << location.y << ')';
	return out;
}
