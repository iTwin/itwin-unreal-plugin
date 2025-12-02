#--------------------------------------------------------------------------------------
#
#     $Source: run_uat_with_config.py $
#
#  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
#
#--------------------------------------------------------------------------------------

import sys
import os
import shutil
import subprocess
import argparse

def main():
    parser = argparse.ArgumentParser(description="A wrapper to run UAT with dynamic config overrides.")
    parser.add_argument("--ini-source", required=True, help="Path to the original DefaultGame.ini file.")
    parser.add_argument("--override-dir", required=True, help="Path to create the temporary override config directory.")
    parser.add_argument("--comfy-flag", required=True, choices=['ON', 'OFF'], help="The status of the COMFY feature flag.")
    parser.add_argument("uat_command", nargs=argparse.REMAINDER, help="The RunUAT.bat command and all its arguments.")

    args = parser.parse_args()

    # Override Config Directory ---
    override_ini_dir = args.override_dir
    override_ini_path = os.path.join(override_ini_dir, "Game.ini")

    print(f"--- Preparing config override in: {override_ini_dir}")
    os.makedirs(override_ini_dir, exist_ok=True)

    # Copy the original file to the override directory
    shutil.copyfile(args.ini_source, override_ini_path)

    # If the Comfy feature is ON, modify the copy to remove the NeverCook line
    if args.comfy_flag == 'ON':
        print("--- COMFY is ON. Removing /Game/ComfyUI from NeverCook list in override file.")
        with open(override_ini_path, 'r', encoding='utf-8') as f:
            lines = [line for line in f if '/Game/ComfyUI' not in line]
        with open(override_ini_path, 'w', encoding='utf-8') as f:
            f.writelines(lines)
    else:
        print("--- COMFY is OFF. No changes made to override file (exclusion remains).")

    # Construct and Run the UAT Command ---
    command_to_run = list(args.uat_command)
    
    # Inject the -ConfigOverride argument into the command
    command_to_run.append(f'-ConfigOverride={override_ini_dir}')

    print(f"--- Executing UAT command: {' '.join(command_to_run)}")
    
    # Use shell=True on Windows for .bat files
    is_windows = sys.platform.startswith('win')
    process = subprocess.run(command_to_run, shell=is_windows)

    # Propagate the exit code from UAT
    sys.exit(process.returncode)

if __name__ == "__main__":
    main()