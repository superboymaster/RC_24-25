// RC 24/25 
/*
*   José Santos and José Filipe
*   Description: 
*   This is the transmitter side of a program that sends files between two computers using an RS-232 connection.
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#define DEBUG

#include "../include/linklayer.h"

#define FALSE 0
#define TRUE 1

// Define roles for the connection
#define RX 0
#define TX 1

#define START 0x02
#define END 0x03
#define DATA 0x01

#define FILE_SIZE 0x00
#define FILE_NAME 0x01


int main(int argc, char *argv[])
{
    // Check if the program was called with the correct arguments
    if (argc < 3)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPortNumber> <FilePath>\n"
               "Example: %s 1 file.gif\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open the file to be sent
    int file = open(argv[2], O_RDONLY);
    if (file < 0)
    {
        fprintf(stderr, "Error opening file %s\n", argv[2]);
        exit(1);
    }

    // Reading file stats
    struct stat fileStat;
    if (fstat(file, &fileStat) < 0)
    {
        fprintf(stderr, "Error reading file stats\n");
        close(file);
        exit(1);
    }
    // Get the file size in bytes.
    long fileSize = fileStat.st_size;

    printf("File: %s\nSize: %ld bytes\n", argv[2], fileSize);

    // Read the serial port number
    const char *portNumberString = argv[1];
    int portNumber = atoi(portNumberString);

    // Begin communication
    int fd = llopen(portNumber, TX);
    if (fd == -1)
    {
        printf("Connection could not be established\n");
        exit(1);
    }

    // Read the file and send it to the receiver
    // Start by sending START packet with file size and name.
    unsigned char packet[MAX_SIZE];

    packet[0] = START;
    packet[1] = FILE_SIZE;
    packet[2] = sizeof(fileSize); // file size is a long
    packet[3] = (fileSize >> 24) & 0xFF;
    packet[4] = (fileSize >> 16) & 0xFF;
    packet[5] = (fileSize >> 8) & 0xFF;
    packet[6] = fileSize & 0xFF;
    packet[7] = FILE_NAME;
    if (strlen(argv[2]) > (MAX_SIZE-8))
    {
        printf("File name is too long\n");
        exit(1);
    }
    strcpy((char *)&packet[8], argv[2]);

    int bytes = llwrite(fd, packet, strlen(argv[2]) + 8);
    if (bytes == -1)
    {
        printf("Error sending file size and name\n");
        exit(1);
    }
    while (llwrite(fd, packet, strlen(argv[2]) + 8) == -2)
    {
        printf("Rejected, will try again\n");
    }

    sleep(1);
    
    // Start sending the file in data packets

    llclose(fd, TX);
    close(file);

    return 0;
}