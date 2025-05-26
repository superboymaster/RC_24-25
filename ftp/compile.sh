#!/bin/bash

# Check if debug mode is enabled
if [ "$1" == "debug" ]; then
    echo "Compiling in debug mode..."
    gcc -DDEBUG -g main.c ftp.c -o download
else
    echo "Compiling in normal mode..."
    gcc main.c ftp.c -o download
fi

# Check if compilation was successful
if [ $? -eq 0 ]; then
    echo "Compilation successful."
else
    echo "Compilation failed."
fi