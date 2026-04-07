#include "MaterialRegistry.h"

#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Material/MaterialLibraryAsset.h"
#include "MainClass.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/Templates/BlinnPhong.h"

namespace Engine::RenderSystemState {
    MaterialLibrary *MaterialRegistry::GetOrCreateLibrary(GUID library_guid) {
        auto it = m_libraries.find(library_guid);
        if (it != m_libraries.end()) {
            return it->second.get();
        }

        auto am = MainClass::GetInstance()->GetAssetManager();
        AssetRef ref(library_guid);
        ref.Acquire();
        auto *asset = ref.as<MaterialLibraryAsset>();
        assert(asset);

        auto ptr = std::make_shared<MaterialLibrary>(m_system);
        ptr->Instantiate(*asset);
        m_libraries[library_guid] = ptr;
        return ptr.get();
    }

    std::shared_ptr<MaterialInstance> MaterialRegistry::GetOrCreateInstance(GUID material_guid) {
        auto it = m_instances.find(material_guid);
        if (it != m_instances.end()) {
            return it->second;
        }

        auto am = MainClass::GetInstance()->GetAssetManager();
        AssetRef mat_ref(material_guid);
        mat_ref.Acquire();
        auto *mat_asset = mat_ref.as<MaterialAsset>();
        assert(mat_asset);

        GUID library_guid = mat_asset->m_library.GetGUID();
        GetOrCreateLibrary(library_guid);

        auto inst = std::make_shared<MaterialInstance>(m_system, *m_libraries[library_guid]);
        inst->Instantiate(*mat_asset);
        m_instances[material_guid] = inst;
        return inst;
    }

    void MaterialRegistry::GarbageCollect() {
        for (auto it = m_instances.begin(); it != m_instances.end();) {
            if (it->second.use_count() <= 1) {
                it = m_instances.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = m_libraries.begin(); it != m_libraries.end();) {
            if (it->second.use_count() <= 1) {
                it = m_libraries.erase(it);
            } else {
                ++it;
            }
        }
    }
} // namespace Engine::RenderSystemState
