// reflection mode: 'WhiteList', 'BlackList'. Default is 'WhiteList'
// serialization mode: 'DefaultSerialization' for automatic generating function, 'CustomSerializaion' for custom serialization. Default is 'DefaultSerialize'
#define REFL_WHITELIST "WhiteList"
#define REFL_BLACKLIST "BlackList"
#define SER_DEFAULT "DefaultSerialization"
#define SER_CUSTOM "CustomSerialization"

#define REFL_SER_CLASS(...) [[clang::annotate("%REFL_SER_CLASS " __VA_ARGS__)]]

#define REFL_ENABLE [[clang::annotate("%REFLECTION ENABLE")]]
#define REFL_DISABLE [[clang::annotate("%REFLECTION DISABLE")]]
#define SER_ENABLE [[clang::annotate("%SERIALIZATION ENABLE")]]
#define SER_DISABLE [[clang::annotate("%SERIALIZATION DISABLE")]]
#define REFL_SER_ENABLE [[clang::annotate("%REFLECTION ENABLE"), clang::annotate("%SERIALIZATION ENABLE")]]
#define REFL_SER_DISABLE [[clang::annotate("%REFLECTION DISABLE"), clang::annotate("%SERIALIZATION DISABLE")]]
