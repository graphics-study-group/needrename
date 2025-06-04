#include <SDL3/SDL.h>
#include <iostream>

#include "MainClass.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Render/FullRenderSystem.h"
#include <cmake_config.h>

using namespace Engine;

int main(int argc, char * argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Compute Shader Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto asys = cmc->GetAssetManager();
    asys->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    asys->LoadBuiltinAssets();
    auto cs_ref = asys->GetNewAssetRef("~/shaders/test_compute.comp.spv.asset");
    asys->LoadAssetImmediately(cs_ref);

    auto cs = cs_ref->cas<ShaderAsset>();
    auto ret = ShaderUtils::ReflectSpirvDataCompute(cs->binary);

    assert(ret.desc.names.find("outputImage") != ret.desc.names.end() && ret.desc.names["outputImage"] == 0);
    assert(ret.desc.vars[0].set == 0 
        && ret.desc.vars[0].binding == 1 
        && ret.desc.vars[0].type == ShaderVariableProperty::Type::StorageImage
    );

    return 0;
}
