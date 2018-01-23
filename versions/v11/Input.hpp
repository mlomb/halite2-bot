#pragma once

#include <iostream>
#include <string>
#include <sstream>

namespace in {
	static std::string GetString() {
		std::string result;
		std::getline(std::cin, result);
		return result;
	}

	static std::stringstream GetSString() {
		return std::stringstream(GetString());
	}
}