#pragma once

#include <string>
#include <functional>

class Image {
public:
	static std::tuple<unsigned char, unsigned char, unsigned char> Interpolate(std::tuple<unsigned char, unsigned char, unsigned char> colorA, std::tuple<unsigned char, unsigned char, unsigned char> colorB, double t = 0.5);

	static void WriteImage(const std::string& path, unsigned int width, unsigned int height, std::function<std::tuple<unsigned char, unsigned char, unsigned char>(int, int)> pixelRequest);

private:
	Image() = default;
};