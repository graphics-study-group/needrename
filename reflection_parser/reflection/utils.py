import clang.cindex as CX


def get_qualified_name(node: CX.Cursor):
    if node.kind in [CX.CursorKind.TRANSLATION_UNIT, CX.CursorKind.LINKAGE_SPEC]:
        return ""
    return get_qualified_name(node.semantic_parent) + ("" if node.semantic_parent.kind in [CX.CursorKind.TRANSLATION_UNIT, CX.CursorKind.LINKAGE_SPEC] else "::") + node.displayname


def get_type_full_name(cx_type: CX.Type):
    result = ""
    if cx_type.is_const_qualified():
        result += "const "
    
    if cx_type.kind == CX.TypeKind.POINTER:
        result += get_type_full_name(cx_type.get_pointee()) + " *"
    elif cx_type.kind == CX.TypeKind.CONSTANTARRAY:
        result += get_type_full_name(cx_type.element_type) + f"[{cx_type.element_count}]"
    elif cx_type.kind == CX.TypeKind.LVALUEREFERENCE:
        result += get_type_full_name(cx_type.get_pointee()) + " &"
    elif cx_type.kind == CX.TypeKind.RVALUEREFERENCE:
        result += get_type_full_name(cx_type.get_pointee()) + " &&"
    elif cx_type.kind == CX.TypeKind.RECORD:
        result += get_qualified_name(cx_type.get_declaration())
    elif cx_type.kind == CX.TypeKind.ELABORATED:
        result += get_qualified_name(cx_type.get_declaration())
    elif cx_type.kind == CX.TypeKind.ENUM:
        result += get_qualified_name(cx_type.get_declaration())
    else:
        result += cx_type.spelling
    return result

def get_simple_name(cx_type: CX.Type):
    result = ""
    if cx_type.is_const_qualified():
        result += "const "
    
    if cx_type.kind == CX.TypeKind.POINTER:
        result += get_simple_name(cx_type.get_pointee()) + " *"
    elif cx_type.kind == CX.TypeKind.CONSTANTARRAY:
        result += get_simple_name(cx_type.element_type) + f"[{cx_type.element_count}]"
    elif cx_type.kind == CX.TypeKind.LVALUEREFERENCE:
        result += get_simple_name(cx_type.get_pointee()) + " &"
    elif cx_type.kind == CX.TypeKind.RVALUEREFERENCE:
        result += get_simple_name(cx_type.get_pointee()) + " &&"
    elif cx_type.kind == CX.TypeKind.RECORD:
        result += cx_type.get_declaration().displayname
    elif cx_type.kind == CX.TypeKind.ELABORATED:
        result += cx_type.get_declaration().displayname
    elif cx_type.kind == CX.TypeKind.ENUM:
        result += cx_type.get_declaration().displayname
    else:
        result += cx_type.spelling
    return result


def get_mangled_qualified_name(node: CX.Cursor):
    if node.kind in [CX.CursorKind.TRANSLATION_UNIT, CX.CursorKind.LINKAGE_SPEC]:
        return ""
    name = node.displayname.replace("<", "_").replace(">", "_")
    return get_mangled_qualified_name(node.semantic_parent) + name + str(len(name))

def get_type_mangled_name(cx_type: CX.Type):
    result = ""
    if cx_type.is_const_qualified():
        result += "const"
    
    if cx_type.kind == CX.TypeKind.RECORD:
        result += get_mangled_qualified_name(cx_type.get_declaration())
    elif cx_type.kind == CX.TypeKind.ELABORATED:
        result += get_mangled_qualified_name(cx_type.get_declaration())
    elif cx_type.kind == CX.TypeKind.ENUM:
        result += get_mangled_qualified_name(cx_type.get_declaration())
    else:
        result += cx_type.spelling
    return result


def find_smart_pointer_type_from_template(cx_type: CX.Type, answer: list[str]):
    node = cx_type.get_declaration()
    if node.spelling in ["shared_ptr", "unique_ptr", "weak_ptr"] and get_qualified_name(node.semantic_parent) == "std":
        assert cx_type.get_num_template_arguments() == 1, f"Smart pointer {node.spelling} should have one template argument, but got {cx_type.get_num_template_arguments()}"
        answer.append(get_type_full_name(cx_type.get_template_argument_type(0)))
    num_arg = cx_type.get_num_template_arguments()
    for i in range(num_arg):
        arg = cx_type.get_template_argument_type(i)
        find_smart_pointer_type_from_template(arg, answer)
    