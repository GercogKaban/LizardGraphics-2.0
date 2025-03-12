#include "pch.h"
#include "LWindow.h"

#define GLFW_INCLUDE_VULKAN
#include <glfw3.h>

LWindow::LWindow(const LWindowSpecs& wndSpecs)
{
	if (auto res = init(wndSpecs); res != WindowInitRes::SUCCESS)
	{
		LLogger::LogString(WindowInitRes::SUCCESS, true);
	}
}

LWindow::~LWindow()
{
	glfwDestroyWindow(_window);
	glfwTerminate();
}

LWindow::WindowInitRes LWindow::init(const LWindowSpecs& wndSpecs)
{
	specs = wndSpecs;
	
	if (auto res = glfwInit(); res != GLFW_TRUE)
	{
		return WindowInitRes::GLFW_INIT_FAIL;
	}

	const auto monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	if (specs.wndMode == WindowMode::WindowedFullscreen)
	{
		glfwWindowHint(GLFW_RED_BITS, mode->redBits);
		glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
		glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
		glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);	
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    const int32 width = specs.wndMode == WindowMode::Windowed? specs.wndWidth : mode->width;
	const int32 height = specs.wndMode == WindowMode::Windowed? specs.wndHeight : mode->height;
	
	if (_window = glfwCreateWindow(width, height, specs.wndName.data(), glfwGetPrimaryMonitor(), nullptr);
		!_window)
	{
		return WindowInitRes::GLFW_CREATE_WINDOW_ERROR;
	}
	return WindowInitRes::SUCCESS;
}
