#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGERCOMPONENT_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGERCOMPONENT_INCLUDED

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        class IFrameManagerComponent {
        protected:
            RenderSystem & m_system;

        public:
            IFrameManagerComponent(RenderSystem & system) : m_system(system) {}
            virtual ~IFrameManagerComponent() = 0;

            virtual void OnFrameStart() {}
            virtual void OnFrameComplete() {}

            virtual void OnPreMainCbSubmission() {}
            virtual void OnPostMainCbSubmission() {}
        };

        inline IFrameManagerComponent::~IFrameManagerComponent() { }
    }
}

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGERCOMPONENT_INCLUDED
