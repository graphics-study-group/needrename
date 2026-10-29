#ifndef EDITOR_ENGINE_IMPL_INSPECTOR_LIGHTCOMPONENTINSPECTOR_INCLUDED
#define EDITOR_ENGINE_IMPL_INSPECTOR_LIGHTCOMPONENTINSPECTOR_INCLUDED

#include <Editor/Inspector/ComponentInspectorBase.h>

namespace Editor {
    /**
     * @brief Inspector for `Engine::LightComponent`.
     *
     * Presents the scene's unique shadow-caster role without editing any other
     * light: the holder is labelled, a flagged light that loses the role gets a
     * one-line warning naming the holder, and an unflagged light says which light
     * casts shadows instead. The shadow settings are only editable on the holder;
     * their stored values stay on every light so they travel with the role.
     */
    class LightComponentInspector : public ComponentInspectorBase {
    public:
        void Inspect(Engine::Component &component) override;
    };
} // namespace Editor

#endif // EDITOR_ENGINE_IMPL_INSPECTOR_LIGHTCOMPONENTINSPECTOR_INCLUDED
