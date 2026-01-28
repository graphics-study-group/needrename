#include "Scene.h"
#include <Core/Delegate/Delegate.h>
#include <Core/Functional/EventQueue.h>
#include <Framework/component/RenderComponent/LightComponent.h>
#include <Framework/component/RenderComponent/RendererComponent.h>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/Type.h>
#include <Render/RenderSystem.h>

namespace Engine {
    Scene::Scene() {
        m_event_queue = std::make_unique<EventQueue>(*this);
    }

    Scene::~Scene() {
    }

    GameObject &Scene::CreateGameObject() {
        auto go_ptr = std::unique_ptr<GameObject>(new GameObject(this));
        auto handle = MainClass::GetInstance()->GetWorldSystem()->NextAvailableObjectHandle();
        auto &ret = *go_ptr;
        go_ptr->m_handle = handle;
        m_go_map[handle] = go_ptr.get();
        auto &transform_component = go_ptr->template AddComponent<TransformComponent>();
        go_ptr->m_transformComponent = transform_component.m_handle;
        m_go_add_queue.push_back(std::move(go_ptr));
        return ret;
    }

    Component &Scene::CreateComponent(GameObject &parent, const Reflection::Type &type) {
        auto comp_var = type.CreateInstance(&parent);
        return AddComponent(parent.GetHandle(), static_cast<Component *>(comp_var.GetDataPtr()));
    }

    Component &Scene::AddComponent(ObjectHandle objectHandle, Component *ptr) {
        auto comp_ptr = std::unique_ptr<Component>(static_cast<Component *>(ptr));
        comp_ptr->m_scene = this;
        comp_ptr->m_parentGameObject = objectHandle;
        auto handle = MainClass::GetInstance()->GetWorldSystem()->NextAvailableComponentHandle();
        auto &ret = *comp_ptr;
        comp_ptr->m_handle = handle;
        m_comp_map[handle] = comp_ptr.get();
        if (auto obj = this->GetGameObject(objectHandle)) {
            obj->m_components.push_back(handle);
        }
        m_comp_add_queue.push_back(std::move(comp_ptr));
        return ret;
    }

    void Scene::RemoveGameObject(ObjectHandle handle) {
        m_go_remove_queue.push_back(handle);
    }

    void Scene::RemoveComponent(ComponentHandle handle) {
        m_comp_remove_queue.push_back(handle);
    }

    void Scene::FlushCmdQueue() {
        for (auto &go_ptr : m_go_add_queue) {
            m_game_objects.push_back(std::move(go_ptr));
        }
        m_go_add_queue.clear();
        for (auto handle : m_go_remove_queue) {
            auto go_ptr = this->GetGameObject(handle);
            for (auto comp : go_ptr->m_components) {
                this->RemoveComponent(comp);
            }
            m_go_map.erase(handle);
            m_game_objects.erase(
                std::remove_if(
                    m_game_objects.begin(),
                    m_game_objects.end(),
                    [&handle](const std::unique_ptr<GameObject> &obj) { return obj->GetHandle() == handle; }
                ),
                m_game_objects.end()
            );
        }
        m_go_remove_queue.clear();

        for (auto &comp_ptr : m_comp_add_queue) {
            m_components.push_back(std::move(comp_ptr));
            m_event_queue->AddEvent(comp_ptr->GetHandle(), &Component::Init);
            // XXX: should not render init here
            auto render_comp = dynamic_cast<RendererComponent *>(comp_ptr.get());
            if (render_comp) {
                render_comp->RenderInit();
            }
        }
        m_comp_add_queue.clear();
        for (auto handle : m_comp_remove_queue) {
            m_comp_map.erase(handle);
            m_components.erase(
                std::remove_if(
                    m_components.begin(),
                    m_components.end(),
                    [&handle](const std::unique_ptr<Component> &comp) { return comp->GetHandle() == handle; }
                ),
                m_components.end()
            );
        }
        m_comp_remove_queue.clear();
    }

    void Scene::ProcessEvents() {
        m_event_queue->ProcessEvents();
    }

    void Scene::ClearEventQueue() {
        m_event_queue->Clear();
    }

    GameObject *Scene::GetGameObject(ObjectHandle handle) {
        auto it = m_go_map.find(handle);
        if (it == m_go_map.end()) {
            return nullptr;
        }
        return it->second;
    }

    Component *Scene::GetComponent(ComponentHandle handle) {
        auto it = m_comp_map.find(handle);
        if (it == m_comp_map.end()) {
            return nullptr;
        }
        return it->second;
    }

    GameObject &Scene::GetGameObjectRef(ObjectHandle handle) {
        auto it = m_go_map.find(handle);
        if (it == m_go_map.end()) {
            throw std::runtime_error("GameObject not found.");
        }
        return *it->second;
    }

    Component &Scene::GetComponentRef(ComponentHandle handle) {
        auto it = m_comp_map.find(handle);
        if (it == m_comp_map.end()) {
            throw std::runtime_error("Component not found.");
        }
        return *it->second;
    }

    const std::vector<std::unique_ptr<GameObject>> &Scene::GetGameObjects() const {
        return m_game_objects;
    }

    const std::vector<std::unique_ptr<Component>> &Scene::GetComponents() const {
        return m_components;
    }

    void Scene::AddInitEvent() {
        for (auto &comp : m_components) {
            m_event_queue->AddEvent(comp->GetHandle(), &Component::Init);
        }
    }

    void Scene::AddTickEvent() {
        for (auto &comp : m_components) {
            m_event_queue->AddEvent(comp->GetHandle(), &Component::Tick);
        }
    }
} // namespace Engine
