import os
from io import StringIO
import clang.cindex as CX
from pathlib import Path
from mako.template import Template
from reflection.Type import Type, Method, Field


class ReflectionParser:
    def __init__(self):
        self.types = {}
        self.files = []
    
    
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


    def traverse_class(self, node: CX.Cursor, args: list):
        current_type = Type(node.type)
        # check if the class has been parsed
        # some clang bug may cause the same class to be parsed multiple times
        if self.types.get(current_type.full_name) is not None:
            return
        
        mode = "WhiteList"
        if "BlackList" in args:
            mode = "BlackList"
        serialization_mode = "DefaultSerialization"
        if "CustomSerialization" in args:
            serialization_mode = "CustomSerialization"
            
        # a marker to indicate if the class is parsed
        # it should not be parsed when the node is a forward declaration
        flag = False
        
        for child in node.get_children():
            # record base types
            if child.kind == CX.CursorKind.CXX_BASE_SPECIFIER:
                current_type.base_types.append(Type(child.type))
            # ignore inner classes
            if child.kind == CX.CursorKind.STRUCT_DECL or child.kind == CX.CursorKind.CLASS_DECL:
                continue
            # check if the field, constructor, or method should be reflected
            if not self.is_reflection(child, mode):
                continue
            # if the node is not a forward declaration, it must have one of the following kinds
            # because every reflected class has a backdoor constructor in REFL_SER_BODY() macro
            if child.kind == CX.CursorKind.FIELD_DECL:
                current_type.fields.append(Field(child))
                flag = True
            elif child.kind == CX.CursorKind.CONSTRUCTOR:
                current_type.constructors.append(Method(child))
                flag = True
            elif child.kind == CX.CursorKind.CXX_METHOD:
                current_type.methods.append(Method(child))
                flag = True
        
        if serialization_mode == "DefaultSerialization":
            for child in node.get_children():
                if not self.is_serialized(child, mode):
                    continue
                if child.kind == CX.CursorKind.FIELD_DECL:
                    current_type.serialized_fields.append(Field(child))
                    flag = True

        if flag:
            self.types[current_type.full_name] = current_type
        return


    def traverse(self, node: CX.Cursor):
        if node.location.file is not None:
            path = str(Path(node.location.file.name).resolve())
            if path not in self.files:
                return
        args = self.get_reflection_class_args(node)
        if args is not None:
            if node.kind == CX.CursorKind.CLASS_DECL or node.kind == CX.CursorKind.STRUCT_DECL:
                self.traverse_class(node, args)
            else:
                raise Exception(f"Reflection macro can only be used in a class or struct, but found '{node.spelling}' '{node.kind}' not in a class")
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
            f.write(template_impl.render(classes_map=self.types))
            
        topological_sorted_types=self.topological_sort(self.types)
        with open("template/reflection_init.ipp.template", "r") as f:
            template_init = Template(f.read())
        with open(os.path.join(generated_code_dir, "reflection_init.ipp"), "w") as f:
            f.write(template_init.render(classes_map=self.types, topological_sorted_types=topological_sorted_types))
        
        with open("template/generated_reflection.cpp.template", "r") as f:
            template_gr = Template(f.read())
        with open(os.path.join(generated_code_dir, "generated_reflection.cpp"), "w") as f:
            f.write(template_gr.render())
        
        with open("template/serialization_impl.ipp.template", "r") as f:
            template_gs_ipp = Template(f.read())
        with open(os.path.join(generated_code_dir, "serialization_impl.ipp"), "w") as f:
            f.write(template_gs_ipp.render(classes_map=self.types))
            
        with open("template/reflection.hpp.template", "r") as f:
            template_reflection = Template(f.read())
        with open(os.path.join(generated_code_dir, "reflection.hpp"), "w") as f:
            f.write(template_reflection.render())
    
    
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


def process_file(all_header_file_path: str, files: list, generated_code_dir: str, args: str, verbose: bool = False):
    index = CX.Index.create()
    flag = CX.TranslationUnit.PARSE_INCOMPLETE \
        + CX.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES \
        + CX.TranslationUnit.PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION
    try:
        tu = index.parse(all_header_file_path, args=args.split(), options=flag)
    except CX.TranslationUnitLoadError as e:
        print(f"[parser] When parsing {all_header_file_path} , error occurs:")
        print(e)
        raise e
    
    print(f"Processing translation unit {tu.spelling}")
    
    print("Generated diagnostics for this translation unit:")

    warning_count, error_count, fatal_count = 0, 0, 0
    for diag in tu.diagnostics:
        severity = diag.severity
        message = diag.spelling
        location = diag.location
        filename = location.file.name if location.file else "stdin"
        printed = False

        if severity == CX.Diagnostic.Ignored:
            severity_str = "Ignored"
            printed = verbose
        elif severity == CX.Diagnostic.Note:
            severity_str = "Note"
            printed = verbose
        elif severity == CX.Diagnostic.Warning:
            severity_str = "Warning"
            warning_count += 1
            printed = True
        elif severity == CX.Diagnostic.Error:
            severity_str = "Error"
            error_count += 1
            printed = True
        elif severity == CX.Diagnostic.Fatal:
            severity_str = "Fatal"
            fatal_count += 1
            printed = True
        else:
            raise KeyError(f"Unknown diagnostic type: {message}.")
        
        if printed:
            print(f"    [parser] {severity_str}: '{filename}' line {location.line} column {location.column}: {message}")
    print(f"    [parser] {warning_count} warnings, {error_count} errors.")

    if verbose or error_count > 0 or fatal_count > 0:
        print("Included files for this translation unit:")
        for inclusion in tu.get_includes():
            print(f"    {inclusion.include}")
            print(f"        from {inclusion.source} line {inclusion.location.line}")
    
    if error_count > 0 or fatal_count > 0:
        raise RuntimeError(f"Ill-formed translation unit {tu.spelling}.")
    
    Parser = ReflectionParser()
    Parser.files = [all_header_file_path] + files
    Parser.traverse(tu.cursor)
    Parser.generate_code(generated_code_dir)
