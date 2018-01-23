#pragma once

#include <utility>

#define INF 99999999

typedef int EntityId;
typedef int PlayerId;

template<typename T>
using possibly = std::pair<T, bool>;