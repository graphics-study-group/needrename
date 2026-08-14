#include "AssetPath.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <vector>

namespace {
    std::string NormalizeScheme(std::string_view scheme) {
        std::string result(scheme);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
    }

    std::string NormalizeSubpath(std::string_view subpath) {
        std::vector<std::string> segments;
        size_t start = 0;
        while (true) {
            const size_t pos = subpath.find('/', start);
            const std::string_view segment =
                (pos == std::string_view::npos) ? subpath.substr(start) : subpath.substr(start, pos - start);
            if (!segment.empty() && segment != ".") {
                if (segment == "..") {
                    if (!segments.empty()) {
                        segments.pop_back();
                    }
                } else {
                    segments.emplace_back(segment);
                }
            }
            if (pos == std::string_view::npos) {
                break;
            }
            start = pos + 1;
        }
        std::string result;
        for (const auto &segment : segments) {
            if (!result.empty()) {
                result += '/';
            }
            result += segment;
        }
        return result;
    }

    std::string BuildValue(std::string_view scheme, std::string_view subpath) {
        return NormalizeScheme(scheme) + "://" + NormalizeSubpath(subpath);
    }
} // namespace

namespace Engine {
    AssetPath::AssetPath(std::string_view path) {
        if (path.empty()) {
            return;
        }
        const size_t pos = path.find("://");
        if (pos == std::string_view::npos) {
            throw std::invalid_argument("AssetPath: missing \"://\" scheme separator");
        }
        m_value = BuildValue(path.substr(0, pos), path.substr(pos + 3));
    }

    AssetPath::AssetPath(std::string_view scheme, std::string_view subpath) {
        m_value = BuildValue(scheme, subpath);
    }

    std::size_t AssetPath::Hash::operator()(const AssetPath &p) const {
        return std::hash<std::string>{}(p.m_value);
    }

    std::string_view AssetPath::GetScheme() const {
        const size_t pos = m_value.find("://");
        if (pos == std::string::npos) {
            return {};
        }
        return std::string_view(m_value).substr(0, pos);
    }

    std::filesystem::path AssetPath::GetSubPath() const {
        const size_t pos = m_value.find("://");
        if (pos == std::string::npos) {
            return {};
        }
        return std::filesystem::path(m_value.substr(pos + 3));
    }

    bool AssetPath::IsEmpty() const {
        return m_value.empty();
    }

    AssetPath AssetPath::parent_path() const {
        const size_t pos = m_value.find("://");
        if (pos == std::string::npos) {
            return *this;
        }
        const std::string_view subpath(m_value.data() + pos + 3, m_value.size() - pos - 3);
        const size_t slash = subpath.find_last_of('/');
        if (slash == std::string_view::npos) {
            return AssetPath(std::string_view(m_value).substr(0, pos + 3));
        }
        return AssetPath(std::string_view(m_value).substr(0, pos), std::string_view(subpath).substr(0, slash));
    }

    std::string AssetPath::filename() const {
        const size_t pos = m_value.find("://");
        if (pos == std::string::npos) {
            return {};
        }
        const std::string_view subpath(m_value.data() + pos + 3, m_value.size() - pos - 3);
        const size_t slash = subpath.find_last_of('/');
        if (slash == std::string_view::npos) {
            return std::string(subpath);
        }
        return std::string(subpath.substr(slash + 1));
    }

    AssetPath AssetPath::operator/(std::string_view child) const {
        if (m_value.empty() || child.empty()) {
            return *this;
        }
        const size_t pos = m_value.find("://");
        if (pos == std::string::npos) {
            return AssetPath(child);
        }
        const std::string scheme(m_value.substr(0, pos));
        const std::string subpath(m_value.substr(pos + 3));
        std::string joined = subpath;
        if (!joined.empty()) {
            joined += '/';
        }
        joined += child;
        return AssetPath(scheme, joined);
    }

    const std::string &AssetPath::ToString() const {
        return m_value;
    }

    const std::string &AssetPath::generic_string() const {
        return m_value;
    }
} // namespace Engine
