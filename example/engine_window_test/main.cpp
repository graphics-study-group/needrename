#include <SDL3/SDL.h>

#include "consts.h"
#include "MainClass.h"
#include "Functional/SDLWindow.h"

#include "Render/Material.h"
#include "Render/RenderSystem.h"
#include "Framework/go/GameObject.h"
#include "Framework/component/RenderComponent/TestTriangleRendererComponent.h"

using namespace Engine;

Engine::MainClass * cmc;

const char vert [] = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
void main() {
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
}
)";

const char frag [] = R"(
#version 330 core
out vec4 albedo;
void main() {
    albedo = vec4(1.0, 0.5, 0.2, 1.0);
}
)";

int main(int argc, char * argv[])
{
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions * opt = phraseOptions(argc, argv);
    if (opt->instantQuit)
        return -1;

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            opt->enableVerbose ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO);
    cmc->Initialize(opt);

    std::shared_ptr <GameObject> go = std::make_shared<GameObject>();
    std::shared_ptr <Material> mat = std::make_shared<Material>(cmc->renderer, vert, frag);
    cmc->renderer->RegisterMaterial(mat);
    std::shared_ptr <TestTriangleRendererComponent> testTriangle = std::make_shared<TestTriangleRendererComponent>(mat, go);
    mat->RegisterComponent(testTriangle);

    cmc->MainLoop();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
