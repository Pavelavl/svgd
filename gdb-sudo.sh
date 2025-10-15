#!/bin/bash

# This script is a wrapper for gdb to run with sudo permissions.

# Check if the script is being run to provide the password
if [[ "$SUDO_ASKPASS" = "$(realpath -s "$0")" ]]; then
  zenity --password --title="$1"
else
  # Execute gdb with the parameters passed by VS Code
  exec env SUDO_ASKPASS="$(realpath -s "$0")" sudo -A /usr/bin/gdb "$@"
fi