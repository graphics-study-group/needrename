class Type:
    def __init__(self, name: str):
        self.name = name
        self.base_types = []
        self.fields = []
        self.constructors = []
        self.methods = []

    def __str__(self):
        return f"Type: {self.name}, base_types: {self.base_types}, fields: {self.fields}, constructors: {self.constructors}, methods: {self.methods}"

class Method:
    def __init__(self, name: str):
        self.name = name
        self.arg_types = []
        self.return_type = "void"
        self.return_type_is_reference = False
        self.is_const = False
    
    def __str__(self):
        return f"Method: {self.name}, arg_types: {self.arg_types}, return_type: {self.return_type}"

class Field:
    def __init__(self, name: str):
        self.name = name
        self.type = None

    def __str__(self):
        return f"Field: {self.name}, type: {self.type}"
