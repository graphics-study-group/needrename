#ifndef RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED
#define RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED

#include "MaterialTemplate.h"
#include "Render/Memory/Buffer.h"
#include <any>

namespace Engine
{
    /// @brief A light-weight instance of a given material,
    /// where all mutable data such as texture and uniforms are stored.
    class MaterialInstance {
    public:
        struct PassInfo {
            Buffer ubo;
            vk::DescriptorSet desc_set;
        };

    private:
        std::weak_ptr <RenderSystem> m_system;
        std::weak_ptr <MaterialTemplate> m_parent_template;
        std::unordered_map <uint32_t, std::unordered_map<uint32_t, std::any>> m_variables {};
        std::unordered_map <uint32_t, PassInfo> m_pass_info {};
    
    public:
        MaterialInstance(std::weak_ptr <RenderSystem> system, std::shared_ptr <MaterialTemplate> tpl);

        /// @brief Set the texture uniform descriptor to point to a given texture.
        /// @param name 
        /// @param texture 
        // void WriteTextureUniform(const std::string & name, const AllocatedImage2DTexture & texture);
    
        /// @brief Set the uniform variable descriptor to point to a given value.
        /// @param pass
        /// @param index 
        /// @param uniform 
        void WriteUBOUniform(uint32_t pass, uint32_t index, std::any uniform);

        /// @brief Write descriptors to GPU.
        /// Should be called before drawing.
        virtual void PushDescriptors();
    };
} // namespace Engine


#endif // RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED
