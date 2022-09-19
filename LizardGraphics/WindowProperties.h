#pragma once

#include <cstdint>

class GLFWwindow;

struct FWindowProperties
{
	GLFWwindow* Window = nullptr;

	uint32_t WindowWidth = 0;
	uint32_t WindowHeight = 0;
};