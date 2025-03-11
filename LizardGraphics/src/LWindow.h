#pragma once

#include "globals.h"

class LWindow
{
public:

	struct LWindowSpecs
	{
		std::string wndName;
		int16 wndWidth;
		int16 wndHeight;
		bool bVsync = false;
	};

	LWindow(const struct LWindowSpecs& wndSpecs);
	~LWindow();

	const LWindowSpecs& getWindowSpecs() const {return specs;}
	class GLFWwindow* const getWindow() const { return _window; }

protected:

	enum class WindowInitRes
	{
		SUCCESS,
		GLFW_INIT_FAIL,
		GLFW_CREATE_WINDOW_ERROR,
	};

	WindowInitRes init(const LWindowSpecs& wndSpecs);
	LWindowSpecs specs;
	
	class GLFWwindow* _window = nullptr;
	
};

