#ifndef RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED
#define RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED

#include "MaterialTemplate.h"
#include <any>

namespace Engine
{
    /// @brief An instance of a given material,
    /// where all mutable data such as texture and uniforms are stored.
    class MaterialInstance {
    
    public:
        MaterialInstance(std::weak_ptr <MaterialTemplate> tpl);

        /// @brief Set the texture uniform descriptor to point to a given texture.
        /// @param name 
        /// @param texture 
        void WriteTextureUniform(const char * name, const AllocatedImage2DTexture & texture);
    
        /// @brief Set the uniform variable descriptor to point to a given value.
        /// @param name 
        /// @param uniform 
        void WriteUBOUniform(const char * name, std::any uniform);

        /// @brief Write descriptors to GPU.
        /// Should be called before drawing.
        virtual void PushDescriptors();
    };
} // namespace Engine


#endif // RENDER_MATERIAL_MATERIALINSTANCE_INCLUDED
