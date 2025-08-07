#include "serialization_glm.h"

namespace Engine {
    namespace Serialization {
        void save_to_archive(const glm::vec2 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            json = Json::array();
            json.push_back(value.x);
            json.push_back(value.y);
        }

        void load_from_archive(glm::vec2 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            value.x = json[0].get<float>();
            value.y = json[1].get<float>();
        }

        void save_to_archive(const glm::vec3 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            json = Json::array();
            json.push_back(value.x);
            json.push_back(value.y);
            json.push_back(value.z);
        }

        void load_from_archive(glm::vec3 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            value.x = json[0].get<float>();
            value.y = json[1].get<float>();
            value.z = json[2].get<float>();
        }

        void save_to_archive(const glm::vec4 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            json = Json::array();
            json.push_back(value.x);
            json.push_back(value.y);
            json.push_back(value.z);
            json.push_back(value.w);
        }

        void load_from_archive(glm::vec4 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            value.x = json[0].get<float>();
            value.y = json[1].get<float>();
            value.z = json[2].get<float>();
            value.w = json[3].get<float>();
        }

        void save_to_archive(const glm::quat &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            json = Json::array();
            json.push_back(value.x);
            json.push_back(value.y);
            json.push_back(value.z);
            json.push_back(value.w);
        }

        void load_from_archive(glm::quat &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            value.x = json[0].get<float>();
            value.y = json[1].get<float>();
            value.z = json[2].get<float>();
            value.w = json[3].get<float>();
        }

        void save_to_archive(const glm::mat3 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            json = Json::array();
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    json.push_back(value[i][j]);
                }
            }
        }

        void load_from_archive(glm::mat3 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            int index = 0;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    value[i][j] = json[index++].get<float>();
                }
            }
        }

        void save_to_archive(const glm::mat4 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            json = Json::array();
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    json.push_back(value[i][j]);
                }
            }
        }

        void load_from_archive(glm::mat4 &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            int index = 0;
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    value[i][j] = json[index++].get<float>();
                }
            }
        }
    } // namespace Serialization
} // namespace Engine
