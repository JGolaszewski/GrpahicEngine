#include "SandboxApp.h"

#include <iostream>

int main() {
	std::cout << "Running program..." << std::endl;

	SandboxApp app;

	app.Run();

	std::cout << "Closing..." << std::endl;
	return 0;
}