// Read from serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

//#define DEBUG

#include "../include/linklayer.h"

// Define roles for the connection
#define RX 0
#define TX 1

// Application layer control field values
#define START 0x02
#define END 0x03
#define DATA 0x01

#define FILE_SIZE 0x00
#define FILE_NAME 0x01

#define ROLE RX

#define PROGRESS_BAR_WIDTH 50 // Width of the progress bar

// Function to display the progress bar
void display_progress_bar(long total_bytes, long fileSize)
{
    double progress = (double)total_bytes / fileSize * 100;
    int bar_width = (int)(progress / 100 * PROGRESS_BAR_WIDTH);

    printf("\r<"); 
    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++)
    {
        if (i < bar_width)
            printf("=");
        else
            printf(" ");
    }
    printf("> %.2f%%", progress); // Display the percentage
    fflush(stdout); // Ensure the output is displayed immediately
}

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

    // In the case of the receiver file path will be 
    // the path to store the received file
    char *file_path = argv[2];
    char file_name[MAX_SIZE];
    long fileSize = 0;

    // O_WRONLY -> Write only mode
    // O_CREAT -> Create the file if it does not exist
    // O_TRUNC -> Truncate the file to 0 bytes if it exists (Clear the file)
    // 0666 -> File permissions
    int file = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file < 0)
    {
        fprintf(stderr, "Error creating file %s\n", file_path);
        exit(1);
    }

    // Read the serial port number
    const char *portNumberString = argv[1];
    int portNumber = atoi(portNumberString);

    // Begin communication in RX mode
    int fd = llopen(portNumber, RX);
    if (fd == -1)
    {
        printf("Connection could not be established\n");
        exit(1);
    }

    // Read the file metadata
    unsigned char rcv_packet[MAX_SIZE];
    int rcv_packet_size = llread(fd, rcv_packet);
    if (rcv_packet_size < 0)
    {
        printf("Error reading file metadata\n");
        exit(1);
    }

    #ifdef DEBUG
    printf("Received packet:\n");
    for (int i = 0; i < rcv_packet_size; i++)
    {
        printf("0x%02X, ", rcv_packet[i]);
    }
    printf("\n");
    #endif

    // Check if the received packet is a START packet
    if (rcv_packet[0] != START)
    {
        printf("Error: Expected START packet\n");
        exit(1);
    }

    if (rcv_packet[1] == FILE_SIZE)
    {
        // Get the file size in bytes
        for (int i = 0; i < rcv_packet[2]; i++)
        {
            fileSize |= rcv_packet[i + 3] << (8 * (3 - i));
        }
    }
    else
    {
        printf("Error: Expected file size in START packet\n");
        //exit(1);
    }

    if (rcv_packet[3 + rcv_packet[2]] == FILE_NAME)
    {
        strcpy(file_name, (char *)&rcv_packet[4 + rcv_packet[2]]);
    }
    else
    {
        printf("Error: Expected file name in START packet\n");
        //exit(1);
    }

    printf("Receiving file: %s\nSize: %ld bytes\n", file_name, fileSize);

    // Read the file data while checking for END packets
    int run = TRUE;

    unsigned char data_packet[MAX_SIZE];
    int data_packet_size = 0;
    int total_bytes = 0;

    while (run)
    {
        data_packet_size = llread(fd, data_packet);
        /*if (data_packet_size <= 0)
        {
            printf("Error reading data packet (%d)\n", data_packet_size);
            exit(1);
        }*/

        #ifdef DEBUG
        printf("Received packet:\n");
        for (int i = 0; i < data_packet_size; i++)
        {
            printf("0x%02X, ", data_packet[i]);
        }
        printf("\n");
        #endif

        switch (data_packet[0])
        {
            case DATA: 
                int data_size = (data_packet[1] << 8) | data_packet[2];
                total_bytes += data_size;

                // Write the data received to the file
                if (write(file, &data_packet[3], data_size) < 0)
                {
                    printf("Error writing to file\n");
                    exit(1);
                }

                display_progress_bar(total_bytes, fileSize);

                break;
            case END:
                run = FALSE;
                printf("\nTransmitter request the END of communication\n");
                if (total_bytes != fileSize)
                {
                    printf("Warning: File size mismatch\n");
                    printf("Expected: %ld bytes\n", fileSize);
                    printf("Received: %d bytes\n", total_bytes);
                }
                else
                {
                    printf("File received successfully\n");
                    //printf("File size: %ld bytes\n", fileSize);
                    printf("Terminating communication\n");
                }

                llclose(fd, RX);
                close(file);
                break;
        }
    }

    return 0;
}
