#include <SDL3/SDL.h>
#include <cassert>

#include "consts.h"
#include "MainClass.h"
#include "Functional/SDLWindow.h"

#include "Render/SinglePassMaterial.h"
#include "Render/RenderSystem.h"
#include "Framework/go/GameObject.h"
#include "Framework/component/RenderComponent/MeshComponent.h"

using namespace Engine;

Engine::MainClass * cmc;

const char vert [] = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 uv;
out vec3 clip_space_coordinate;
out vec2 vert_uv;
void main() {
    gl_Position = vec4(aPos.x, aPos.y - 1.0, aPos.z, 1.0);
    clip_space_coordinate = aPos;
    vert_uv = uv;
}
)";

const char frag [] = R"(
#version 330 core
in vec3 clip_space_coordinate;
in vec2 vert_uv;
out vec4 albedo;
void main() {
    albedo = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

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

    std::shared_ptr <GameObject> go = std::make_shared<GameObject>();
    std::shared_ptr <ShaderPass> pass = std::make_shared <ShaderPass> ();
    assert(pass->Compile(vert, frag));
    std::shared_ptr <Material> mat = std::make_shared<SinglePassMaterial>(cmc->renderer, pass);
    std::shared_ptr <MeshComponent> testMesh = 
        std::make_shared<MeshComponent>(mat, go);
    testMesh->ReadAndFlatten("D:/test.obj");
    
    cmc->renderer->RegisterComponent(testMesh);

    cmc->MainLoop();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
