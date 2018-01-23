#pragma once

#include <utility>

#define INF 99999999
#define MAX_DISTANCE 1000

typedef int EntityId;
typedef int PlayerId;

template<typename T>
using possibly = std::pair<T, bool>;