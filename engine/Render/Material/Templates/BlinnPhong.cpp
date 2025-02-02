#include "BlinnPhong.h"
#include "Render/Memory/Image2DTexture.h"
#include <SDL3/SDL.h>

namespace Engine::Materials
{
    BlinnPhongAsset::BlinnPhongAsset() : MaterialTemplateAsset()
    {
        vs = std::make_shared<ShaderAsset>();
        fs = std::make_shared<ShaderAsset>();
        vs_ref = std::make_shared<AssetRef>(vs);
        fs_ref = std::make_shared<AssetRef>(fs);
        
        this->name = "Built-in Blinn-Phong";
        vs->filename = "shader/blinn_phong.vert";
        fs->filename = "shader/blinn_phong.frag";
        vs->shaderType = ShaderAsset::ShaderType::Vertex;
        fs->shaderType = ShaderAsset::ShaderType::Fragment;

        MaterialTemplateSinglePassProperties mtspp{};
        mtspp.shaders.shaders = std::vector<AssetRef>{*vs_ref, *fs_ref};

        ShaderVariableProperty light_source, light_color;
        light_source.frequency = light_color.frequency = ShaderVariableProperty::Frequency::PerScene;
        light_source.type = light_color.type = ShaderVariableProperty::Type::Vec4;
        light_source.binding = light_color.binding = 0;
        light_source.offset = 0;
        light_source.name = "light_source";
        light_color.offset = 16;
        light_color.name = "light_color";

        ShaderVariableProperty view, proj;
        view.frequency = proj.frequency = ShaderVariableProperty::Frequency::PerCamera;
        view.type = proj.type = ShaderVariableProperty::Type::Mat4;
        view.binding = proj.binding = 0;
        view.offset = 0;
        proj.offset = 64;
        view.name = "view";
        proj.name = "proj";

        ShaderVariableProperty base_tex, specular_color, ambient_color;
        base_tex.frequency = specular_color.frequency = ambient_color.frequency = ShaderVariableProperty::Frequency::PerMaterial;
        base_tex.type = ShaderVariableProperty::Type::Texture;
        specular_color.type = ambient_color.type = ShaderVariableProperty::Type::Vec4;
        base_tex.binding = 1;
        specular_color.binding = ambient_color.binding = 0;
        specular_color.offset = 0;
        ambient_color.offset = 16;
        base_tex.name = "base_tex";
        specular_color.name = "specular_color";
        ambient_color.name = "ambient_color";

        mtspp.shaders.uniforms = {
            light_source, light_color, view, proj, base_tex, specular_color, ambient_color
        };
        this->properties.properties[0] = mtspp;
    }

    BlinnPhongInstance::BlinnPhongInstance(
        std::weak_ptr <RenderSystem> system, 
        std::shared_ptr<MaterialTemplate> tpl
    ) : MaterialInstance(system, tpl)
    {
        texture_id = tpl->GetVariableIndex("base_tex", 0).value();
        specular_id = tpl->GetVariableIndex("specular", 0).value();
        ambient_id = tpl->GetVariableIndex("ambient", 0).value();
    }

    void BlinnPhongInstance::SetBaseTexture(const AllocatedImage2DTexture & image) {
        this->WriteTextureUniform(0, texture_id, image);
    }
    void BlinnPhongInstance::SetSpecular(glm::vec4 spec) {
        this->WriteUBOUniform(0, specular_id, spec);
    }
    void BlinnPhongInstance::SetAmbient(glm::vec4 ambi) {
        this->WriteUBOUniform(0, ambient_id, ambi);
    }

    BlinnPhongTemplate::BlinnPhongTemplate(
        std::weak_ptr <RenderSystem> system,
        std::shared_ptr <AssetRef> asset
    ) : MaterialTemplate(system, asset) {
        if (asset->as<BlinnPhongAsset>() == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Constructing Blinn-Phong material template with a mis-matched asset.");
        }
    }
    std::shared_ptr<MaterialInstance> BlinnPhongTemplate::CreateInstance()
    {
        return std::make_shared<BlinnPhongInstance>(this->m_system, this->shared_from_this());
    }
} // namespace Engine::Materials
