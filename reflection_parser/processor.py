import os
import pickle
import clang.cindex as CX
from pathlib import Path
from mako.template import Template
from reflection.Type import Type, Enum, Method, Field
from reflection.utils import find_smart_pointer_type_from_template


class ReflectionParser:
    def __init__(self):
        self.types = {} # a map from type name to Type object
        self.enums = {} # a map from enum name to Enum object
        self.files = [] # a list of files to be parsed
        self.file_type_map = {} # a map from file path to types in the file
        self.file_enum_map = {} # a map from file path to enums in the file
        self.type_file_map = {} # a map from type name to file path, may contain parent projects' types

    def in_record_type(self, name: str):
        return name in self.type_file_map.keys()
    
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
                field = Field(child)
                if field.type.cx_type.is_const_qualified():
                    print(f"[parser] Warning: detect const qualified field: {current_type.name}.{field.name}, which is not supported.")
                    continue
                current_type.fields.append(field)
                # reflection for smart pointer requires extra headers
                find_smart_pointer_type_from_template(field.type.cx_type, current_type.reflection_smart_pointer_typenames)
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
                    # smart pointer serialization requires extra headers, so we should record them
                    field = Field(child)
                    find_smart_pointer_type_from_template(field.type.cx_type, current_type.serialization_smart_pointer_typenames)
                    current_type.serialized_fields.append(field)
                    flag = True

        if flag:
            self.types[current_type.full_name] = current_type
            assert node.location.file is not None
            path = str(Path(node.location.file.name).resolve())
            if path not in self.file_type_map:
                self.file_type_map[path] = []
            self.file_type_map[path].append(current_type)
            self.type_file_map[current_type.full_name] = path
        return
    
    def traverse_enum(self, node: CX.Cursor, args: list):
        current_type = Enum(node.type)
        if self.enums.get(current_type.full_name) is not None:
            return
        for child in node.get_children():
            if child.kind == CX.CursorKind.ENUM_CONSTANT_DECL:
                current_type.values.append(child.spelling)
        self.enums[current_type.full_name] = current_type
        assert node.location.file is not None
        path = str(Path(node.location.file.name).resolve())
        if path not in self.file_enum_map:
            self.file_enum_map[path] = []
        self.file_enum_map[path].append(current_type)
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
            elif node.kind ==CX.CursorKind.ENUM_DECL:
                self.traverse_enum(node, args)
            else:
                raise Exception(f"Reflection macro can only be used in a class or struct, but found '{node.spelling}' '{node.kind}' not in a class")
        for child in node.get_children():
            self.traverse(child)


    def generate_code(self, generated_code_dir: str, output_files: list):
        with open("template/registrar_declare.hpp.template", "r") as f:
            template_declare = Template(f.read())
        with open(os.path.join(generated_code_dir, "registrar_declare.hpp"), "w") as f:
            f.write(template_declare.render(parser=self))
            
        with open("template/reflection_init.ipp.template", "r") as f:
            template_init = Template(f.read())
        with open(os.path.join(generated_code_dir, "reflection_init.ipp"), "w") as f:
            f.write(template_init.render(parser=self))
        
        with open("template/registrar_impl.ipp.template", "r") as f:
            template_impl = Template(f.read())
        for file in output_files:
            input_path = str(Path(file["input_path"]).resolve())
            if input_path not in self.file_type_map.keys():
                continue
            output_path = os.path.join(generated_code_dir, file["output_impl_file"]["registrar"])
            with open(output_path, "w") as out:
                out.write(template_impl.render(file_path=input_path, parser=self))
                
        with open("template/serialization_impl.ipp.template", "r") as f:
            template_gs_ipp = Template(f.read())
        for file in output_files:
            input_path = str(Path(file["input_path"]).resolve())
            if input_path not in self.file_type_map.keys():
                continue
            output_path = os.path.join(generated_code_dir, file["output_impl_file"]["serialization"])
            with open(output_path, "w") as out:
                out.write(template_gs_ipp.render(file_path=input_path, parser=self))


def clang_parse(file, config, verbose):
    index = CX.Index.create()
    flag = CX.TranslationUnit.PARSE_INCOMPLETE \
        + CX.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES \
        + CX.TranslationUnit.PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION
    try:
        tu = index.parse(file, args=config["args"].split(), options=flag)
    except CX.TranslationUnitLoadError as e:
        print(f"[parser] When parsing {file} , error occurs:")
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
    
    return tu


def process(config, verbose: bool = False):
    # generate the all reflection file header
    target_files = [str(Path(target["input_path"]).resolve()) for target in config["target_files"]]
    all_reflection_file_header_path = str(Path(os.path.join(config["generated_code_dir"], "all_reflection_files.hpp")).resolve())
    with open(all_reflection_file_header_path, "w") as f:
        for file_path in target_files:
            f.write('#include "%s"\n' % file_path)
    
    tu = clang_parse(all_reflection_file_header_path, config, verbose)
    
    Parser = ReflectionParser()
    Parser.files = [all_reflection_file_header_path] + target_files
    for cache_file_path in config["parent_project_cache"]:
        with open(cache_file_path, "rb") as f:
            Parser.type_file_map.update(pickle.load(f))
    Parser.traverse(tu.cursor)
    Parser.generate_code(config["generated_code_dir"], config["target_files"])
    with open(os.path.join(config["generated_code_dir"], "reflection_data.pkl"), "wb") as f:
        pickle.dump(Parser.type_file_map, f, protocol=pickle.HIGHEST_PROTOCOL)
