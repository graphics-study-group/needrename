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

    std::array<SDL_Vertex, 3> origin_vertices = {
        SDL_Vertex{{150, 100}, {1.0f, 0.0f, 0.0f, 1.0f}}, // top
        SDL_Vertex{{000, 300}, {0.0f, 1.0f, 0.0f, 1.0f}}, // left bottom
        SDL_Vertex{{300, 300}, {0.0f, 0.0f, 1.0f, 1.0f}}  // right bottom
    };

    SDL_Event event{};
    bool keep_going = true;

    while (keep_going)
    {
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
            {
                keep_going = false;
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            {
                keep_going = keep_going && (event.key.key != SDLK_ESCAPE);
                break;
            }
            }
            // SDL_Log("Event: %d", event.type);
        }
        SDL_RenderGeometry(renderer, nullptr, origin_vertices.data(), origin_vertices.size(), nullptr, 0);
        // SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
