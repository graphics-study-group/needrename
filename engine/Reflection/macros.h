#ifndef REFLECTION_MACROS_INCLUDED
#define REFLECTION_MACROS_INCLUDED

// reflection mode: 'WhiteList', 'BlackList'. Default is 'WhiteList'
// serialization mode: 'DefaultSerialization' for automatic generating function, 'CustomSerializaion' for custom
// serialization. Default is 'DefaultSerialize'
#define REFL_WHITELIST "WhiteList"
#define REFL_BLACKLIST "BlackList"
#define SER_DEFAULT "DefaultSerialization"
#define SER_CUSTOM "CustomSerialization"

#if defined(__clang__)
#define REFL_SER_CLASS(...) [[clang::annotate("%REFL_SER_CLASS " __VA_ARGS__)]]
#define REFL_ENABLE [[clang::annotate("%REFLECTION ENABLE")]]
#define REFL_DISABLE [[clang::annotate("%REFLECTION DISABLE")]]
#define SER_ENABLE [[clang::annotate("%SERIALIZATION ENABLE")]]
#define SER_DISABLE [[clang::annotate("%SERIALIZATION DISABLE")]]
#define REFL_SER_ENABLE [[clang::annotate("%REFLECTION ENABLE"), clang::annotate("%SERIALIZATION ENABLE")]]
#define REFL_SER_DISABLE [[clang::annotate("%REFLECTION DISABLE"), clang::annotate("%SERIALIZATION DISABLE")]]
#else
#define REFL_SER_CLASS(...)
#define REFL_ENABLE
#define REFL_DISABLE
#define SER_ENABLE
#define SER_DISABLE
#define REFL_SER_ENABLE
#define REFL_SER_DISABLE
#endif

/// Serialization body for regular class. Declare some virtual serialization functions and backdoor constructor.
#define REFL_SER_BODY(class_name, ...)                                                                                 \
public:                                                                                                                \
    friend class Engine::Reflection::Registrar;                                                                        \
    REFL_DISABLE virtual void _SERIALIZATION_SAVE_(Engine::Serialization::Archive &buffer) const;                      \
    REFL_DISABLE virtual void _SERIALIZATION_LOAD_(Engine::Serialization::Archive &buffer);                            \
    REFL_ENABLE class_name(Engine::Serialization::SerializationMarker marker);

/// Serialization body for simple struct. Declare non-virtual serialization functions, backdoor constructor and default
/// constructor.
#define REFL_SER_SIMPLE_STRUCT(class_name, ...)                                                                        \
public:                                                                                                                \
    friend class Engine::Reflection::Registrar;                                                                        \
    REFL_DISABLE void _SERIALIZATION_SAVE_(Engine::Serialization::Archive &buffer) const;                              \
    REFL_DISABLE void _SERIALIZATION_LOAD_(Engine::Serialization::Archive &buffer);                                    \
    REFL_ENABLE class_name(Engine::Serialization::SerializationMarker marker);                                         \
    REFL_ENABLE class_name() = default;

namespace Engine {
    namespace Reflection {
        class Registrar;
    }

    namespace Serialization {
        class Archive;
        struct SerializationMarker;
    } // namespace Serialization
} // namespace Engine

#endif // REFLECTION_MACROS_INCLUDED
