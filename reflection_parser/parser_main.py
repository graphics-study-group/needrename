import sys
if sys.prefix[-10:] != 'parser_env':  
    raise RuntimeWarning(  
        f"This script is running in {sys.prefix} instead of a designated virtual environment, and might have unexpected behaviour."  
    )  

import os
import json
import argparse
import filecmp
import shutil
from pathlib import Path

from processor import process

def main():
    parser = argparse.ArgumentParser(description="a parser based on libclang, used for reflection")
    parser.add_argument("--config", type=str, help="the path to the config.json", required=True)
    parser.add_argument("--verbose", action="store_true", help="print verbose information")
    args = parser.parse_args()
    with open(args.config, "r") as f:
        config = json.load(f)
    
    generated_code_dir = config["generated_code_dir"]
    # create the generated code directory
    if not os.path.exists(generated_code_dir):
        os.makedirs(generated_code_dir)
    temp_gen_dir = os.path.join(generated_code_dir, "temp")
    if not os.path.exists(temp_gen_dir):
        os.makedirs(temp_gen_dir)
    
    # process the reflection
    print("[parser] processing all reflection files and generating reflection code")
    config["generated_code_dir"] = temp_gen_dir # change the generated code directory to the temp directory
    process(config, args.verbose)
    
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
