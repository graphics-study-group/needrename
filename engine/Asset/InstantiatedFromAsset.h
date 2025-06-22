#ifndef ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED
#define ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED

#include "Asset/Asset.h"
#include "Asset/AssetRef.h"

namespace Engine {
    template <typename T>
    concept DerivedFromAsset = std::derived_from<T, Asset>;

    template <DerivedFromAsset T>
    class IInstantiatedFromAsset {
    public:
        virtual ~IInstantiatedFromAsset() = default;
        virtual void Instantiate(const T & asset) = 0;

        virtual void InstantiateFromRef(std::shared_ptr <AssetRef> asset_ref) 
        {
            std::shared_ptr <const T> asset = asset_ref->cas<T>();
            this->Instantiate(*asset);
        }
    };
}

#endif // ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED
