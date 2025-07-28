#include <SDL3/SDL.h>
#include <cassert>

#include "Functional/SDLWindow.h"
#include "MainClass.h"

using namespace Engine;

int main(int argc, char *argv[]) {
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions *opt = ParseOptions(argc, argv);
    if (opt->instantQuit) return -1;

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(opt, SDL_INIT_VIDEO, opt->enableVerbose ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO, 0u);
    cmc->MainLoop();

    delete opt;

    return 0;
}
