#include "AssetRef.h"
#include <Asset/Asset.h>
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>
#include <Reflection/serialization.h>

namespace Engine {
    AssetRef::AssetRef() {
    }

    AssetRef::AssetRef(GUID guid) : m_guid(guid) {
    }

    AssetRef::AssetRef(const Asset *asset) : m_guid(asset->GetGUID()) {
    }

    AssetRef::AssetRef(const AssetRef &other) : m_guid(other.m_guid) {
    }

    AssetRef &AssetRef::operator=(const AssetRef &other) {
        m_guid = other.m_guid;
        return *this;
    }

    AssetRef::~AssetRef() {
        Release();
    }

    void AssetRef::save_to_archive(Serialization::Archive &archive) const {
        Serialization::Json &json = *archive.m_cursor;
        if (m_guid.IsValid()) {
            json = m_guid.toString();
        } else {
            json = nullptr;
        }
    }

    void AssetRef::load_from_archive(Serialization::Archive &archive) {
        Serialization::Json &json = *archive.m_cursor;
        if (json.is_null()) {
            m_guid.SetZero();
        } else {
            m_guid = GUID(json.get<std::string>());
        }
    }

    void AssetRef::Acquire() {
        if (IsValid() && !IsAcquired()) {
            auto &amg = *MainClass::GetInstance()->GetAssetManager();
            if (!amg.IsAssetLoaded(m_guid)) {
                amg.LoadAssetImmediately(m_guid);
            }
            m_is_acquired = true;
            amg.IncrementRefCount(m_guid);
        }
    }

    void AssetRef::AcquireAsync() {
        if (IsValid() && !IsAcquired()) {
            auto &amg = *MainClass::GetInstance()->GetAssetManager();
            if (!amg.IsAssetLoaded(m_guid)) {
                amg.AddToLoadingQueue(m_guid);
            }
            m_is_acquired = true;
            amg.IncrementRefCount(m_guid);
        }
    }

    void AssetRef::Release() {
        if (IsAcquired()) {
            m_is_acquired = false;
            auto cmc = MainClass::GetInstance();
            if (cmc) {
                auto amg = cmc->GetAssetManager();
                if (amg) {
                    amg->DecrementRefCount(m_guid);
                }
            }
        }
    }

    bool AssetRef::IsAcquired() const {
        return m_is_acquired;
    }

    bool AssetRef::IsValid() const {
        return m_guid.IsValid();
    }

    GUID AssetRef::GetGUID() const {
        return m_guid;
    }

    Asset *AssetRef::TryGetAsset() const {
        if (!IsAcquired()) return nullptr;
        auto &amg = *MainClass::GetInstance()->GetAssetManager();
        return amg.GetAsset(m_guid);
    }

} // namespace Engine
