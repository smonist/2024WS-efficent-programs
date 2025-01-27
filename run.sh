#!/bin/bash

#Thanks ChatGPT

# Function to display error message if './ourJoin' is not found
command_not_found() {
  echo -e "\033[1;31mCommand './ourJoin' not found. Did you forget to run 'make'?\033[0m"
}

# Function to recompile the ./ourJoin executable
recompile() {
  echo -e "\033[1;34mRecompiling './ourJoin'...\033[0m"
  make clean && make
  if [ $? -ne 0 ]; then
    echo -e "\033[1;31mFailed to compile './ourJoin'.\033[0m"
    exit 1
  fi
}

# Default commands
small_command="./ourJoin data/a.csv data/b.csv data/c.csv data/d.csv"
large_command="./ourJoin data/f1.csv data/f2.csv data/f3.csv data/f4.csv"

# Parse flags
profile_prefix=""
small=false
recompile_flag=false
validate=false

validate_command=""

while [[ "$#" -gt 0 ]]; do
  case $1 in
    --small) small=true ;;
    --profile) profile_prefix="LC_NUMERIC=en_US perf stat -e cycles " ;;
    --recompile) recompile_flag=true ;;
    --validate) validate=true ;;
    *) echo -e "\033[1;33mUnknown option: $1\033[0m" && exit 1 ;;
  esac
  shift
done

# Recompile if the flag is set
if [ "$recompile_flag" = true ]; then
  recompile
fi

# Check if the './ourJoin' command is available
if ! command -v ./ourJoin &> /dev/null; then
  command_not_found
  exit 1
fi

# Select the command based on the --small flag
command_to_run="$small_command"
if [ "$small" = false ]; then
  command_to_run="$large_command"
fi

if [ "$validate" = true ]; then
  validate_command="|sort|diff - data/abcd.csv"
  if [ "$small" = false ]; then
    validate_command="|sort|diff - data/output.csv"
  fi
fi

# Execute the command with profiling if enabled
final_command="${profile_prefix}${command_to_run}${validate_command}"
echo -e "\033[1;34mRunning command:\033[0m $final_command"
eval "$final_command"
