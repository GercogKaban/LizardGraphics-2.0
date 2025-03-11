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

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	if (_window = glfwCreateWindow(specs.wndWidth, specs.wndHeight, specs.wndName.data(), nullptr, nullptr);
		!_window)
	{
		return WindowInitRes::GLFW_CREATE_WINDOW_ERROR;
	}
	return WindowInitRes::SUCCESS;
}
