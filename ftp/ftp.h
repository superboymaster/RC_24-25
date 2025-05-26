#ifndef _FTP_H_
#define _FTP_H_

// FTP Protocol Constants
#define FTP_URL_MAX_SIZE       2048
#define FTP_BUFFER_SIZE        1024
#define FTP_SMALL_BUFFER_SIZE  256
#define FTP_CONTROL_PORT       21

// FTP Commands
#define FTP_CODE_READY          220
#define FTP_CODE_LOGIN_OK       230  
#define FTP_CODE_NEED_PASSWORD  331
#define FTP_CODE_LOGIN_FAILED   530
#define FTP_CODE_PASV_OK        227
#define FTP_CODE_PASV_FAILED    425
#define FTP_CODE_QUIT_OK        221
#define FTP_CODE_TYPE_OK        200
#define FTP_CODE_TRANSFER_OK    226


typedef struct {
    int control_socket;
    int data_socket;
    char response[FTP_BUFFER_SIZE];
} ftp_connection_t;

typedef struct {
    char username[FTP_SMALL_BUFFER_SIZE];
    char password[FTP_SMALL_BUFFER_SIZE];
    char hostname[FTP_SMALL_BUFFER_SIZE];
    char filepath[FTP_BUFFER_SIZE];
    int port;
} ftp_url_t;

// Functions

/*
* @brief Parses an FTP URL and fills the ftp_url_t structure.

* @param url The FTP URL to parse.
* @param parsed Pointer to the ftp_url_t structure to fill.
* @return 0 on success, -1 on error.
* @note The URL should be in the format: ftp://[username[:password]@]hostname[:port][/path].
*       If username and password are not provided, defaults to "anonymous" and "password".
*       If path is not provided, defaults to root ("/").
*/
int parse_ftp_url(const char* url, ftp_url_t* parsed);

/*
* @brief Connects to the FTP server.
*
* @param parsed Pointer to the ftp_url_t structure containing connection details.
* @param conn Pointer to the ftp_connection_t structure to store the control socket.
* @return A socket file descriptor on success, -1 on error.
*/
int ftp_connect(ftp_url_t* parsed, ftp_connection_t* conn);

/*
* @brief Logs in to the FTP server.
*
* @param parsed Pointer to the ftp_url_t structure containing login details.
* @param conn Pointer to the ftp_connection_t structure containing the control socket.
* @return 0 on success, -1 on error.
*/
int ftp_login(ftp_url_t* parsed, ftp_connection_t* conn);

/*
* @brief Retrieves a file from the FTP server.
*
* @param parsed Pointer to the ftp_url_t structure containing the file path.
* @param conn Pointer to the ftp_connection_t structure containing the control and data sockets.
* @param fd The file descriptor to write the retrieved data to.
* @return 0 on success, -1 on error.
*/
int ftp_retrieve(ftp_url_t* parsed, ftp_connection_t* conn, int fd);

/*
* @brief Reads the FTP server response and checks for the expected response code.
*
* @param conn Pointer to the ftp_connection_t structure containing the control socket.
* @return -1 on error, RESPONSE_CODE on success.
*/
int ftp_read_response(ftp_connection_t* conn);

/*
* @brief Disconnects from the FTP server and closes the control and data sockets.
*
* @param conn Pointer to the ftp_connection_t structure containing the control and data sockets.
* @return 0 on success, -1 on error.
* @note Sends the QUIT command to the server before closing the sockets.
*/
int ftp_disconnect(ftp_connection_t* conn);

#endif // _FTP_H_