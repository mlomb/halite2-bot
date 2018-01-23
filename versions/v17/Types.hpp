#pragma once

#include <utility>

#define INF 99999999

typedef unsigned int EntityId;
typedef unsigned int PlayerId;

template<typename T>
using possibly = std::pair<T, bool>;