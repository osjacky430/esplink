import argparse
import sys
import json
from pathlib import Path, PurePath
import subprocess


def is_json(dir: str):
    if PurePath(dir).suffix != '.json':
        raise argparse.ArgumentTypeError('only json file is available')
    return dir


parser = argparse.ArgumentParser()
parser.add_argument('--mkbin-dir', type=Path, required=True)
parser.add_argument('--arg-dir', type=is_json, required=True)
parser.add_argument('--elf-dir', type=Path, required=True)


def main():
    cl_args = parser.parse_args()
    with open(cl_args.arg_dir) as file:
        invoke_args = json.load(file)

        elf_files = [files for files in cl_args.elf_dir.iterdir()
                     if files.suffix == '.elf']
        mkbin = cl_args.mkbin_dir / 'esp-mkbin'
        for elf in elf_files:
            bin_name = elf.stem + '.bin'
            args = invoke_args[elf.name]
            run_cmd = [mkbin, "--file",
                       elf, "--output", bin_name]
            [run_cmd.extend([k, v]) for k, v in args.items()]
            try:
                subprocess.run(run_cmd, check=True)
            except subprocess.CalledProcessError as err:
                print(err.output)
                sys.exit(err.returncode)


if __name__ == '__main__':
    main()
