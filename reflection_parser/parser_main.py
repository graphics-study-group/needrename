import os
import argparse

from processor import process_file

def main():
    parser = argparse.ArgumentParser(description="a parser based on libclang, used for reflection")
    parser.add_argument("--engine_headers_record_file", type=str, help="a txt file records all the header files of the engine", required=True)
    parser.add_argument("--generated_code_dir", type=str, help="the output directory of the generated files", required=True)
    args = parser.parse_args()

    # get all the headers of the engine 
    with open(args.engine_headers_record_file, "r") as f:
        headers = f.read().strip().split(';')
    # generate the all_headers file
    output_file = os.path.join(args.generated_code_dir, "engine_all_headers.hpp")
    with open(output_file, "w") as f:
        # XXX: the code below ignores other macros topological order
        # libclang can't open '#include' files in the code, 
        # and we must make sure the macros are defined before using them, 
        # so we need to make sure the macros about reflection are defined 
        # but the code below ignores other macros except reflection
        for header in headers:
            if "Reflection" in header:
                f.write('#include "%s"\n' % header)
        for header in headers:
            if "Reflection" not in header:
                f.write('#include "%s"\n' % header)
    
    # process all the headers
    process_file(output_file, args.generated_code_dir)


if __name__ == "__main__":
    main()
