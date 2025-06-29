import clang.cindex as CX
from reflection.utils import *

class Type:
    def __init__(self, cx_type: CX.Type):
        self.cx_type = cx_type
        self.name = get_simple_name(self.cx_type)
        self.full_name = get_type_full_name(self.cx_type)
        self.mangled_name = get_type_mangled_name(self.cx_type)
        self.base_types = []
        self.fields = []
        self.serialized_fields = []
        self.serialization_smart_pointer_typenames = []
        self.constructors = []
        self.methods = []


class Method:
    def __init__(self, cx_method: CX.Cursor):
        self.cx_method = cx_method
        self.name = cx_method.spelling
        self.return_type = Type(cx_method.result_type)
        self.return_type_is_reference = cx_method.result_type.get_canonical().kind in [CX.TypeKind.LVALUEREFERENCE, CX.TypeKind.RVALUEREFERENCE]
        self.is_const = cx_method.is_const_method()
        
        self.arg_types = []
        for method_child in cx_method.get_children():
            if method_child.kind == CX.CursorKind.PARM_DECL:
                self.arg_types.append(Type(method_child.type))
    

class Field:
    def __init__(self, cx_field: CX.Cursor):
        self.cx_field = cx_field
        self.name = cx_field.spelling
        self.type = Type(cx_field.type)
