#include "src/LWindow.h"
#include "src/LRenderer.h"

class LTickablePlane : public Primitives::LPlane, public LTickable
{

protected:

	void tick(float delta) override
	{
		static auto startTime = std::chrono::high_resolution_clock::now();
	
		auto currentTime = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count() * delta;

		setModelMatrix(glm::rotate(getModelMatrix(), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
	}
};

int main()
{ 
	LWindow::LWindowSpecs wndSpecs{LWindow::WindowMode::Windowed,"LGWindow",false, 1280, 720 };
	LWindow wnd(wndSpecs);
	
	LRenderer renderer(wnd);
	
    auto plane1 = ObjectBuilder::construct<Primitives::LPlane>();
	
	auto plane2 = ObjectBuilder::construct<LTickablePlane>();
	plane2->translate(glm::vec3(1.0f, 0.0f, 0.0f));
	
	renderer.loop();
}