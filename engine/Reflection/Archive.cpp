#include "Archive.h"
#include <Reflection/reflection.h>
#include <fstream>

namespace Engine {
    namespace Serialization {
        Archive::Archive(const Archive &other, Json *cursor) : m_context(other.m_context), m_cursor(cursor) {
        }

        void Archive::prepare_save() {
            if (m_context->save_prepared) throw std::runtime_error("Archive already initialized");
            m_context->json["%data"] = Json::object();
            m_context->json["%extra_data"] = Json::array();
            std::string main_id;
            main_id = std::string("&") + std::to_string(m_context->current_id);
            m_context->current_id++;
            m_context->json["%main_id"] = main_id;
            m_context->json["%data"][main_id] = Json::object();
            m_cursor = &m_context->json["%data"][main_id];
            m_context->save_prepared = true;
        }

        void Archive::prepare_load() {
            std::string str_id = m_context->json["%main_id"].get<std::string>();
            m_cursor = &m_context->json["%data"][str_id];
            m_context->load_prepared = true;
        }

        void Archive::clear() {
            m_context->json.clear();
            m_context->extra_data.clear();
            m_context->id_map.clear();
            m_context->pointer_map.clear();
            m_context->current_id = 0;
            m_context->save_prepared = false;
            m_context->load_prepared = false;

            m_cursor = nullptr;
        }

        size_t Archive::create_new_extra_data_buffer(const std::string &extension) {
            m_context->extra_data.emplace_back();
            auto &buffer_meta = m_context->json["%extra_data"].emplace_back();
            buffer_meta["%extension"] = extension;
            return m_context->extra_data.size() - 1;
        }

        void Archive::save_to_file(std::filesystem::path path) {
            std::filesystem::path json_path = path;
            json_path.replace_filename(json_path.filename().string() + ".asset");

            std::ofstream json_file(json_path);
            if (json_file.is_open()) {
                json_file << m_context->json.dump(4) << std::endl;
                json_file.close();
            } else {
                throw std::runtime_error("Failed to open json file");
            }

            for (size_t i = 0; i < m_context->extra_data.size(); ++i) {
                auto &data = m_context->extra_data[i];
                std::string ext = m_context->json["%extra_data"][i]["%extension"].get<std::string>();
                std::ofstream extra_data_file(path.string() + "." + std::to_string(i) + ext, std::ios::binary);
                if (extra_data_file.is_open()) {
                    extra_data_file.write(reinterpret_cast<const char *>(data.data()), data.size());
                    extra_data_file.close();
                } else {
                    throw std::runtime_error("Failed to open extra data file");
                }
            }
        }

        void Archive::load_from_file(std::filesystem::path path) {
            clear();
            std::filesystem::path json_path = path;
            json_path.replace_filename(json_path.filename().string() + ".asset");
            std::ifstream json_file(json_path);
            if (json_file.is_open()) {
                json_file >> m_context->json;
                json_file.close();
            } else {
                throw std::runtime_error("Failed to open .asset file");
            }

            m_context->extra_data.resize(m_context->json["%extra_data"].size());
            for (size_t i = 0; i < m_context->extra_data.size(); ++i) {
                auto &data = m_context->extra_data[i];
                std::string ext = m_context->json["%extra_data"][i]["%extension"].get<std::string>();
                std::ifstream extra_data_file(path.string() + "." + std::to_string(i) + ext, std::ios::binary);
                if (extra_data_file.is_open()) {
                    extra_data_file.seekg(0, std::ios::end);
                    size_t size = extra_data_file.tellg();
                    extra_data_file.seekg(0, std::ios::beg);
                    data.resize(size);
                    extra_data_file.read(reinterpret_cast<char *>(data.data()), size);
                    extra_data_file.close();
                } else {
                    throw std::runtime_error("Failed to open extra data file");
                }
            }
        }
    } // namespace Serialization
} // namespace Engine
