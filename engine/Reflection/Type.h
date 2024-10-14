#ifndef REFLECTION_TYPE_INCLUDED
#define REFLECTION_TYPE_INCLUDED

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Engine
{
    namespace Reflection
    {
        class Registrar;

        class Type
        {
        public:
            static std::unordered_map<std::string, std::shared_ptr<Type>> s_typeMap;

        public:
            Type() = default;
            virtual ~Type() = default;

        protected:
            friend class Registrar;

            std::string m_name{};
            // std::unordered_map<std::string, std::shared_ptr<Field>> m_fields {};
            std::unordered_map<std::string, std::function<void(void *, void *&, std::vector<void *>)>> m_methods{};

            void setName(const std::string &name);
            void AddMethod(const std::string &name, std::function<void(void *, void *&, std::vector<void *>)> method);

        public:
            const std::string &GetName() const;
            void *CreateInstance();
            template <typename T>
            T *CreateInstance();
            template <typename... Args>
            void *InvokeMethod(void *obj, const std::string &name, Args... args);
            template <typename T, typename... Args>
            T InvokeMethod(void *obj, const std::string &name, Args... args);
        };

        template <typename T>
        T *Type::CreateInstance()
        {
            return static_cast<T *>(CreateInstance());
        }

        template <typename... Args>
        void *Type::InvokeMethod(void *obj, const std::string &name, Args... args)
        {
            auto func_iter = m_methods.find(name);
            if (func_iter == m_methods.end())
                throw std::runtime_error("Method " + name + " not found");
            std::vector<void *> arg_pointers;
            (arg_pointers.push_back(reinterpret_cast<void *>(std::addressof(args))), ...);
            void *ret = nullptr;
            func_iter->second(obj, ret, arg_pointers);
            return ret;
        }

        template <typename T, typename... Args>
        T Type::InvokeMethod(void *obj, const std::string &name, Args... args)
        {
            return *static_cast<T *>(InvokeMethod(obj, name, args...));
        }
    }
}

#endif // REFLECTION_TYPE_INCLUDED
