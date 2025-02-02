#include "BlinnPhong.h"

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
} // namespace Engine::Materials
