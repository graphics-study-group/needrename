#ifndef RENDER_RESOURCE_BUILTINRESOURCEPROVIDERS_INCLUDED
#define RENDER_RESOURCE_BUILTINRESOURCEPROVIDERS_INCLUDED

#include "Render/Resource/RenderResourceHub.h"

namespace Engine {
    class MaterialLibrary;
    class MaterialInstance;

    namespace RenderSystemState {
        class StaticMeshResource;

        class MaterialLibraryProvider final : public IResourceProvider {
        public:
            const std::type_info &GetResourceType() const noexcept override;
            std::shared_ptr<void> Create(
                RenderSystem &system, RenderResourceHub &hub, uint64_t dependency_owner, const GUID &guid
            ) override;
            const char *GetDebugName() const noexcept override;
        };

        class MaterialInstanceProvider final : public IResourceProvider {
        public:
            const std::type_info &GetResourceType() const noexcept override;
            std::shared_ptr<void> Create(
                RenderSystem &system, RenderResourceHub &hub, uint64_t dependency_owner, const GUID &guid
            ) override;
            void Destroy(
                RenderSystem &system,
                RenderResourceHub &hub,
                uint64_t dependency_owner,
                const GUID &guid,
                std::shared_ptr<void> &resource
            ) override;
            const char *GetDebugName() const noexcept override;
        };

        class StaticMeshProvider final : public IResourceProvider {
        public:
            const std::type_info &GetResourceType() const noexcept override;
            std::shared_ptr<void> Create(
                RenderSystem &system, RenderResourceHub &hub, uint64_t dependency_owner, const GUID &guid
            ) override;
            const char *GetDebugName() const noexcept override;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_BUILTINRESOURCEPROVIDERS_INCLUDED
