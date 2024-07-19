#include <SDL3/SDL.h>
#include <cassert>

#include "consts.h"
#include "MainClass.h"
#include "Functional/SDLWindow.h"

#include "Render/Material/SingleColor.h"
#include "Render/RenderSystem.h"
#include "Framework/go/GameObject.h"
#include "Framework/component/RenderComponent/TestTriangleRendererComponent.h"

using namespace Engine;

Engine::MainClass * cmc;

int main(int argc, char * argv[])
{
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions * opt = ParseOptions(argc, argv);
    if (opt->instantQuit)
        return -1;

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            opt->enableVerbose ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO);
    cmc->Initialize(opt);

    // Setup material
    std::shared_ptr <SingleColor> mat = std::make_shared<SingleColor>(cmc->renderer, 0.0, 1.0, 0.0, 1.0);

    std::shared_ptr <GameObject> go = std::make_shared<GameObject>();
    std::shared_ptr <TestTriangleRendererComponent> testMesh = 
        std::make_shared<TestTriangleRendererComponent>(mat, go);
    
    cmc->renderer->RegisterComponent(testMesh);

    cmc->MainLoop();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
