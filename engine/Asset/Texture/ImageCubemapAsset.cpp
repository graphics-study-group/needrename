#include "ImageCubemapAsset.h"

#include <array>
#include <stb_image.h>
#include <cstring>
#include <assert.h>

namespace Engine {
    void ImageCubemapAsset::LoadFromFile(const std::array <std::filesystem::path, 6> & paths) {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        auto first_image = stbi_load(paths[0].string().c_str(), &width, &height, &channels, 4);
        assert(first_image);

        auto image_size = width * height * 4;
        m_data.resize(image_size * 6);
        std::memcpy(m_data.data(), first_image, image_size);
        stbi_image_free(first_image);

        for (int i = 1; i < 6; i++) {
            int nw, nh, nc;
            stbi_set_flip_vertically_on_load(true);
            auto image = stbi_load(paths[i].string().c_str(), &nw, &nh, &nc, 4);
            assert(image);
            assert(nw == width && nh == height && nc == channels);

            std::memcpy(m_data.data() + image_size * i, image, image_size);
            stbi_image_free(image);
        }
    }
    const std::byte * ImageCubemapAsset::GetPixelData() const {
        return m_data.data();
    }
    size_t ImageCubemapAsset::GetPixelDataSize() const {
        return m_data.size();
    }
} // namespace Engine
