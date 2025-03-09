#include "pch.h"
#include "LWindow.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#define GLFW_INCLUDE_VULKAN
#include <glfw3.h>

LWindow::LWindow(const LWindowSpecs& wndSpecs)
{
	if (auto res = init(wndSpecs); res != WindowInitRes::SUCCESS)
	{
		LLogger::LogString(res, true);
	}
}

LWindow::~LWindow()
{
	glfwDestroyWindow(_window);
	glfwTerminate();
}

LWindow::WindowInitRes LWindow::init(const LWindowSpecs& wndSpecs)
{
	if (auto res = glfwInit(); res != GLFW_TRUE)
	{
		return WindowInitRes::GLFW_INIT_FAIL;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	if (_window = glfwCreateWindow(wndSpecs.wndWidth, wndSpecs.wndHeight, wndSpecs.wndName.data(), nullptr, nullptr);
		!_window)
	{
		return WindowInitRes::GLFW_CREATE_WINDOW_ERROR;
	}
	return WindowInitRes::SUCCESS;
}
