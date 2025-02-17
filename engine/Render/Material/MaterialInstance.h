#ifndef RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED
#define RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED

#include "MaterialTemplate.h"
#include "Render/Memory/Buffer.h"
#include <any>

namespace Engine
{
    class TransferCommandBuffer;
    class ImageInterface;
    /// @brief A light-weight instance of a given material,
    /// where all mutable data such as texture and uniforms are stored.
    class MaterialInstance {
    public:
        struct PassInfo {
            // FIXME: We are only allocating one buffer for multiple frames-in-flight. This might lead to synchronization problems.
            std::unique_ptr<Buffer> ubo {};
            vk::DescriptorSet desc_set {};
            bool is_ubo_dirty {false};
            bool is_descriptor_set_dirty {false};
        };

    protected:
        std::weak_ptr <RenderSystem> m_system;
        std::weak_ptr <MaterialTemplate> m_parent_template;
        std::unordered_map <uint32_t, std::unordered_map<uint32_t, std::any>> m_variables {};
        std::unordered_map <uint32_t, PassInfo> m_pass_info {};

        // A small buffer for uniform buffer staging to avoid random write to UBO.
        std::vector <std::byte> m_buffer {};
    
    public:
        MaterialInstance(std::weak_ptr <RenderSystem> system, std::shared_ptr <MaterialTemplate> tpl);
        virtual ~MaterialInstance() = default;

        /**
         * @brief Get the template associated with this material instance.
         * 
         * This method returns a constant reference to the `MaterialTemplate` object
         * that was used to create this material instance. The template provides information
         * about the material's structure and configuration, including its passes,
         * bindings, and other properties.
         * 
         * @return A constant reference to the `MaterialTemplate` associated with this material instance.
         */ 
        const MaterialTemplate & GetTemplate() const;
        
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
        void WriteTextureUniform(uint32_t pass, uint32_t index, std::shared_ptr<const ImageInterface> texture);
    
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
         * @brief Write out UBO changes if it is dirty.
         */
        void WriteUBO(uint32_t pass);
        
        /**
         * @brief Write out pending descriptor changes to the descriptor set.
         * Calls vk::WriteDescriptorSet to upload.
         * 
         * @param pass The index of the pass.
         */
        void WriteDescriptors(uint32_t pass);

        /**
         * @brief Get the descriptor set for a specific pass
         *
         * This method returns the descriptor set associated with the given pass index.
         * The descriptor set contains the bindings for various resources such as textures and uniform buffers.
         *
         * @param pass_index The index of the pass
         * @return A reference to the `vk::DescriptorSet` object associated with the specified pass
         */
        vk::DescriptorSet GetDescriptor(uint32_t pass) const;

        /**
         * @brief Covert a material asset to the material instance. Load properties to the uniforms.
         * 
         * @param asset The MaterialAsset to convert.
         * @param tcb 
         */
        virtual void Convert(std::shared_ptr <AssetRef> asset, TransferCommandBuffer & tcb);
    };
} // namespace Engine


#endif // RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED
