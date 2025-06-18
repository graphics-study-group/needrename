#ifndef ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED
#define ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED

#include "Asset/Asset.h"

namespace Engine {
    template <typename T>
    concept DerivedFromAsset = std::derived_from<T, Asset>;

    template <DerivedFromAsset T>
    class IInstantiatedFromAsset {
    public:
        virtual ~IInstantiatedFromAsset() = default;
        virtual void Instantiate(const T & asset) = 0;
    };
}

#endif // ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED
