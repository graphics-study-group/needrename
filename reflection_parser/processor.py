import os
from io import StringIO
import clang.cindex as CX

index = None

def traverse(node: CX.Cursor):
    if node.kind == CX.CursorKind.ANNOTATE_ATTR:
        print(f"Find ANNOTATE_ATTR: {node.spelling} !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
    # else:
    #     print(f"{str(node.kind).removeprefix('CursorKind.')}: {node.spelling}")
    children = list(node.get_children())
    for child in children:
        traverse(child)

def process_file(path: str, generated_code_dir: str):
    # # The libclang can't ignore headers in the code, so we need to process all the headers to remove headers manually
    # code_lines = code.split('\n')
    # code_without_includes = '\n'.join(line for line in code_lines if not line.strip().startswith('#include'))
    # code = code_without_includes

    # global index
    # if index is None:
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
    traverse(tu.cursor)
