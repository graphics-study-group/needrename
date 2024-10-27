class Type:
    def __init__(self, name: str):
        self.name = name
        self.base_types = []
        self.fields = []
        self.methods = []
        self.parent = None

    def __str__(self):
        return f"Type: {self.name}, base_types: {self.base_types}, fields: {self.fields}, methods: {self.methods}"
