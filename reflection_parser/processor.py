import os
from io import StringIO
import clang.cindex as CX

from mako.template import Template
from reflection.Type import Type, Method, Field


class ReflectionParser:
    def __init__(self):
        self.types = {}
    
    
    def get_reflection_class_args(self, node: CX.Cursor):
        for child in node.get_children():
            if child.kind == CX.CursorKind.ANNOTATE_ATTR and child.spelling.startswith("%REFL_SER_CLASS"):
                return child.spelling.split()[1:]
        return None
    
    def is_reflection(self, node: CX.Cursor, mode: str = "WhiteList"):
        has_enable = False
        has_disable = False
        for child in node.get_children():
            if child.kind == CX.CursorKind.ANNOTATE_ATTR:
                if child.spelling.startswith("%REFLECTION ENABLE"):
                    has_enable = True
                if child.spelling.startswith("%REFLECTION DISABLE"):
                    has_disable = True
        if has_enable and has_disable:
            print(f"[parser] Warning: the reflection mode of {node.spelling} has both ENABLE and DISABLE. reflection will be disabled")
            return False
        if has_enable and node.kind not in [CX.CursorKind.FIELD_DECL, CX.CursorKind.CONSTRUCTOR, CX.CursorKind.CXX_METHOD]:
            print(f"[parser] Warning: the reflection mode of {node.spelling} has ENABLE but not in FIELD_DECL, CONSTRUCTOR, CXX_METHOD. reflection will be disabled")
            return False
        if node.access_specifier != CX.AccessSpecifier.PUBLIC:
            if has_enable:
                print(f"[parser] Warning: the reflection mode of {node.spelling} has ENABLE but not in PUBLIC. reflection will be disabled")
            return False
        if mode == "WhiteList":
            return has_enable
        if mode == "BlackList":
            return not has_disable
        return False
    
    def is_serialized(self, node: CX.Cursor, mode: str = "WhiteList"):
        has_enable = False
        has_disable = False
        for child in node.get_children():
            if child.kind == CX.CursorKind.ANNOTATE_ATTR:
                if child.spelling.startswith("%SERIALIZATION ENABLE"):
                    has_enable = True
                if child.spelling.startswith("%SERIALIZATION DISABLE"):
                    has_disable = True
        if has_enable and has_disable:
            print(f"[parser] Warning: the serialization mode of {node.spelling} has both ENABLE and DISABLE. serialization will be disabled")
            return False
        if has_enable and node.kind not in [CX.CursorKind.FIELD_DECL]:
            print(f"[parser] Warning: the serialization mode of {node.spelling} has ENABLE but not in FIELD_DECL. serialization will be disabled")
            return False
        if node.access_specifier != CX.AccessSpecifier.PUBLIC:
            if has_enable:
                print(f"[parser] Warning: the serialization mode of {node.spelling} has ENABLE but not in PUBLIC. serialization will be disabled")
            return False
        if mode == "WhiteList":
            return has_enable
        if mode == "BlackList":
            return not has_disable
        return False
    
    
    def get_full_name(self, node: CX.Cursor, last=True):
        if node.kind != CX.CursorKind.TRANSLATION_UNIT:
            return self.get_full_name(node.semantic_parent, False) + node.spelling + ("::" if not last else "")
        return ""
        
    def get_mangled_name(self, node: CX.Cursor):
        if node.kind != CX.CursorKind.TRANSLATION_UNIT:
            return self.get_mangled_name(node.semantic_parent) + node.spelling + str(len(node.spelling))
        return ""


    def traverse_class(self, node: CX.Cursor, args: list):
        class_name = self.get_full_name(node)
        if class_name in self.types:
            one_type = self.types[class_name]
        else:
            one_type = Type(class_name)
        one_type.mangled_name = self.get_mangled_name(node)
        
        mode = "WhiteList"
        if "BlackList" in args:
            mode = "BlackList"
        serialization_mode = "DefaultSerialization"
        if "CustomSerialization" in args:
            serialization_mode = "CustomSerialization"
        
        for child in node.get_children():
            if child.kind == CX.CursorKind.CXX_BASE_SPECIFIER:
                base_type = self.get_full_name(child.referenced)
                one_type.base_types.append(base_type)
            if not self.is_reflection(child, mode):
                continue
            # check if the class has been reflected
            # put it here to avoid taking class declaration as reflection
            if self.types.get(class_name) is not None:
                raise Exception(f"Class {class_name} has already been reflected")
            if child.kind == CX.CursorKind.FIELD_DECL:  
                field_name = child.spelling
                field_type = child.type.spelling
                field = Field(field_name)
                field.type = field_type
                one_type.fields.append(field)
            elif child.kind == CX.CursorKind.CONSTRUCTOR:
                constructor = Method(class_name)
                for constructor_child in child.get_children():
                    if constructor_child.kind == CX.CursorKind.PARM_DECL:
                        arg_type = constructor_child.type.spelling
                        constructor.arg_types.append(arg_type)
                one_type.constructors.append(constructor)
            elif child.kind == CX.CursorKind.CXX_METHOD:
                method_name = child.spelling
                method = Method(method_name)
                method.return_type = child.result_type.spelling
                method.return_type_is_reference = child.result_type.get_canonical().kind == CX.TypeKind.LVALUEREFERENCE or child.result_type.get_canonical().kind == CX.TypeKind.RVALUEREFERENCE
                method.is_const = child.is_const_method()
                for method_child in child.get_children():
                    # print(f"method: {method_name}, child: {method_child.spelling}, kind: {method_child.kind}")
                    if method_child.kind == CX.CursorKind.PARM_DECL:
                        arg_type = method_child.type.spelling
                        method.arg_types.append(arg_type)
                one_type.methods.append(method)
        
        if serialization_mode == "DefaultSerialization":
            for child in node.get_children():
                if not self.is_serialized(child, mode):
                    continue
                if child.kind == CX.CursorKind.FIELD_DECL:
                    field_name = child.spelling
                    field_type = child.type.spelling
                    field = Field(field_name)
                    field.type = field_type
                    one_type.serialized_fields.append(field)

        self.types[class_name] = one_type


    def traverse(self, node: CX.Cursor):
        args = self.get_reflection_class_args(node)
        if args is not None:
            if node.kind == CX.CursorKind.CLASS_DECL or node.kind == CX.CursorKind.STRUCT_DECL:
                self.traverse_class(node, args)
            else:
                raise Exception(f"Reflection macro can only be used in a class or struct, but found '{node.spelling}' '{node.kind}' not in a class")
        else:
            for child in node.get_children():
                self.traverse(child)


    def generate_code(self, generated_code_dir: str):
        with open("template/registrar_declare.hpp.template", "r") as f:
            template_declare = Template(f.read())
        mangled_names = [one_type.mangled_name for one_type in self.types.values()]
        with open(os.path.join(generated_code_dir, "registrar_declare.hpp"), "w") as f:
            f.write(template_declare.render(mangled_names=mangled_names))
        
        with open("template/registrar_impl.ipp.template", "r") as f:
            template_impl = Template(f.read())
        with open(os.path.join(generated_code_dir, "registrar_impl.ipp"), "w") as f:
            f.write(template_impl.render(classes_map=self.types, topological_sorted_types=self.topological_sort(self.types)))
        
        with open("template/reflection_global_template_func.tpp.template", "r") as f:
            template_rgfi = Template(f.read())
        with open(os.path.join(generated_code_dir, "reflection_global_template_func.tpp"), "w") as f:
            f.write(template_rgfi.render())
            
        with open("template/generated_reflection.tpp.template", "r") as f:
            template_gr = Template(f.read())
        with open(os.path.join(generated_code_dir, "generated_reflection.tpp"), "w") as f:
            f.write(template_gr.render())
        
        with open("template/generated_reflection.ipp.template", "r") as f:
            template_gr = Template(f.read())
        with open(os.path.join(generated_code_dir, "generated_reflection.ipp"), "w") as f:
            f.write(template_gr.render())
    
    
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
                if base_type not in idx:
                    continue
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


def process_file(path: str, generated_code_dir: str, args: str, verbose: bool = False):
    index = CX.Index.create()
    flag = CX.TranslationUnit.PARSE_INCOMPLETE \
        + CX.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES \
        + CX.TranslationUnit.PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION
    # print(f"args: {args}")
    try:
        tu = index.parse(path, args=args.split(), options=flag)
    except CX.TranslationUnitLoadError as e:
        print(f"[parser] When parsing {path}, error occurs:")
        print(e)
        raise e
    
    if verbose:
        diagnostics = tu.diagnostics
        for diag in diagnostics:
            severity = diag.severity
            message = diag.spelling
            location = diag.location
            filename = location.file.name
            if severity == CX.Diagnostic.Ignored:
                severity_str = "Ignored"
            elif severity == CX.Diagnostic.Note:
                severity_str = "Note"
            elif severity == CX.Diagnostic.Warning:
                severity_str = "Warning"
            elif severity == CX.Diagnostic.Error:
                severity_str = "Error"
            elif severity == CX.Diagnostic.Fatal:
                severity_str = "Fatal"
            else:
                severity_str = "Unknown"
            print(f"[parser] {severity_str}: '{filename}' line {location.line} column {location.column}: {message}")
    
    Parser = ReflectionParser()
    Parser.traverse(tu.cursor)
    Parser.generate_code(generated_code_dir)
