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
               "Usage: %s <SerialPortNumber> <Role>\n"
               "Example: %s 1 0\nThis will open /dev/ttyS1 in RX mode\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Read role
    int role = atoi(argv[2]);
    if (role != RX && role != TX)
    {
        printf("Invalid role. Use 0 for RX and 1 for TX\n");
        exit(1);
    }

    // Read the serial port number
    const char *portNumberString = argv[1];
    int portNumber = atoi(portNumberString);

    // Begin communication
    int fd = llopen(portNumber, role);
    if (fd == DEFAULT_ERROR)
    {
        printf("Connection could not be established\n");
        exit(1);
    }
    else if (fd == TIMEOUT_ERROR)
    {
        printf("Connection time out. The program will now close.\n");
        exit(1);
    }
    
    // Test data:
    unsigned char data[] = {0x00, 0x01, 0x02, 0x03, 0x7E, 0x05, 0x06, 0x07, 0x7D, 0x09};
    unsigned char rcv_packet[MAX_SIZE];


    switch (role)
    {
        case RX:
            printf("Receiver mode\n");
            int packet_count = 0;
            // Receive the data from the sender
            while (packet_count < 10)
            {
                int bytes = llread(fd, rcv_packet);
                if (bytes < 0)
                {
                    printf("Error receiving data (Code %d)\n", bytes);
                    exit(1);
                }
                printf("Received packet %d: ", packet_count);
                for (int i = 0; i < bytes; i++)
                {
                    printf("0x%02X ", rcv_packet[i]);
                }
                printf("\n");
                packet_count++;
            }
            break;
        case TX:
            printf("Transmitter mode\n");
            // Send the data to the receiver
            for (int i = 0; i < 10; i++)
            {
                int bytes = llwrite(fd, data, sizeof(data));
                if (bytes < 0)
                {
                    printf("Error sending data (Code %d)\n", bytes);
                    exit(1);
                }
                sleep(1); // Wait for 1 second before sending the next packet
            }
            sleep(1);
            break;
        default:
            printf("Invalid role\n");
            exit(1);
    }


    // When data is done close the connection
    llclose(fd, role);

    return 0;
}
