#include "ImageCubemapAsset.h"

#include <array>
#include <assert.h>
#include <cstring>
#include <glm.hpp>
#include <gtc/constants.hpp>
#include <stb_image.h>
#include <cstring>
#include <assert.h>
#include <stb_image_write.h>

namespace {
    void sampleBilinear(const std::byte *src, int w, int h, int c, float x, float y, std::byte *out) {
        int x0 = (int)std::floor(x);
        int y0 = (int)std::floor(y);
        int x1 = (x0 + 1) % w;
        int y1 = std::min(y0 + 1, h - 1);

        float tx = x - x0;
        float ty = y - y0;

        for (int k = 0; k < c; ++k) {
            float c00 = (float)src[(y0 * w + x0) * c + k];
            float c10 = (float)src[(y0 * w + x1) * c + k];
            float c01 = (float)src[(y1 * w + x0) * c + k];
            float c11 = (float)src[(y1 * w + x1) * c + k];

            float c0 = c00 * (1 - tx) + c10 * tx;
            float c1 = c01 * (1 - tx) + c11 * tx;

            out[k] = (std::byte)(c0 * (1 - ty) + c1 * ty);
        }
    }
    void convertEquirectToCubemap(
        const std::byte *src, int srcW, int srcH, int channels, int faceSize, std::vector<std::byte> &out
    ) {
        out.resize(faceSize * faceSize * channels * 6);
        for (int face = 0; face < 6; ++face) {
            std::vector<std::byte> dst(faceSize * faceSize * channels);

            for (int y = 0; y < faceSize; ++y) {
                for (int x = 0; x < faceSize; ++x) {

                    float u = 2.0f * (x + 0.5f) / faceSize - 1.0f;
                    float v = 2.0f * (y + 0.5f) / faceSize - 1.0f;

                    glm::vec3 dir;
                    switch (face) {
                    case 0:
                        dir = {1, -v, -u};
                        break; // +X
                    case 1:
                        dir = {-1, -v, u};
                        break; // -X
                    case 2:
                        dir = {u, 1, v};
                        break; // +Y
                    case 3:
                        dir = {u, -1, -v};
                        break; // -Y
                    case 4:
                        dir = {u, -v, 1};
                        break; // +Z
                    case 5:
                        dir = {-u, -v, -1};
                        break; // -Z
                    }

                    dir = glm::normalize(dir);
                    float lon = std::atan2(dir.y, dir.x);
                    float lat = -std::asin(dir.z);

                    float srcX = (lon + glm::pi<float>()) / (2.0f * glm::pi<float>()) * srcW;
                    float srcY = (glm::pi<float>() / 2.0f - lat) / glm::pi<float>() * srcH;

                    std::byte *pixel = &dst[(y * faceSize + x) * channels];

                    sampleBilinear(src, srcW, srcH, channels, srcX, srcY, pixel);
                }
            }
            std::memcpy(&out[face * faceSize * faceSize * channels], dst.data(), dst.size());
        }
    }
    static void write_png_to_mem(void *context, void *data, int size) {
        auto &extra_data = *reinterpret_cast<std::vector<std::byte> *>(context);
        extra_data.insert(
            extra_data.end(), reinterpret_cast<std::byte *>(data), reinterpret_cast<std::byte *>(data) + size
        );
    }
} // namespace

namespace Engine {
    void ImageCubemapAsset::LoadFromFile(const std::filesystem::path &paths, int width, int height) {
        stbi_set_flip_vertically_on_load(true);
        int srcW, srcH, channels;
        auto raw_image_data = stbi_load(paths.string().c_str(), &srcW, &srcH, &channels, 4);
        assert(raw_image_data);
        m_width = width;
        m_height = height;
        m_channel = 4;
        convertEquirectToCubemap(reinterpret_cast<const std::byte *>(raw_image_data), srcW, srcH, 4, width, m_data);
        stbi_image_free(raw_image_data);
    }

    void ImageCubemapAsset::LoadFromFile(const std::array <std::filesystem::path, 6> & paths) {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        auto first_image = stbi_load(paths[0].string().c_str(), &width, &height, &channels, 4);
        assert(first_image);
        m_width = width;
        m_height = height;
        m_channel = 4;

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

    void ImageCubemapAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        auto &json = *archive.m_cursor;
        size_t extra_data_id = archive.create_new_extra_data_buffer(".png");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];

        // Create a 3x2 layout image from 6 cubemap faces
        int combined_width = m_width * 3;
        int combined_height = m_height * 2;
        std::vector<std::byte> combined_image(combined_width * combined_height * m_channel);

        // Face layout:
        // [0] [1] [2]
        // [3] [4] [5]
        const int faces[6][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}};

        for (int face = 0; face < 6; ++face) {
            int grid_x = faces[face][0];
            int grid_y = faces[face][1];

            const std::byte *src_face = m_data.data() + face * m_width * m_height * m_channel;

            for (int y = 0; y < m_height; ++y) {
                for (int x = 0; x < m_width; ++x) {
                    int dst_x = grid_x * m_width + x;
                    int dst_y = grid_y * m_height + y;

                    int src_idx = (y * m_width + x) * m_channel;
                    int dst_idx = (dst_y * combined_width + dst_x) * m_channel;

                    std::memcpy(
                        combined_image.data() + dst_idx,
                        src_face + src_idx,
                        m_channel
                    );
                }
            }
        }

        stbi_flip_vertically_on_write(false);
        stbi_write_png_to_func(write_png_to_mem, &data, combined_width, combined_height, m_channel, combined_image.data(), 0);

        TextureAsset::save_asset_to_archive(archive);
    }

    void ImageCubemapAsset::load_asset_from_archive(Serialization::Archive &archive) {
        auto &json = *archive.m_cursor;
        auto &data = archive.m_context->extra_data[json["%extra_data_id"].get<size_t>()];

        stbi_set_flip_vertically_on_load(false);
        int combined_width, combined_height, channel;
        stbi_uc *raw_image_data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc *>(data.data()), data.size(), &combined_width, &combined_height, &channel, 0
        );
        assert(raw_image_data);

        // Extract 6 cubemap faces from 3x2 layout image
        int face_width = combined_width / 3;
        int face_height = combined_height / 2;

        m_width = face_width;
        m_height = face_height;
        m_channel = channel;

        m_data.resize(face_width * face_height * channel * 6);

        const int faces[6][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}};

        for (int face = 0; face < 6; ++face) {
            int grid_x = faces[face][0];
            int grid_y = faces[face][1];

            std::byte *dst_face = m_data.data() + face * face_width * face_height * channel;

            for (int y = 0; y < face_height; ++y) {
                for (int x = 0; x < face_width; ++x) {
                    int src_x = grid_x * face_width + x;
                    int src_y = grid_y * face_height + y;

                    int src_idx = (src_y * combined_width + src_x) * channel;
                    int dst_idx = (y * face_width + x) * channel;

                    std::memcpy(
                        dst_face + dst_idx,
                        reinterpret_cast<const std::byte *>(raw_image_data) + src_idx,
                        channel
                    );
                }
            }
        }

        stbi_image_free(raw_image_data);

        TextureAsset::load_asset_from_archive(archive);
    }
} // namespace Engine
