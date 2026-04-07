#ifndef RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED
#define RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED

#include "Core/guid.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include <memory>
#include <unordered_map>

namespace Engine {
    class AssetRef;
    class MaterialInstance;

    namespace RenderSystemState {
        class MaterialRegistry final {
            RenderSystem &m_system;

            std::unordered_map<GUID, std::shared_ptr<MaterialLibrary>> m_libraries;
            std::unordered_map<GUID, std::shared_ptr<MaterialInstance>> m_instances;

        public:
            MaterialRegistry(RenderSystem &system) : m_system(system), m_libraries{}, m_instances{} {};

            /**
             * @brief Get or create a MaterialLibrary for the given MaterialLibraryAsset GUID.
             *
             * If the library does not exist yet, it will be created and instantiated
             * from the asset.
             */
            MaterialLibrary *GetOrCreateLibrary(GUID library_guid);

            /**
             * @brief Get or create a MaterialInstance for the given MaterialAsset GUID.
             *
             * Internally resolves the MaterialLibrary from the MaterialAsset's library reference,
             * creates both the library (if needed) and the instance, then calls Instantiate()
             * to populate uniform/texture data from the asset.
             */
            std::shared_ptr<MaterialInstance> GetOrCreateInstance(GUID material_guid);

            void GarbageCollect();
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED
