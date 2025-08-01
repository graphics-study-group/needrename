#ifndef PIPELINE_COMMANDBUFFER_ACCESSHELPER_INCLUDED
#define PIPELINE_COMMANDBUFFER_ACCESSHELPER_INCLUDED

namespace Engine {
    namespace AccessHelper {
        enum class ImageAccessType {
            None,
            /// Read by transfer command, in transfer source layout.
            TransferRead,
            /// Read by rasterization pipeline in attachment load, blending, etc. operation, in color attachment layout.
            /// @warning Disregarded for now.
            ColorAttachmentRead,
            /// Read by rasterization pipeline in attachment load operation, in depth attachment layout.
            /// @warning Disregarded for now.
            DepthAttachmentRead,
            /// Read by shader as texture with a sampler, in read-only layout.
            ShaderRead,
            /// Write by rasterization pipeline color output stage, in color attachment layout.
            /// Implicitly sets the attachment read bit, as attachment load operations etc. read the attachment.
            ColorAttachmentWrite,
            /// Write by rasterization pipeline depth test stage, in depth attachment layout.
            /// Implicitly sets the attachment read bit, as attachment load operations etc. read the attachment.
            DepthAttachmentWrite,
            /// Write by transfer command, in transfer destination layout.
            TransferWrite,
            /// Random write in compute shaders as storage image, in general layout.
            ShaderRandomWrite,
            /// Random read and write in compute shaders as storage image, in general layout.
            ShaderReadRandomWrite
        };

        enum class ImageGraphicsAccessType {
            /// Read by rasterization pipeline in attachment load operation, in color attachment layout.
            /// @warning Disregarded for now.
            ColorAttachmentRead,
            /// Read by rasterization pipeline in attachment load operation, in depth attachment layout.
            /// @warning Disregarded for now.
            DepthAttachmentRead,
            /// Read by shader, in shader readonly layout.
            ShaderRead,
            /// Write by rasterization pipeline color output stage, in color attachment layout.
            /// Implicitly sets the attachment read bit, as attachment load operations etc. read the attachment.
            ColorAttachmentWrite,
            /// Write by rasterization pipeline depth test stage, in depth attachment layout.
            /// Implicitly sets the attachment read bit, as attachment load operations etc. read the attachment.
            DepthAttachmentWrite,
        };

        enum class ImageTransferAccessType {
            TransferRead,
            TransferWrite
        };

        enum class ImageComputeAccessType {
            /// Read by shader as texture with a sampler, in read-only layout.
            ShaderRead,
            /// Random write in compute shaders as storage image, in general layout.
            ShaderRandomWrite,
            /// Random read and write in compute shaders as storage image, in general layout.
            ShaderReadRandomWrite
        };
    }
}

#endif // PIPELINE_COMMANDBUFFER_ACCESSHELPER_INCLUDED
