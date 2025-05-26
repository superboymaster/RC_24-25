#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include "ftp.h"

#define FTP_SAVE_DIR "downloads"

int main(int argc, char **argv){

    // Check the arguments.
    if (argc < 2)
    {
        fprintf(stderr, "Too few arguments! Please provide an ftp link.\n");
        exit(-1);
    }

    // URL parsing.
    ftp_url_t parsed;
    if (parse_ftp_url(argv[1], &parsed) != 0)
    {
        fprintf(stderr, "Failed to parse URL.\n");        
        exit(-1);
    }

    printf("+-----------------------------------------+\n|             FTP DOWNLOADER              |\n+-----------------------------------------+\n");
    printf("Transfer details:\nUSER: %s\nPASS: %s\nHOST: %s\nPATH: %s\n", parsed.username, parsed.password, parsed.hostname, parsed.filepath);
    
    // Open a connection to the FTP server.
    ftp_connection_t conn;
    if (ftp_connect(&parsed, &conn) != 0)
    {
        fprintf(stderr, "Failed to connect to FTP server.\n");
        exit(-1);
    }
    printf("Successfully connected to FTP server %s on port %d!\n", parsed.hostname, parsed.port);

    // Read the server's welcome message.
    int rcv_code = 0;
    if ((rcv_code = ftp_read_response(&conn)) < 0) {
        fprintf(stderr, "Failed to read welcome message.\n");
        close(conn.control_socket);
        exit(-1);
    }
    if (rcv_code != FTP_CODE_READY) {
        fprintf(stderr, "Unexpected response code: %d\n", rcv_code);
        close(conn.control_socket);
        exit(-1);
    }

    // Log in to the server with provided credentials.
    if (ftp_login(&parsed, &conn) != 0) {
        fprintf(stderr, "Failed to log in to FTP server.\n");
        close(conn.control_socket);
        exit(-1);
    }
    printf("Logged in as %s\n", parsed.username);

    // The files will be saved to a download directory.
    // Ensure the directory exists
    if (mkdir(FTP_SAVE_DIR, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create downloads directory.\n");
        close(conn.control_socket);
        exit(-1);
    }

    // Change to the download directory
    if (chdir(FTP_SAVE_DIR) < 0) {
        fprintf(stderr, "Failed to change to downloads directory.\n");
        close(conn.control_socket);
        exit(-1);
    }

    // Find the last '/' in the path string. This will give us the file name.
    char* filename = strrchr(parsed.filepath, '/');
    if (filename) {
        filename++; // Skip the '/'
    } else {
        filename = parsed.filepath;
    }
    
    // Handle empty filename
    if (strlen(filename) == 0) {
        fprintf(stderr, "Invalid file path: no filename found.\n");
        close(conn.control_socket);
        exit(-1);
    }

    // Open local file for writing (in the chosen new directory).
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to open file %s for writing.\n", filename);
        close(conn.control_socket);
        exit(-1);
    }
    
    // Retrieve the file from the server.
    if (ftp_retrieve(&parsed, &conn, fd) != 0) {
        fprintf(stderr, "Failed to retrieve file from FTP server.\n");
        // Clean up
        close(fd);
        close(conn.control_socket);
        if (conn.data_socket >= 0) {
            close(conn.data_socket);
        }
        exit(-1);
    }

    // Close the file descriptor after writing successfully.
    close(fd);
    //printf("File retrieved successfully.\n");

    // Disconnect from the FTP server
    if (ftp_disconnect(&conn) != 0) {
        fprintf(stderr, "Failed to disconnect from FTP server.\n");
        close(conn.control_socket);
        exit(-1);
    }
    printf("Disconnected from FTP server.\n");

    return 0;
}