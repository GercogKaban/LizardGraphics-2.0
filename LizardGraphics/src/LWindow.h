#pragma once

#include "globals.h"

class LWindow
{
public:
	enum class WindowMode
	{
		Windowed,
		Fullscreen,
		WindowedFullscreen,
	};
	struct LWindowSpecs
	{
		WindowMode wndMode = WindowMode::WindowedFullscreen;
		std::string wndName;

		bool bVsync = false;

		// ignored if you choose Fullscreen/Windowed fullscreen mods
		int32 wndWidth;

		// ignored if you choose Fullscreen/Windowed fullscreen mods
		int32 wndHeight;
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

