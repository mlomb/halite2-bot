#include "Log.hpp"

Log* Log::s_Log = nullptr;

Log::Log() {

}

void Log::Open(const std::string& path)
{
	file.open(path, std::ios::out);
}

Log* Log::Get() {
	if (s_Log == nullptr)
		s_Log = new Log();
	return s_Log;
}

void Log::log(const std::string& str) {
	Get()->file << str << std::endl;
}

std::ofstream& Log::log()
{
	return Get()->file;
}
