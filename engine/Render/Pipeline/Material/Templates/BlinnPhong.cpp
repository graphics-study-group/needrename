#include "BlinnPhong.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Render/Memory/ImageTexture.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include <SDL3/SDL.h>

namespace Engine::Materials {
    BlinnPhongInstance::BlinnPhongInstance(RenderSystem &system, std::shared_ptr<MaterialTemplate> tpl) :
        MaterialInstance(system, tpl) {
    }
    void BlinnPhongInstance::SetBaseTexture(std::shared_ptr<const Texture> image) {
        this->AssignTexture("base_tex", *image);
    }
    void BlinnPhongInstance::SetSpecular(glm::vec4 spec) {
        this->AssignSimpleVariables("specular_color", spec);
    }
    void BlinnPhongInstance::SetAmbient(glm::vec4 ambi) {
        this->AssignSimpleVariables("ambient_color", ambi);
    }

    void BlinnPhongInstance::Instantiate(const MaterialAsset &asset) {
        // We currently only care about base texture and specular and ambient vectors
        // Load texture asset
        const auto &base_texture_prop = asset.m_properties.at("base_tex");
        assert(base_texture_prop.m_type == MaterialProperty::Type::Texture);
        auto base_texture_asset =
            (std::any_cast<std::shared_ptr<AssetRef>>(base_texture_prop.m_value))->as<Image2DTextureAsset>();
        std::shared_ptr base_texture = ImageTexture::CreateUnique(m_system, *base_texture_asset);
        this->SetBaseTexture(base_texture);
        m_system.GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
            *base_texture, base_texture_asset->GetPixelData(), base_texture_asset->GetPixelDataSize()
        );

        // Load specular and ambient vectors
        auto itr = asset.m_properties.find("specular_color");
        glm::vec4 specular_prop = itr == asset.m_properties.end() ? glm::vec4{0.0f, 0.0f, 0.0f, 0.0f}
                                                                  : std::any_cast<glm::vec4>(itr->second.m_value);

        itr = asset.m_properties.find("ambient_color");
        glm::vec4 ambient_prop = itr == asset.m_properties.end() ? glm::vec4{0.0f, 0.0f, 0.0f, 0.0f}
                                                                 : std::any_cast<glm::vec4>(itr->second.m_value);
        this->SetSpecular(specular_prop);
        this->SetAmbient(ambient_prop);
    }

    BlinnPhongTemplate::BlinnPhongTemplate(RenderSystem &system) : MaterialTemplate(system) {
    }
    std::shared_ptr<MaterialInstance> BlinnPhongTemplate::CreateInstance() {
        return std::make_shared<BlinnPhongInstance>(this->m_system, this->shared_from_this());
    }
} // namespace Engine::Materials
