import os
from io import StringIO
import clang.cindex as CX

from mako.template import Template
from reflection.Type import Type

# TODO: 使用模板生成代码：根据名称获得类型Type，Type需要能过获得实例，调用构造函数
# 构造函数和成员函数等，需要使用wrapper包装，wrapper讲参数包装为void*数组，解包调用成员函数
# 使用map映射string和wrapper函数
# 成员变量为Field，实现待定
# 还需实现序列化反序列化

class ReflectionParser:
    def __init__(self):
        self.types = []

    def traverse(self, node: CX.Cursor, parent=None, current_class=None):
        if node.kind == CX.CursorKind.ANNOTATE_ATTR and node.spelling == "reflection":
            assert parent is not None
            if parent.kind == CX.CursorKind.CLASS_DECL or parent.kind == CX.CursorKind.STRUCT_DECL:
                print(f"Class: {parent.spelling}")
                assert current_class is None
                current_class = Type(parent.spelling)
                self.types.append(current_class)

            elif parent.kind == CX.CursorKind.FIELD_DECL:
                print(f"Field: {parent.spelling}")
            elif parent.kind == CX.CursorKind.CXX_METHOD:
                print(f"Method: {parent.spelling}")
            else:
                raise Exception(f"Unsupported reflection type: {str(parent.kind)}")
                
        children = list(node.get_children())
        for child in children:
            self.traverse(child, node, current_class)

    def generate_code(self, generated_code_dir: str):
        with open("template/registrar_declare.hpp.template", "r") as f:
            template_declare = Template(f.read())
        class_names = [one_type.name for one_type in self.types]
        with open(os.path.join(generated_code_dir, "registrar_declare.hpp"), "w") as f:
            f.write(template_declare.render(class_names=class_names))


def process_file(path: str, generated_code_dir: str):
    index = CX.Index.create()
    flag = CX.TranslationUnit.PARSE_INCOMPLETE \
        + CX.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES \
        + CX.TranslationUnit.PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION
    try:
        tu = index.parse(path, args=['-std=c++20'], options=flag)
    except CX.TranslationUnitLoadError as e:
        print(f"When parsing {path}, error occurs:")
        print(e)
        raise e
    
    Parser = ReflectionParser()
    Parser.traverse(tu.cursor)
    Parser.generate_code(generated_code_dir)
