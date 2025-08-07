#include "RenderTargetBinding.h"
#include <cassert>

namespace Engine {
    using AttachmentDescription = AttachmentUtils::AttachmentDescription;

    size_t RenderTargetBinding::GetColorAttachmentCount() const noexcept {
        return m_color_attachments.size();
    }
    bool RenderTargetBinding::HasDepthAttachment() const noexcept {
        return (m_depth_attachment.texture != nullptr);
    }
    AttachmentDescription RenderTargetBinding::GetDepthAttachment() const noexcept {
        return m_depth_attachment;
    }

    const std::vector<AttachmentDescription> &RenderTargetBinding::GetColorAttachments() const noexcept {
        return m_color_attachments;
    }
    void RenderTargetBinding::SetDepthAttachment(AttachmentDescription attachment) {
        assert(attachment.texture != nullptr);
        m_depth_attachment = attachment;
    }
    void RenderTargetBinding::SetColorAttachment(AttachmentDescription attachment) {
        assert(attachment.texture != nullptr);
        m_color_attachments.resize(1);
        m_color_attachments[0] = attachment;
    }
    void RenderTargetBinding::SetColorAttachments(std::initializer_list<AttachmentDescription> attachments) {
        for (const auto &att : attachments) {
            assert(att.texture != nullptr);
        }
        m_color_attachments = std::vector(attachments);
    }
}; // namespace Engine
