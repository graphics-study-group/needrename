#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <array>

int main(int argc, char *argv[])
{
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    SDL_Window *window =
        SDL_CreateWindow("Hello, SDL3!", 800, 600, SDL_WINDOW_BORDERLESS);
    if (!window)
    {
        SDL_Log("Could not create a window: %s", SDL_GetError());
        return -1;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    int count = SDL_GetNumRenderDrivers();
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetRenderDriver(i);
        SDL_Log("Render driver[%d]: %s", i, name);
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, "opengl");
    if (!renderer)
    {
        SDL_Log("Create renderer failed: %s", SDL_GetError());
        return -1;
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
