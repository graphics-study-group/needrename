#include "MaterialTemplateAsset.h"
#include <MainClass.h>
#include <Render/RenderSystem.h>

namespace Engine
{
    void MaterialTemplateAsset::load_asset_from_archive(Serialization::Archive &archive)
    {
        Asset::load_asset_from_archive(archive);

        MainClass::GetInstance()->GetRenderSystem()->GetMaterialRegistry().AddMaterial(std::make_shared<AssetRef>(shared_from_this()));
    }
}
