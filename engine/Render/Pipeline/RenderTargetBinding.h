#ifndef RENDER_PIPELINE_RENDERTARGETBINDING_INCLUDED
#define RENDER_PIPELINE_RENDERTARGETBINDING_INCLUDED

#include "Render/AttachmentUtils.h"

namespace Engine {
    class RenderTargetBinding {
    public:
        using AttachmentDescription = AttachmentUtils::AttachmentDescription;

        size_t GetColorAttachmentCount() const noexcept;
        bool HasDepthAttachment() const noexcept;

        AttachmentDescription GetDepthAttachment() const noexcept;
        const std::vector <AttachmentDescription> & GetColorAttachments() const noexcept;

        void SetDepthAttachment(AttachmentDescription attachment);
        void SetColorAttachment(AttachmentDescription attachment);
        void SetColorAttachments(std::initializer_list <AttachmentDescription> attachments);
    
    private:
        std::vector <AttachmentDescription> m_color_attachments {};
        AttachmentDescription m_depth_attachment {};
    };
}

#endif // RENDER_PIPELINE_RENDERTARGETBINDING_INCLUDED
