#pragma once

#include <string>
#include <fstream>

class Log {
public:
	Log();

	void Open(const std::string& path);

	static Log* Get();

	static void log(const std::string& string);
	static std::ofstream& log();

private:
	std::ofstream file;

	static Log* s_Log;
};