import os
import argparse

from processor import process_file

def main():
    parser = argparse.ArgumentParser(description="a parser based on libclang, used for reflection")
    parser.add_argument("--reflection_search_files", type=str, help="a txt file records all the files that need to search for reflection", required=True)
    parser.add_argument("--generated_code_dir", type=str, help="the output directory of the generated files", required=True)
    parser.add_argument("--reflection_macros_header", type=str, help="a header file records all the macros about reflection", required=True)
    args = parser.parse_args()

    headers = args.reflection_search_files.strip().split(';')
    with open(args.reflection_macros_header, "r") as f:
        reflection_macros = f.read()
    # generate the all reflection file
    output_file = os.path.join(args.generated_code_dir, "all_reflection_files.hpp")
    with open(output_file, "w") as f:
        f.write(reflection_macros)
        f.write("\n")
        for header in headers:
            if header == "":  # skip empty header
                continue
            f.write('#include "%s"\n' % header)
    
    # process all the headers
    process_file(output_file, args.generated_code_dir)


if __name__ == "__main__":
    main()
