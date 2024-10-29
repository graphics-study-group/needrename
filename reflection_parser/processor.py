import os
from io import StringIO
import clang.cindex as CX

from mako.template import Template
from reflection.Type import Type, Method, Field

# TODO: 使用模板生成代码：根据名称获得类型Type，Type需要能过获得实例，调用构造函数
# 构造函数和成员函数等，需要使用wrapper包装，wrapper讲参数包装为void*数组，解包调用成员函数
# 使用map映射string和wrapper函数
# 成员变量为Field，实现待定
# 还需实现序列化反序列化

class ReflectionParser:
    def __init__(self):
        self.types = {}
    
    def is_reflection(self, node: CX.Cursor):
        for child in node.get_children():
            if child.kind == CX.CursorKind.ANNOTATE_ATTR and child.spelling == "reflection":
                return True
        return False

    def traverse_class(self, node: CX.Cursor):
        class_name = node.spelling
        if class_name in self.types:
            one_type = self.types[class_name]
        else:
            one_type = Type(class_name)
        for child in node.get_children():
            if child.kind == CX.CursorKind.CXX_BASE_SPECIFIER:
                base_type = child.referenced.spelling # XXX: base class ignored the namespace
                one_type.base_types.append(base_type)
            elif child.kind == CX.CursorKind.FIELD_DECL:
                if not self.is_reflection(child):
                    continue
                field_name = child.spelling
                field_type = child.type.spelling
                one_field = Field(field_name)
                one_field.type = field_type
                one_type.fields.append(one_field)
            elif child.kind == CX.CursorKind.CONSTRUCTOR:
                if not self.is_reflection(child):
                    continue
                one_constructor = Method(class_name)
                for constructor_child in child.get_children():
                    if constructor_child.kind == CX.CursorKind.PARM_DECL:
                        arg_type = constructor_child.type.spelling
                        one_constructor.arg_types.append(arg_type)
                one_type.constructors.append(one_constructor)
            elif child.kind == CX.CursorKind.CXX_METHOD:
                if not self.is_reflection(child):
                    continue
                method_name = child.spelling
                one_method = Method(method_name)
                for method_child in child.get_children():
                    if method_child.kind == CX.CursorKind.PARM_DECL:
                        arg_type = method_child.type.spelling
                        one_method.arg_types.append(arg_type)
                    elif method_child.kind == CX.CursorKind.TYPE_REF:
                        return_type = method_child.spelling
                        one_method.return_type = return_type
                one_type.methods.append(one_method)
        self.types[class_name] = one_type

    def traverse(self, node: CX.Cursor):
        if self.is_reflection(node):
            if node.kind == CX.CursorKind.CLASS_DECL or node.kind == CX.CursorKind.STRUCT_DECL:
                self.traverse_class(node)
            else:
                raise Exception(f"Reflection attribute can only be used in a class or struct, but found {node.spelling} not in a class")
        else:
            for child in node.get_children():
                self.traverse(child)

    def generate_code(self, generated_code_dir: str):
        with open("template/registrar_declare.hpp.template", "r") as f:
            template_declare = Template(f.read())
        class_names = [class_name for class_name in self.types.keys()]
        with open(os.path.join(generated_code_dir, "registrar_declare.hpp"), "w") as f:
            f.write(template_declare.render(class_names=class_names))
        with open("template/registrar_impl.ipp.template", "r") as f:
            template_impl = Template(f.read())
        with open(os.path.join(generated_code_dir, "registrar_impl.ipp"), "w") as f:
            f.write(template_impl.render(classes_map=self.types, topological_sorted_types=self.topological_sort(self.types)))
    
    def topological_sort(self, types):
        n = len(types)
        edges = [[] for _ in range(n)]
        in_degree = [0 for _ in range(n)]
        idx = {}
        types_list = list(types.values())
        for i, one_type in enumerate(types.values()):
            idx[one_type.name] = i
        for one_type in types.values():
            for base_type in one_type.base_types:
                edges[idx[base_type]].append(idx[one_type.name])
                in_degree[idx[one_type.name]] += 1
        queue = []
        for i in range(n):
            if in_degree[i] == 0:
                queue.append(i)
        result = []
        while queue:
            u = queue.pop(0)
            result.append(u)
            for v in edges[u]:
                in_degree[v] -= 1
                if in_degree[v] == 0:
                    queue.append(v)
        return [types_list[i] for i in result]


def process_file(path: str, generated_code_dir: str, args: str):
    index = CX.Index.create()
    flag = CX.TranslationUnit.PARSE_INCOMPLETE \
        + CX.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES \
        + CX.TranslationUnit.PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION
    try:
        tu = index.parse(path, args=args.split(), options=flag)
    except CX.TranslationUnitLoadError as e:
        print(f"When parsing {path}, error occurs:")
        print(e)
        raise e
    
    Parser = ReflectionParser()
    Parser.traverse(tu.cursor)
    Parser.generate_code(generated_code_dir)
