#include "Archive.h"
#include <Reflection/reflection.h>

namespace Engine
{
    namespace Serialization
    {
        Archive::Archive(const Archive &other, Json *cursor)
            : m_context(other.m_context), m_cursor(cursor)
        {
        }

        void Archive::init(const std::string &archive_type_name, const void *main_data)
        {
            if (m_context->initialized)
                throw std::runtime_error("Archive already initialized");
            m_context->json["%archive_type"] = archive_type_name;
            m_context->json["%data"] = Json::object();
            m_context->json["%extra_data"] = Json::array();
            m_context->json["%main_id"] = "&-1";
            if (main_data)
            {
                m_context->id_map[(AddressID)main_data] = m_context->current_id++;
                std::string main_id = std::string("&") + std::to_string(m_context->id_map[(AddressID)main_data]);
                m_context->json["%main_id"] = main_id;
                m_context->json["%data"][main_id] = Json::object();
                m_cursor = &m_context->json["%data"][main_id];
            }
            m_context->initialized = true;
        }

        void Archive::load_init(void *main_data)
        {
            std::string str_id = m_context->json["%main_id"].get<std::string>();
            int id = std::stoi(str_id.substr(1));
            m_context->pointer_map[id] = main_data;
            m_cursor = &m_context->json["%data"][str_id];
        }

        void Archive::clear()
        {
            m_context->json.clear();
            m_context->extra_datas.clear();
            m_context->id_map.clear();
            m_context->current_id = 0;
            m_context->initialized = false;

            m_cursor = nullptr;
        }

        void Archive::save_to_file(const std::filesystem::path &path)
        {
            throw std::runtime_error("Not implemented");
        }
    }
}
