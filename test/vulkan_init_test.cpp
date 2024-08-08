#include <SDL3/SDL.h>
#include <cassert>

#include "MainClass.h"
#include "Functional/SDLWindow.h"


using namespace Engine;

Engine::MainClass * cmc;

int main(int, char **)
{
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 800, .resol_y = 600, .title = "Vulkan Test"};

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            SDL_LOG_PRIORITY_VERBOSE);
    cmc->Initialize(&opt);

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
