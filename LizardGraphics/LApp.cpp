#include "LApp.h"
#include "pch.h"

LApp::LApp(const FWindowProperties& WndProperties)
{
	InitRenderer(WndProperties);
}

LApp::~LApp()
{
	DestroyRenderer();
}

void LApp::BeginLoop()
{
	BeginPlay();
	while (!glfwWindowShouldClose(WindowProperties.Window))
	{
		glfwPollEvents();
		Tick();
	}
}

void LApp::BeginPlay()
{

}

void LApp::Tick()
{

}

void LApp::InitRenderer(const FWindowProperties& WndProperties)
{
	InitGlfw(WndProperties);
	InitVulkan();
}

void LApp::DestroyRenderer()
{
	DestroyVulkan();
	DestroyGlfw();
}

void LApp::InitGlfw(const FWindowProperties& WndProperties)
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	WindowProperties.Window = glfwCreateWindow(WndProperties.WindowWidth, WndProperties.WindowHeight, "", nullptr, nullptr);
	if (!WindowProperties.Window)
	{
		throw std::runtime_error("Can't initialize GLFW!");
	}
}

void LApp::InitVulkan()
{
	uint32_t ExtensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &ExtensionCount, nullptr);
}

void LApp::DestroyVulkan()
{

}

void LApp::DestroyGlfw()
{
	glfwDestroyWindow(WindowProperties.Window);
	glfwTerminate();
}

