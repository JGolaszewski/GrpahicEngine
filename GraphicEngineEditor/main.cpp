#include "Editor.h"
#include "backends/D3D11Backend.h"
#include "windows/GLFWWindow.h"

int main()
{
    Editor app;
    app.Run(std::make_unique<Engine::GLFWWindow>(1280, 720, "Editor"),
            std::make_unique<Engine::D3D11Backend>());

    return 0;
}
