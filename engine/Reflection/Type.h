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

        using WrapperMemberFunc = std::function<void(void *, void *&, std::vector<void *>)>;

        class Type
        {
        public:
            static constexpr const char *constructer_name = "$Constructor";
            static std::unordered_map<std::string, std::shared_ptr<Type>> s_type_map;

        public:
            Type() = default;
            virtual ~Type() = default;

        private:
            WrapperMemberFunc FindFunction(const std::string &name);

        protected:
            friend class Registrar;

            std::string m_name{};
            std::vector<std::shared_ptr<Type>> m_base_type;
            // std::unordered_map<std::string, std::shared_ptr<Field>> m_fields {};
            std::unordered_map<std::string, WrapperMemberFunc> m_methods{};

            void setName(const std::string &name);
            void AddMethod(const std::string &name, WrapperMemberFunc method);
            void AddBaseType(std::shared_ptr<Type> base_type);

        public:
            const std::string &GetName() const;
            template <typename... Args>
            void *CreateInstance(Args... args);
            template <typename... Args>
            void *InvokeMethod(void *obj, const std::string &name, Args... args);
        };

        template <typename... Args>
        void *Type::CreateInstance(Args... args)
        {
            return InvokeMethod(nullptr, constructer_name, args...);
        }

        template <typename... Args>
        void *Type::InvokeMethod(void *obj, const std::string &name, Args... args)
        {
            WrapperMemberFunc func = FindFunction(name);
            if (!func)
                throw std::runtime_error("Method " + name + " not found");
            std::vector<void *> arg_pointers;
            (arg_pointers.push_back(reinterpret_cast<void *>(std::addressof(args))), ...);
            void *ret = nullptr;
            func(obj, ret, arg_pointers);
            return ret;
        }
    }
}

#endif // REFLECTION_TYPE_INCLUDED
