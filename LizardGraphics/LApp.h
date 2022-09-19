#pragma once

#include "Enums.h"
#include "WindowProperties.h"

class LApp
{
public:

	LApp(const FWindowProperties& WndProperties);
	~LApp();

	void BeginLoop();

protected:

	// methods

	virtual void BeginPlay();
	virtual void Tick();

	void InitRenderer(const FWindowProperties& WndProperties);
	void DestroyRenderer();

	void InitGlfw(const FWindowProperties& WndProperties);
	void DestroyGlfw();

	void InitVulkan();
	void DestroyVulkan();

	// fields

	FWindowProperties WindowProperties;
};