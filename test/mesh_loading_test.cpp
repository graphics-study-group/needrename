#include <SDL3/SDL.h>
#include <cassert>

#include "MainClass.h"
#include "Functional/SDLWindow.h"

#include "Framework/component/RenderComponent/MeshComponent.h"

using namespace Engine;

Engine::MainClass * cmc;

int main(int argc, char * argv[])
{
    SDL_Init(SDL_INIT_VIDEO);
    
    if(argc < 2) {
        SDL_LogCritical(0, "Mesh not provided.");
        return -1;
    }
    SDL_LogInfo(0, "Loading mesh %s", argv[1]);

    StartupOptions opt;
    opt.resol_x = 800;
    opt.resol_y = 600;
    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            SDL_LOG_PRIORITY_VERBOSE
    );
    cmc->Initialize(&opt);

    std::shared_ptr<GameObject> go;
    std::shared_ptr<MeshComponent> testMesh = std::make_shared<MeshComponent>(go);

    std::filesystem::path mesh {argv[1]};
    mesh = mesh / "mesh.obj";
    if(!testMesh->ReadAndFlatten(mesh))
        return -1;

    delete cmc;
    return 0;
}

