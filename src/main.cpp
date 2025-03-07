#include "src/LWindow.h"
#include "src/LRenderer.h"
#include <signal.h>

int main()
{ 
	LWindow::LWindowSpecs wndSpecs{ "LGWindow", 1280, 720 };
	LWindow wnd(wndSpecs);

	LRenderer renderer(wnd.getWindow());
	wnd.loop();
	//return 0;
}