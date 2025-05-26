# FTP Client
## Build
For compiling the project run:
```
make
```
For a clean build you can also do:
```
make rebuild
```
If you wish to see debug messages run:
```
make DEBUG=1
```
## Usage
For running the program do:
```
./download <link>
```
```<link>``` should be your ftp link for the file you wish to transfer. It shoud be in the format ```ftp://[<user>:<password>@]<host>/<url-path>```
The requested files will be downloaded to a folder created automatically called 'downloads' in the working directory.

## Structure
```
ftp/
├── compile.sh       # Bash script for compiling and running the program (unused)
├── main.c           # Main source file for the FTP client
├── ftp.c            # FTP-related functionality implementation
├── ftp.h            # Header file for FTP-related functions
├── makefile         # Makefile for building the project
├── README.md        # Documentation for the project
└── downloads/       # Directory where downloaded files are stored (created at runtime)
```