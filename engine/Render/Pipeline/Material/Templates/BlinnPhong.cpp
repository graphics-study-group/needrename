#include "BlinnPhong.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/Memory/Image2DTexture.h"
#include <SDL3/SDL.h>

namespace Engine::Materials
{
    BlinnPhongInstance::BlinnPhongInstance(
        std::weak_ptr <RenderSystem> system, 
        std::shared_ptr<MaterialTemplate> tpl
    ) : MaterialInstance(system, tpl)
    {
        texture_id = tpl->GetVariableIndex("base_tex", 0).value().first;
        specular_id = tpl->GetVariableIndex("specular_color", 0).value().first;
        ambient_id = tpl->GetVariableIndex("ambient_color", 0).value().first;
    }

    void BlinnPhongInstance::SetBaseTexture(std::shared_ptr<const AllocatedImage2DTexture> image) {
        this->WriteTextureUniform(0, texture_id, image);
    }
    void BlinnPhongInstance::SetSpecular(glm::vec4 spec) {
        this->WriteUBOUniform(0, specular_id, spec);
    }
    void BlinnPhongInstance::SetAmbient(glm::vec4 ambi) {
        this->WriteUBOUniform(0, ambient_id, ambi);
    }

    void BlinnPhongInstance::Convert(std::shared_ptr<AssetRef> asset)
    {
        const auto & material_asset = asset->cas<MaterialAsset>();

        // We currently only care about base texture and specular and ambient vectors
        // Load texture asset
        const auto & base_texture_prop = material_asset->m_properties.at("base_tex");
        assert(base_texture_prop.m_type == MaterialProperty::Type::Texture);
        auto base_texture_asset = (std::any_cast<std::shared_ptr<AssetRef>>(base_texture_prop.m_value))->as<Image2DTextureAsset>();
        auto base_texture = std::make_shared<AllocatedImage2DTexture>(m_system);
        base_texture->Create(*base_texture_asset);
        this->SetBaseTexture(base_texture);
        m_system.lock()->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
            *base_texture, 
            base_texture_asset->GetPixelData(), 
            base_texture_asset->GetPixelDataSize()
        );

        // Load specular and ambient vectors
        auto itr = material_asset->m_properties.find("specular_color");
        glm::vec4 specular_prop = 
            itr == material_asset->m_properties.end() ? 
            glm::vec4{0.0f, 0.0f, 0.0f, 0.0f} : 
            std::any_cast<glm::vec4>(itr->second.m_value);

        itr = material_asset->m_properties.find("ambient_color");
        glm::vec4 ambient_prop = 
            itr == material_asset->m_properties.end() ?
            glm::vec4{0.0f, 0.0f, 0.0f, 0.0f} :
            std::any_cast<glm::vec4>(itr->second.m_value);
        this->SetSpecular(specular_prop);
        this->SetAmbient(ambient_prop);
    }

    BlinnPhongTemplate::BlinnPhongTemplate(
        std::weak_ptr <RenderSystem> system,
        std::shared_ptr <AssetRef> asset
    ) : MaterialTemplate(system, asset) {
        // if (asset->as<BlinnPhongTemplateAsset>() == nullptr) {
        //     SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Constructing Blinn-Phong material template with a mis-matched asset.");
        // }
    }
    std::shared_ptr<MaterialInstance> BlinnPhongTemplate::CreateInstance()
    {
        return std::make_shared<BlinnPhongInstance>(this->m_system, this->shared_from_this());
    }
} // namespace Engine::Materials
