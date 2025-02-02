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
            std::unique_ptr<Buffer> ubo {};
            vk::DescriptorSet desc_set {};
            bool is_dirty {false};
        };

    private:
        std::weak_ptr <RenderSystem> m_system;
        std::weak_ptr <MaterialTemplate> m_parent_template;
        std::unordered_map <uint32_t, std::unordered_map<uint32_t, std::any>> m_variables {};
        std::unordered_map <uint32_t, PassInfo> m_pass_info {};

        // A small buffer for uniform buffer staging to avoid random write to UBO.
        std::vector <std::byte> m_buffer {};
    
    public:
        MaterialInstance(std::weak_ptr <RenderSystem> system, std::shared_ptr <MaterialTemplate> tpl);
        
        /**
         * @brief Get the variables of a specific pass
         *
         * @param pass_index The index of the pass
         * @return A reference to the variables map for the specified pass
         */
        auto GetVariables(uint32_t pass_index) const -> const decltype(m_variables.at(0)) &;

        /**
         * @brief Set the texture uniform descriptor to point to a given texture.
         * @param name 
         * @param texture 
         * 
         * TODO: Figure out a way to connect samplers with textures
         */
        void WriteTextureUniform(uint32_t pass, uint32_t index, const ImageInterface & texture);
    
        /**
         * @brief Set the uniform variable descriptor to point to a given value.
         *
         * This function sets the uniform variable descriptor for a specific pass and index.
         * The provided uniform value is stored in the material instance's variables map.
         *
         * @param pass  The index of the pass to which the uniform belongs.
         * @param index The index of the uniform within the pass.
         * @param uniform The uniform value to set, wrapped in a std::any object.
         */
        void WriteUBOUniform(uint32_t pass, uint32_t index, std::any uniform);

        
        /**
         * @brief Write out pending descriptor changes to the descriptor set.
         * Writes uniform buffers to update uniform variables, and calls vk::WriteDescriptorSet to upload.
         * 
         * @param pass The index of the pass.
         */
        void WriteDescriptors(uint32_t pass);
    };
} // namespace Engine


#endif // RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED
