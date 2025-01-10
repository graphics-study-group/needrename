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
    else:
        result += cx_type.spelling
    return result


def get_mangled_qualified_name(node: CX.Cursor):
    if node.kind in [CX.CursorKind.TRANSLATION_UNIT, CX.CursorKind.LINKAGE_SPEC]:
        return ""
    return get_mangled_qualified_name(node.semantic_parent) + node.spelling + str(len(node.spelling))
    

def get_type_mangled_name(cx_type: CX.Type):
    result = ""
    if cx_type.is_const_qualified():
        result += "const"
    
    if cx_type.kind == CX.TypeKind.RECORD:
        result += get_mangled_qualified_name(cx_type.get_declaration())
    elif cx_type.kind == CX.TypeKind.ELABORATED:
        result += get_mangled_qualified_name(cx_type.get_declaration())
    else:
        result += cx_type.spelling
    return result
