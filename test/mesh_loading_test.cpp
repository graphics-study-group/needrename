#include <SDL3/SDL.h>
#include <cassert>

#include "MainClass.h"
#include "Functional/SDLWindow.h"

#include "Framework/component/RenderComponent/MeshComponent.h"

using namespace Engine;

Engine::MainClass * cmc;

int main(int argc, char * argv[])
{
    assert(argc == 2);

    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt;
    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            SDL_LOG_PRIORITY_VERBOSE
    );
    SDLWindow::EnableMSAA(4);
    cmc->Initialize(&opt);
    glEnable(GL_DEPTH_TEST);
    std::shared_ptr<GameObject> go;
    std::shared_ptr<MeshComponent> testMesh = std::make_shared<MeshComponent>(go);
    testMesh->ReadAndFlatten(argv[1]);

    delete cmc;

    return 0;
}

