#include "Editor.h"
#include "backends/D3D11Backend.h"
#include "windows/GLFWWindow.h"

#include <iostream>

int main()
{
    std::cout << "Running program..." << std::endl;

    Editor app;
    app.Run(std::make_unique<Engine::GLFWWindow>(1280, 720, "Editor"),
            std::make_unique<Engine::D3D11Backend>());

    std::cout << "Closing..." << std::endl;
    return 0;
}
