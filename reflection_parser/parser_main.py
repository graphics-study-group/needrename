import sys
if sys.prefix[-10:] != 'parser_env':  
    raise RuntimeWarning(  
        f"This script is running in {sys.prefix} instead of a designated virtual environment, and might have unexpected behaviour."  
    )  

import os
import argparse
import filecmp
import shutil
from pathlib import Path

from processor import process_file

def main():
    parser = argparse.ArgumentParser(description="a parser based on libclang, used for reflection")
    parser.add_argument("--target_name", type=str, help="the target name for this parsing run", required=True)
    parser.add_argument("--reflection_search_files", type=str, help="a txt file records all the files that need to search for reflection", required=True)
    parser.add_argument("--generated_code_dir", type=str, help="the output directory of the generated files", required=True)
    parser.add_argument("--reflection_macros_header", type=str, help="a header file records all the macros about reflection", required=True)
    parser.add_argument("--args", type=str, help="the arguments to pass to the clang compiler", default="-x c++ -w -MG -M -ferror-limit=0 -std=c++20")
    parser.add_argument("--verbose", action="store_true", help="print verbose information")
    args = parser.parse_args()

    generated_code_dir = args.generated_code_dir
    # create the generated code directory
    if not os.path.exists(generated_code_dir):
        os.makedirs(generated_code_dir)
    temp_gen_dir = os.path.join(generated_code_dir, "temp")
    if not os.path.exists(temp_gen_dir):
        os.makedirs(temp_gen_dir)

    # generate the all reflection file header
    headers = args.reflection_search_files.strip().split(';')
    for i in range(len(headers)):
        headers[i] = str(Path(headers[i]).resolve())
     
    with open(args.reflection_macros_header, "r") as f:
        reflection_macros = f.read()
    output_file = os.path.join(temp_gen_dir, "all_reflection_files.hpp")
    output_file = str(Path(output_file).resolve())
    with open(output_file, "w") as f:
        # f.write(reflection_macros)
        # f.write("\n")
        for header in headers:
            if header == "":  # skip empty header
                continue
            f.write('#include "%s"\n' % header)
    
    # process the reflection
    print("[parser] processing all reflection files and generating reflection code")
    process_file(output_file, headers, temp_gen_dir, args.args, args.verbose)
    
    # copy the result when the generated files are different
    print("check and copy the generated files")
    for file in os.listdir(temp_gen_dir):
        src_file = os.path.join(temp_gen_dir, file)
        dest_file = os.path.join(generated_code_dir, file)
        if not os.path.exists(dest_file) or not filecmp.cmp(src_file, dest_file):
            print("copy file '%s'" % file)
            shutil.copy2(src_file, dest_file)
        else:
            print("file '%s' skipped (no changes)" % file)
    shutil.rmtree(temp_gen_dir)


if __name__ == "__main__":
    main()
