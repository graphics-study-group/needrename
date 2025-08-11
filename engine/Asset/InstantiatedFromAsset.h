#ifndef ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED
#define ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED

#include "Asset/AssetRef.h"

namespace Engine {
    template <class T>
    class IInstantiatedFromAsset {
    public:
        virtual ~IInstantiatedFromAsset() = default;
        virtual void Instantiate(const T &asset) = 0;
    };
} // namespace Engine

#endif // ENGINE_ASSET_INSTANTIATEDFROMASSET_INCLUDED
