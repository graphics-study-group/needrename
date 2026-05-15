#ifndef ASSET_LOADER_IMPORTTYPES_INCLUDED
#define ASSET_LOADER_IMPORTTYPES_INCLUDED

#include <Asset/AssetRef.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace Engine {
    /**
     * @brief Runtime output bundle produced by one model import operation.
     */
    struct ImportResult {
        /// Primary mesh asset generated from imported model geometry.
        AssetRef mesh_asset{};

        /// Material refs aligned with mesh submesh order, used for direct component binding.
        std::vector<AssetRef> mesh_material_assets{};

        /// Newly created material assets during this import (deduplicated by source material index).
        std::vector<AssetRef> created_material_assets{};

        /// Newly created texture assets during this import (including fallback generated textures).
        std::vector<AssetRef> created_texture_assets{};

        /// Optional scene asset that contains mesh object and imported light objects when enabled.
        std::optional<AssetRef> scene_asset{};

        /// Number of successfully imported lights written into the generated scene payload.
        uint32_t imported_light_count{0};
    };
} // namespace Engine

#endif // ASSET_LOADER_IMPORTTYPES_INCLUDED
