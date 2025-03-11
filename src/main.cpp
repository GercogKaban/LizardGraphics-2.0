#include "src/LWindow.h"
#include "src/LRenderer.h"

int main()
{ 
	LWindow::LWindowSpecs wndSpecs{ "LGWindow", 1280, 720, false };
	LWindow wnd(wndSpecs);
	
	LRenderer renderer(wnd);
	
    auto triangle = ObjectBuilder::construct<Primitives::LRectangle>();
	renderer.loop();
}