#pragma once

#include "pch.h"
#include "globals.h"

class LWindow
{
public:

	struct LWindowSpecs
	{
		std::string wndName;
		int16 wndWidth;
		int16 wndHeight;
	};

	LWindow(const struct LWindowSpecs& wndSpecs);
	~LWindow();

	void loop();

	class GLFWwindow* const getWindow() const { return _window; }

protected:

	enum class WindowInitRes
	{
		SUCCESS,
		GLFW_INIT_FAIL,
		GLFW_CREATE_WINDOW_ERROR,
	};

	WindowInitRes init(const LWindowSpecs& wndSpecs);
	
	class GLFWwindow* _window = nullptr;
};

