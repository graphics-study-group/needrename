#ifndef RENDER_RENDERER_IVERTEXBASEDRENDERER_INCLUDED
#define RENDER_RENDERER_IVERTEXBASEDRENDERER_INCLUDED

#include <cstdint>
#include <vector>

namespace Engine {
    class VertexAttribute;
    class DeviceBuffer;

    /**
     * @brief A base interface class for all vertex-based renderers.
     * 
     * Supplies necessary interfaces for vertex based draw calls.
     */
    class IVertexBasedRenderer {
    public:
        struct BufferBindingInfo {
            const DeviceBuffer * buffer;
            size_t offset;
            // Size may be unused currently, as draw calls specifies vertex
            // and index counts.
            size_t size;
        };

        IVertexBasedRenderer() = default;
        virtual ~IVertexBasedRenderer() = default;

        /**
         * @brief Get the count of indices (i.e. actual number of
         * vertices drawn) of this renderer.
         */
        virtual uint32_t GetIndexCount() const noexcept = 0;

        /**
         * @brief Get the count of vertex attributes of this renderer.
         */
        virtual uint32_t GetVertexAttributeCount() const noexcept = 0;

        /**
         * @brief Query how the vertex attributes are arranged in
         * the vertex attribute buffer.
         */
        virtual VertexAttribute GetVertexAttributeFormat() const noexcept = 0;

        /**
         * @brief Fetch vertex attribute buffer info for draw calls.
         * 
         * The binding info is explained as follows:
         * the n-th item of this vector corresponds to the n-th used vertex
         * attribute slots, regardless of the actual location specified by
         * `layout(location = N)` in the shader.
         * 
         * @see VertexAttribute::ToVkVertexInputBinding() const noexcept
         * etc. for more details on how pipeline vertex input is constructed.
         */
        virtual void FillVertexAttributeBufferBindings(
            std::vector <BufferBindingInfo> & bindings
        ) const noexcept = 0;

        /**
         * @brief Get vertex attribute buffer info for draw calls.
         */
        std::vector <BufferBindingInfo> GetVertexAttributeBufferBindings() const noexcept {
            std::vector <BufferBindingInfo> ret;
            FillVertexAttributeBufferBindings(ret);
            return ret;
        };

        /**
         * @brief Get index buffer info for draw calls.
         */
        virtual BufferBindingInfo GetIndexBufferBinding() const noexcept = 0;
    };
}

#endif // RENDER_RENDERER_IVERTEXBASEDRENDERER_INCLUDED
