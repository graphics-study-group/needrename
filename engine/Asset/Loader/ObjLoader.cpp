#include "ObjLoader.h"

#include "ImportSharedUtil.h"
#include "TextureImportUtils.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Scene/SceneAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <SDL3/SDL.h>
#include <tiny_obj_loader.h>

#include <any>
#include <map>
#include <tuple>
#include <unordered_map>

namespace Engine {
    namespace {
        struct ParsedObjData {
            tinyobj::attrib_t attrib{};
            std::vector<tinyobj::shape_t> shapes{};
            std::vector<tinyobj::material_t> materials{};
        };

        glm::vec4 BuildFallbackTangentFromNormal(const glm::vec3 &normal) {
            if (glm::length(normal) <= 1e-6f) {
                return glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
            }

            const glm::vec3 n = glm::normalize(normal);
            const float abs_nz = n.z >= 0.0f ? n.z : -n.z;
            const glm::vec3 reference = abs_nz < 0.999f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};

            glm::vec3 tangent = glm::cross(reference, n);
            if (glm::length(tangent) <= 1e-6f) {
                tangent = glm::vec3{1.0f, 0.0f, 0.0f};
            } else {
                tangent = glm::normalize(tangent);
            }

            return glm::vec4{tangent, 1.0f};
        }

        void LoadMeshAssetFromTinyObj(
            MeshAsset &mesh_asset, const tinyobj::attrib_t &attrib, const std::vector<tinyobj::shape_t> &shapes
        );
        void LoadMaterialAssetFromTinyObj(
            MaterialAsset &material_asset,
            const tinyobj::material_t &material,
            const std::filesystem::path &base_path,
            const std::weak_ptr<AssetManager> &asset_manager,
            const std::weak_ptr<FileSystemDatabase> &database
        );

        ParsedObjData ParseAndSplitObjByMaterial(const std::filesystem::path &path) {
            tinyobj::ObjReaderConfig reader_config{};
            tinyobj::ObjReader reader{};

            if (!reader.ParseFromFile(path.string(), reader_config)) {
                throw std::runtime_error("Cannot load OBJ file: " + path.string() + ", " + reader.Error());
            }
            if (!reader.Warning().empty()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TinyObjLoader warning: %s", reader.Warning().c_str());
            }

            ParsedObjData parsed{};
            parsed.attrib = reader.GetAttrib();
            const auto &origin_shapes = reader.GetShapes();
            const auto &origin_materials = reader.GetMaterials();

            for (const auto &shape : origin_shapes) {
                std::map<int, tinyobj::shape_t> material_id_map;
                int shape_id = 0;
                for (size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face) {
                    const int material_id = shape.mesh.material_ids[face];
                    if (!material_id_map.contains(material_id)) {
                        material_id_map[material_id] = tinyobj::shape_t{
                            .name = shape.name + "_" + std::to_string(shape_id++),
                            .mesh = tinyobj::mesh_t{},
                            .lines = tinyobj::lines_t{},
                            .points = tinyobj::points_t{}
                        };
                    }

                    auto &new_shape = material_id_map[material_id];
                    const unsigned int face_vertex_count = shape.mesh.num_face_vertices[face];
                    if (face_vertex_count != 3) {
                        throw std::runtime_error("OBJ contains non-triangle face. Only triangulated OBJ is supported.");
                    }

                    new_shape.mesh.num_face_vertices.push_back(face_vertex_count);
                    new_shape.mesh.material_ids.push_back(material_id);
                    new_shape.mesh.smoothing_group_ids.push_back(shape.mesh.smoothing_group_ids[face]);
                    for (unsigned int i = 0; i < face_vertex_count; ++i) {
                        new_shape.mesh.indices.push_back(shape.mesh.indices[face * 3 + i]);
                    }
                }

                for (const auto &[id, new_shape] : material_id_map) {
                    parsed.shapes.push_back(new_shape);
                    if (id < 0) {
                        continue;
                    }
                    if (static_cast<size_t>(id) >= origin_materials.size()) {
                        continue;
                    }
                    parsed.materials.push_back(origin_materials[id]);
                    std::fill(
                        parsed.shapes.back().mesh.material_ids.begin(),
                        parsed.shapes.back().mesh.material_ids.end(),
                        static_cast<int>(parsed.materials.size() - 1)
                    );
                }
            }

            return parsed;
        }
    } // namespace

    ObjLoader::ObjLoader() {
        m_asset_manager = MainClass::GetInstance()->GetAssetManager();
        m_database = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
        if (m_asset_manager.expired() || m_database.expired()) {
            throw std::runtime_error("ObjLoader requires initialized AssetManager and FileSystemDatabase.");
        }
    }

    ImportResult ObjLoader::LoadObjInMemory(const std::filesystem::path &path) {
        auto am = m_asset_manager.lock();
        auto db = m_database.lock();
        if (!am || !db) {
            throw std::runtime_error("ObjLoader dependencies are not available.");
        }

        ParsedObjData parsed = ParseAndSplitObjByMaterial(path);
        ImportResult result{};

        auto *mesh_asset = am->CreateAsset<MeshAsset>();
        mesh_asset->m_name = detail::import_shared::SanitizeAssetName(path.stem().string());
        LoadMeshAssetFromTinyObj(*mesh_asset, parsed.attrib, parsed.shapes);
        result.mesh_asset = AssetRef(mesh_asset);

        std::vector<AssetRef> material_refs;
        material_refs.reserve(parsed.materials.size());

        for (size_t i = 0; i < parsed.materials.size(); ++i) {
            auto *material_asset = am->CreateAsset<MaterialAsset>();
            LoadMaterialAssetFromTinyObj(
                *material_asset, parsed.materials[i], path.parent_path(), m_asset_manager, m_database
            );
            if (material_asset->m_name.empty()) {
                material_asset->m_name = mesh_asset->m_name + "_material_" + std::to_string(i);
            }
            AssetRef material_ref(material_asset);
            material_refs.push_back(material_ref);
            result.created_material_assets.push_back(material_ref);

            for (const auto &[_, property] : material_asset->m_properties) {
                if (property.m_type != MaterialProperty::Type::Texture) {
                    continue;
                }
                const AssetRef texture_ref = std::any_cast<AssetRef>(property.m_value);
                if (texture_ref.IsValid()) {
                    result.created_texture_assets.push_back(texture_ref);
                }
            }
        }

        const AssetRef fallback_material =
            db->GetNewAssetRef(AssetPath(*db, std::filesystem::path("~/materials/solid_color_dark_grey.asset")));
        result.mesh_material_assets.reserve(parsed.shapes.size());
        for (const auto &shape : parsed.shapes) {
            int material_id = -1;
            if (!shape.mesh.material_ids.empty()) {
                material_id = shape.mesh.material_ids.front();
            }
            if (material_id >= 0 && static_cast<size_t>(material_id) < material_refs.size()) {
                result.mesh_material_assets.push_back(material_refs[material_id]);
            } else {
                result.mesh_material_assets.push_back(fallback_material);
            }
        }

        return result;
    }

    void ObjLoader::LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project) {
        auto db = m_database.lock();
        if (!db) {
            throw std::runtime_error("ObjLoader database is not available.");
        }

        ImportResult imported = LoadObjInMemory(path);
        if (!imported.mesh_asset.IsValid()) {
            throw std::runtime_error("ObjLoader failed to create mesh asset.");
        }

        auto *mesh_asset = imported.mesh_asset.as<MeshAsset>();
        detail::import_shared::SaveAsset(*db, *mesh_asset, path_in_project, mesh_asset->m_name);

        for (auto material_ref : imported.created_material_assets) {
            auto *material = material_ref.as<MaterialAsset>();
            detail::import_shared::SaveAsset(*db, *material, path_in_project, material->m_name);
        }

        for (auto texture_ref : imported.created_texture_assets) {
            auto *texture = texture_ref.as<TextureAsset>();
            detail::import_shared::SaveAsset(*db, *texture, path_in_project, texture->m_name);
        }

        auto &temp_scene = MainClass::GetInstance()->GetWorldSystem()->CreateScene();
        auto &go = temp_scene.CreateGameObject();
        go.m_name = mesh_asset->m_name;
        auto &mesh_component = go.AddComponent<StaticMeshComponent>();
        mesh_component.m_mesh_asset = imported.mesh_asset;
        mesh_component.m_material_assets = imported.mesh_material_assets;
        temp_scene.FlushCmdQueue();

        auto scene_asset = std::make_unique<SceneAsset>();
        scene_asset->SaveFromScene(temp_scene);
        const std::string scene_name = "GO_" + mesh_asset->m_name;
        detail::import_shared::SaveAsset(*db, *scene_asset, path_in_project, scene_name);
    }

    namespace {
        void LoadMeshAssetFromTinyObj(
            MeshAsset &mesh_asset, const tinyobj::attrib_t &attrib, const std::vector<tinyobj::shape_t> &shapes
        ) {
            mesh_asset.m_submeshes.clear();

            const auto &positions = attrib.vertices;
            const auto &normals = attrib.normals;
            const auto &uvs = attrib.texcoords;
            const auto &colors = attrib.colors;

            for (const auto &shape : shapes) {
                mesh_asset.m_submeshes.emplace_back();
                auto &submesh = mesh_asset.m_submeshes.back();
                uint32_t vertex_id = 0;
                std::map<std::tuple<int, int, int>, uint32_t> vertex_id_map;

                std::vector<float> position{};
                std::vector<float> color{};
                std::vector<float> normal{};
                std::vector<float> tangent{};
                std::vector<float> texcoord0{};
                submesh.tangent.type = VertexAttributeType::SFloat32x4;

                for (const auto &index : shape.mesh.indices) {
                    const std::tuple<int, int, int> key(index.vertex_index, index.normal_index, index.texcoord_index);
                    if (!vertex_id_map.contains(key)) {
                        vertex_id_map[key] = vertex_id++;
                        submesh.positions.type = VertexAttributeType::SFloat32x3;
                        position.push_back(positions[index.vertex_index * 3]);
                        position.push_back(positions[index.vertex_index * 3 + 1]);
                        position.push_back(positions[index.vertex_index * 3 + 2]);

                        if (!colors.empty()) {
                            submesh.color.type = VertexAttributeType::SFloat32x3;
                            color.push_back(colors[index.vertex_index * 3]);
                            color.push_back(colors[index.vertex_index * 3 + 1]);
                            color.push_back(colors[index.vertex_index * 3 + 2]);
                        }
                        if (index.normal_index >= 0) {
                            submesh.normal.type = VertexAttributeType::SFloat32x3;
                            const glm::vec3 n{
                                normals[index.normal_index * 3],
                                normals[index.normal_index * 3 + 1],
                                normals[index.normal_index * 3 + 2]
                            };
                            normal.push_back(n.x);
                            normal.push_back(n.y);
                            normal.push_back(n.z);

                            const glm::vec4 fallback_tangent = BuildFallbackTangentFromNormal(n);
                            tangent.push_back(fallback_tangent.x);
                            tangent.push_back(fallback_tangent.y);
                            tangent.push_back(fallback_tangent.z);
                            tangent.push_back(fallback_tangent.w);
                        } else {
                            const glm::vec4 fallback_tangent = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
                            tangent.push_back(fallback_tangent.x);
                            tangent.push_back(fallback_tangent.y);
                            tangent.push_back(fallback_tangent.z);
                            tangent.push_back(fallback_tangent.w);
                        }
                        if (index.texcoord_index >= 0) {
                            submesh.texcoord0.type = VertexAttributeType::SFloat32x2;
                            texcoord0.push_back(uvs[index.texcoord_index * 2]);
                            texcoord0.push_back(1.0f - uvs[index.texcoord_index * 2 + 1]); // Obj needs v flipped
                        }
                    }
                    submesh.m_indices.push_back(vertex_id_map[key]);
                }

                submesh.vertex_count = vertex_id;
                submesh.m_vertex_attributes.reserve(
                    (position.size() + color.size() + normal.size() + texcoord0.size() + tangent.size()) * sizeof(float)
                );
                detail::import_shared::AppendVertexAttribute(
                    submesh.m_vertex_attributes, position.data(), position.size(), submesh.positions
                );
                detail::import_shared::AppendVertexAttribute(
                    submesh.m_vertex_attributes, color.data(), color.size(), submesh.color
                );
                detail::import_shared::AppendVertexAttribute(
                    submesh.m_vertex_attributes, normal.data(), normal.size(), submesh.normal
                );
                detail::import_shared::AppendVertexAttribute(
                    submesh.m_vertex_attributes, texcoord0.data(), texcoord0.size(), submesh.texcoord0
                );
                detail::import_shared::AppendVertexAttribute(
                    submesh.m_vertex_attributes, tangent.data(), tangent.size(), submesh.tangent
                );
            }
        }

        void LoadMaterialAssetFromTinyObj(
            MaterialAsset &material_asset,
            const tinyobj::material_t &material,
            const std::filesystem::path &base_path,
            const std::weak_ptr<AssetManager> &asset_manager,
            const std::weak_ptr<FileSystemDatabase> &database
        ) {
            auto db = database.lock();
            auto am = asset_manager.lock();
            if (!db || !am) {
                throw std::runtime_error("ObjLoader dependencies are not available.");
            }

            material_asset.m_name = material.name.empty() ? "material" : material.name;
            material_asset.m_library =
                db->GetNewAssetRef(AssetPath(*db, std::filesystem::path("~/material_libraries/BlinnPhongLibrary.asset"))
                );

            auto load_texture =
                [&](const std::string &texture_name, const std::string &suffix, const char *property_name) {
                    if (texture_name.empty()) {
                        return;
                    }
                    auto *texture = am->CreateAsset<Image2DTextureAsset>();
                    detail::texture_import::LoadImage2DTextureAssetFromFile(
                        *texture, base_path / texture_name, ImageUtils::ImageFormat::R8G8B8A8SRGB
                    );
                    texture->m_name = detail::import_shared::SanitizeAssetName(material_asset.m_name + suffix);
                    material_asset.m_properties[property_name] =
                        MaterialProperty(AssetRef(texture), MaterialProperty::Type::Texture);
                };

            switch (material.illum) {
            case 2:
                material_asset.m_properties["ambient_color"] =
                    glm::vec4{material.ambient[0], material.ambient[1], material.ambient[2], 1.0f};
                material_asset.m_properties["specular_color"] = glm::vec4{
                    material.specular[0],
                    material.specular[1],
                    material.specular[2],
                    static_cast<float>(material.shininess)
                };
                load_texture(material.diffuse_texname, "_tex_diffuse", "base_tex");
                break;
            default:
                material_asset.m_properties["ambient"] =
                    glm::vec4{material.ambient[0], material.ambient[1], material.ambient[2], 1.0f};
                material_asset.m_properties["diffuse"] =
                    glm::vec4{material.diffuse[0], material.diffuse[1], material.diffuse[2], 1.0f};
                material_asset.m_properties["specular"] =
                    glm::vec4{material.specular[0], material.specular[1], material.specular[2], 1.0f};
                material_asset.m_properties["transmittance"] =
                    glm::vec4{material.transmittance[0], material.transmittance[1], material.transmittance[2], 1.0f};
                material_asset.m_properties["emission"] =
                    glm::vec4{material.emission[0], material.emission[1], material.emission[2], 1.0f};
                material_asset.m_properties["shininess"] = static_cast<float>(material.shininess);
                material_asset.m_properties["ior"] = static_cast<float>(material.ior);
                material_asset.m_properties["dissolve"] = static_cast<float>(material.dissolve);

                load_texture(material.ambient_texname, "_tex_ambient", "ambient_texture");
                load_texture(material.diffuse_texname, "_tex_diffuse", "diffuse_texture");
                load_texture(material.specular_texname, "_tex_specular", "specular_texture");
                load_texture(
                    material.specular_highlight_texname, "_tex_specular_highlight", "specular_highlight_texture"
                );
                load_texture(material.bump_texname, "_tex_bump", "bump_texture");
                load_texture(material.displacement_texname, "_tex_displacement", "displacement_texture");
                load_texture(material.alpha_texname, "_tex_alpha", "alpha_texture");
                load_texture(material.roughness_texname, "_tex_roughness", "roughness_texture");
                load_texture(material.metallic_texname, "_tex_metallic", "metallic_texture");
                load_texture(material.sheen_texname, "_tex_sheen", "sheen_texture");
                load_texture(material.emissive_texname, "_tex_emissive", "emissive_texture");
                load_texture(material.normal_texname, "_tex_normal", "normal_texture");
                break;
            }
        }
    } // namespace
} // namespace Engine
