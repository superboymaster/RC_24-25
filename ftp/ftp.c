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
#include <sys/time.h>

#include "ftp.h"

#define BLOCK_SIZE 1024        // Block size for data transfers
#define PROGRESS_BAR_WIDTH 50  // Width of the progress bar (why not :))

// Private functions

// Will only be called by ftp_retrieve().
// Established the connection in passive mode.
int ftp_passive(ftp_connection_t* conn){

    if (!conn) {
        fprintf(stderr, "ftp_passive(): NULL connection parameter\n");
        return -1;
    }

    // Begin passive mode by sending the PASV command
    if (write(conn->control_socket, "PASV\r\n", 6) < 0) {
        perror("write(): PASV command");
        return -1;
    }

    // Read the response from the server
    int rcv_code = ftp_read_response(conn);
    if (rcv_code < 0) {
        return -1;
    }
    if (rcv_code != FTP_CODE_PASV_OK) { 
        fprintf(stderr, "Unexpected response code after PASV command: %d\n", rcv_code);
        return -1;
    }

    // The server will respond with the IP address and port for the data connection.
    // Ex: 227 Entering Passive Mode (192,168,1,2,123,45)
    // IP: 192.168.1.2
    // Port: 12345 (123 * 256 + 45 = 31545)
    char* start = strchr(conn->response, '(');
    if (!start){
        fprintf(stderr, "Invalid PASV response format: %s\n", conn->response);
        return -1;
    }
    
    char* end = strchr(start, ')');
    if (!end || end <= start) {
        fprintf(stderr, "Invalid PASV response format: %s\n", conn->response);
        return -1;
    }

    // To isolate only the IP and port information we insert null teriminator at the closing 
    // parenthesis and moving the start pointer one position
    *end = '\0';
    start++;

    int ip[4], port[2];
    if (sscanf(start, "%d,%d,%d,%d,%d,%d", ip, ip+1, ip+2, ip+3, port, port+1) != 6) {
        fprintf(stderr, "Failed to parse PASV response: %s\n", conn->response);
        return -1;
    }

    // With the response parsed we can now establish the data connection
    // The same way we did for the control connection
    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;

    // !TODO: Check possible memory vulnerability here
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    data_addr.sin_addr.s_addr = inet_addr(ip_str);
    data_addr.sin_port = htons(port[0] * 256 + port[1]);

    #ifdef DEBUG
    printf("[DEBUG] Data connection established on port %d with IP %s\n", port[0] * 256 + port[1], ip_str);
    #endif

    // Create the data socket
    if ((conn->data_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket(): data connection");
        return -1;
    }
    // Connect to the data socket
    if (connect(conn->data_socket, (struct sockaddr *) &data_addr, sizeof(data_addr)) < 0) {
        perror("connect(): data connection");
        close(conn->data_socket);
        return -1;
    }
   
    return 0;
}

// Send the SIZE command
// Warning: Not all servers support the SIZE command. If not supported, it will assume and
// unknown file size and carry on.
long ftp_get_file_size(ftp_connection_t* conn, const char* filepath) {
    char size_cmd[FTP_BUFFER_SIZE];
    snprintf(size_cmd, sizeof(size_cmd), "SIZE %s\r\n", filepath);
    
    if (write(conn->control_socket, size_cmd, strlen(size_cmd)) < 0) {
        perror("write(): SIZE command");
        return -1;
    }
    
    int rcv_code = ftp_read_response(conn);
    if (rcv_code == 213) {

        // Parse the size from the response
        // Response format: "213 1234567\r\n"
        char* size_str = strstr(conn->response, "213 ");
        if (size_str) {
            return atol(size_str + 4);  // Skip "213 "
        }
    }
    return -1;  // Size unknown
}

// Function to display a progress bar
void display_progress(long bytes_transferred, long total_size, double speed_kbps) {
    if (total_size <= 0) {
        printf("\rTransferred: %ld bytes | Speed: %.1f KB/s", 
               bytes_transferred, speed_kbps);
        fflush(stdout);
        return;
    }
    
    double progress = (double)bytes_transferred / total_size;
    int filled = (int)(progress * PROGRESS_BAR_WIDTH);
    
    printf("\r[");
    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
        if (i < filled) printf("=");
        else if (i == filled) printf(">");
        else printf(" ");
    }
    printf("] %.1f%% (%ld/%ld bytes) | %.1f KB/s", 
           progress * 100, bytes_transferred, total_size, speed_kbps);
    fflush(stdout);
}


// Public functions

int parse_ftp_url(const char* url, ftp_url_t* parsed){

    if (!url || !parsed)
    {
        fprintf(stderr, "parse_ftp_url(): NULL argument\n");
        return -1;
    }

    // Set the defaults
    strcpy(parsed->username, "anonymous");
    strcpy(parsed->password, "password");
    memset(parsed->hostname, 0, FTP_SMALL_BUFFER_SIZE);
    memset(parsed->filepath, 0, FTP_BUFFER_SIZE);
    parsed->port = FTP_CONTROL_PORT;

    // Check for prefix 'ftp://'
    if (strncmp(url, "ftp://", 6) != 0) {
        fprintf(stderr, "Error: URL must start with 'ftp://'\n");
        return -1;
    }

    char temp_url[FTP_URL_MAX_SIZE];
    strncpy(temp_url, url+6, FTP_URL_MAX_SIZE-1);
    temp_url[FTP_URL_MAX_SIZE - 1] = '\0';

    // Find the path
    char* path_start = strchr(temp_url, '/');
    if (path_start)
    {
        strncpy(parsed->filepath, path_start, FTP_BUFFER_SIZE-1);
        parsed->filepath[FTP_BUFFER_SIZE - 1] = '\0';
        // Insert null terminator in url string to isolate login details
        *path_start = '\0';
    }
    else
    {
        strcpy(parsed->filepath, "/"); // if there is no path default to root 
        printf("No path provided. Defaulting to root\n");
    }

    // Parse @
    char* at_pos = strchr(temp_url, '@');
    char* host_str;

    if (at_pos)
    {
        host_str = at_pos + 1;

        // Isolate login details
        *at_pos = '\0';
        char* divider = strchr(temp_url, ':');
        if (divider)
        {
            *divider = '\0';
            strncpy(parsed->username, temp_url, sizeof(parsed->username));
            parsed->username[sizeof(parsed->username)-1] = '\0';
            strncpy(parsed->password, divider + 1, sizeof(parsed->password) - 1);
            parsed->password[sizeof(parsed->password) - 1] = '\0';
        }
        else
        {
            // If there is no colon save username and use default password
            strncpy(parsed->username, temp_url, sizeof(parsed->username) - 1);
            parsed->username[sizeof(parsed->username) - 1] = '\0';
        }
    }
    else
    {
        host_str = temp_url;
    }

    // Prevent FTP command injection
    if (strchr(parsed->username, '\r') || strchr(parsed->username, '\n')) {
        fprintf(stderr, "Invalid characters in username\n");
        return -1;
    }

    // Save hostname
    if (strlen(host_str) == 0)
    {
        fprintf(stderr, "Error: No host provided in the url. Terminating...\n");
        return -1;
    }
    strncpy(parsed->hostname, host_str, sizeof(parsed->hostname) - 1);
    parsed->hostname[sizeof(parsed->hostname) - 1] = '\0';

    return 0;
}

int ftp_connect(ftp_url_t* parsed, ftp_connection_t* conn){
    
    // Check for null parameters
    if (!conn){
        fprintf(stderr, "ftp_connect(): NULL connection parameter\n");
        return -1;
    }

    // Initialize the connection variables
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    // This is an IPV4 address
    server_addr.sin_family = AF_INET;

    // Resolve host name
    struct hostent *host_entry;
    host_entry = gethostbyname(parsed->hostname);
    if (!host_entry){
        fprintf(stderr, "Failed to resolve hostname %s\n", parsed->hostname);
        return -1;
    }
    memcpy(&server_addr.sin_addr.s_addr, host_entry->h_addr_list[0], host_entry->h_length);
    server_addr.sin_port = htons(parsed->port);  
    
    // Create the control socket
    if ((conn->control_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    // Connect to the FTP server
    if (connect(conn->control_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        return -1;
    }
    //printf("Connected to FTP server %s on port %d\n", parsed.hostname, parsed.port);

    return 0;
}

int ftp_read_response(ftp_connection_t* conn) {

    char *line_end;
    int total_read = 0;
    
    do {
        int bytes = read(conn->control_socket, conn->response + total_read, FTP_BUFFER_SIZE - total_read - 1);
        if (bytes <= 0) 
            return -1;
        
        total_read += bytes;
        conn->response[total_read] = '\0';
        
        line_end = strstr(conn->response, "\r\n");

    } while (!line_end && total_read < FTP_BUFFER_SIZE - 1);
    
    // Invalid response if no line end string found
    if (total_read < 3) 
        return -1;
    
    char code_str[4];
    strncpy(code_str, conn->response, 3);
    code_str[3] = '\0';

    #ifdef DEBUG
    printf("[DEBUG] FTP Response:\n-----\n%s-----\n", conn->response);
    #endif
    
    return atoi(code_str);
}

int ftp_login(ftp_url_t* parsed, ftp_connection_t* conn){

    // Check for null parameters
    if (!conn) {
        fprintf(stderr, "ftp_login(): NULL connection parameter\n");
        return -1;
    }

    // Send USER command
    char user_command[FTP_BUFFER_SIZE];
    snprintf(user_command, sizeof(user_command), "USER %s\r\n", parsed->username);
    if (write(conn->control_socket, user_command, strlen(user_command)) < 0) {
        perror("write()");
        return -1;
    }

    // Read response
    int rcv_code = ftp_read_response(conn);
    if (rcv_code < 0) {
        return -1;
    }
    if (rcv_code != FTP_CODE_NEED_PASSWORD) { // 331 means username is okay, need password
        fprintf(stderr, "Unexpected response code after USER command: %d\n", rcv_code);
        return -1;
    }

    // Send PASS command
    char pass_command[FTP_BUFFER_SIZE];
    snprintf(pass_command, sizeof(pass_command), "PASS %s\r\n", parsed->password);
    if (write(conn->control_socket, pass_command, strlen(pass_command)) < 0) {
        perror("write()");
        return -1;
    }

    // Read response
    rcv_code = ftp_read_response(conn);
    if (rcv_code < 0) {
        return -1;
    }
    if (rcv_code != FTP_CODE_LOGIN_OK) { // 230 means login successful
        fprintf(stderr, "Unexpected response code after PASS command: %d\n", rcv_code);
        return -1;
    }

    return 0;
}

int ftp_disconnect(ftp_connection_t* conn) {

    if (!conn) {
        fprintf(stderr, "ftp_close(): NULL connection parameter\n");
        return -1;
    }

    // Send the QUIT command to the server
    if (write(conn->control_socket, "QUIT\r\n", 6) < 0) {
        perror("write(): QUIT command");
        return -1;
    }
    // Read the response to the QUIT command
    int rcv_code = ftp_read_response(conn);
    if (rcv_code < 0) {
        return -1;
    }
    if (rcv_code != FTP_CODE_QUIT_OK) { // 221 means service closing control connection
        fprintf(stderr, "Unexpected response code after QUIT command: %d\n", rcv_code);
        return -1;
    }
    //printf("Connection closed by server.\n");

    // Close the control and data sockets
    if (conn->control_socket >= 0) {
        close(conn->control_socket);
        conn->control_socket = -1;
    }
    
    if (conn->data_socket >= 0) {
        close(conn->data_socket);
        conn->data_socket = -1;
    }

    return 0;
}

int ftp_retrieve(ftp_url_t* parsed, ftp_connection_t* conn, int fd) {
    
    if (!conn || !parsed) {
        fprintf(stderr, "ftp_retrieve(): NULL connection or parsed URL parameter\n");
        return -1;
    }
    if (fd < 0) {
        fprintf(stderr, "ftp_retrieve(): Invalid file descriptor\n");
        return -1;
    }

    // Get file size for progress tracking
    long total_size = ftp_get_file_size(conn, parsed->filepath);
    if (total_size > 0) {
        printf("File size: %ld bytes\n", total_size);
    } else {
        printf("File size unknown\n"); // SIZE command failed or is not supported
    }

    // Send TYPE command (Set binary mode)
    if (write(conn->control_socket, "TYPE I\r\n", 8) < 0) {
        perror("write(): TYPE command");
        return -1;
    }
    int rcv_code = ftp_read_response(conn);
    if (rcv_code < 0) {
        return -1;
    }
    if (rcv_code != FTP_CODE_TYPE_OK) {
        fprintf(stderr, "Unexpected response code after TYPE command: %d\n", rcv_code);
        return -1;
    }

    // Begin passive mode transfer
    if (ftp_passive(conn) < 0) {
        fprintf(stderr, "Failed to establish passive data connection\n");
        return -1;
    }

    // Send the RETR command with the file name
    char retr_cmd[FTP_BUFFER_SIZE];
    int cmd_len = snprintf(retr_cmd, sizeof(retr_cmd), "RETR %s\r\n", parsed->filepath);
    if (write(conn->control_socket, retr_cmd, cmd_len) < 0) {  // Fixed: use actual length
        perror("write(): RETR command");
        return -1;
    }

    // Read response to RETR command (should be 150 or 125)
    // 150 - File is okay and will open connection.
    // 125 - Data connection is already open. Transfer will start soon.
    rcv_code = ftp_read_response(conn);
    if (rcv_code != 150 && rcv_code != 125) {
        fprintf(stderr, "RETR command failed with code: %d\n", rcv_code);
        if (conn->data_socket >= 0) {
            close(conn->data_socket);
            conn->data_socket = -1;
        }
        return -1;
    }

    // Data will be read in blocks of BLOCK_SIZE.
    char buffer[BLOCK_SIZE];
    ssize_t bytes_read;
    long total_transferred = 0;

    // Keep track of time to calculate transfer speed.
    struct timeval start_time, current_time;
    gettimeofday(&start_time, NULL);

    printf("Starting transfer...\n");
    while ((bytes_read = read(conn->data_socket, buffer, BLOCK_SIZE)) > 0) {
        
        ssize_t bytes_written = 0;
        ssize_t total_written = 0;
        
        // Make sure we only continue reading if all data already read was written to disk.
        while (total_written < bytes_read) {

            // Write may not finish in one go!
            bytes_written = write(fd, buffer + total_written, bytes_read - total_written);
            if (bytes_written < 0) {
                perror("write(): file data");
                close(conn->data_socket);
                conn->data_socket = -1;
                return -1;
            }
            total_written += bytes_written;
        }
        total_transferred += bytes_read;
        
        // Calculate transfer speed
        gettimeofday(&current_time, NULL);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_usec - start_time.tv_usec) / 1000000.0;
        double speed_kbps;
        if (elapsed > 0)
            speed_kbps = total_transferred / 1024.0 / elapsed;
        else
            speed_kbps = 0;
        
        // Update progress bar
        display_progress(total_transferred, total_size, speed_kbps);
    }
    printf("\n");  // New line after progress bar

    // Check for read errors
    if (bytes_read < 0) {
        perror("read(): data socket");
        close(conn->data_socket);
        conn->data_socket = -1;
        return -1;
    }

    // Close the data socket after the transfer is complete
    if (close(conn->data_socket) < 0) {
        perror("close(): data socket");
        return -1;
    }
    conn->data_socket = -1;

    // Read the final response from the control socket (should be 226)
    rcv_code = ftp_read_response(conn);
    if (rcv_code < 0) {
        return -1;
    }
    if (rcv_code != FTP_CODE_TRANSFER_OK) {  // 226 = Transfer complete
        fprintf(stderr, "Transfer completed with unexpected code: %d\n", rcv_code);
        return -1;
    }

    // Force all data to be written to disk if not already.
    if (fsync(fd) < 0) {
        perror("fsync(): file descriptor");
        return -1;
    }
    printf("File '%s' retrieved successfully (%ld bytes)\n", parsed->filepath, total_transferred);
    return 0;
}