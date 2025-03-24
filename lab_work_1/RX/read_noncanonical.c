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

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 5

#define FLAG 0x7E
#define A_BYTE 0x03
#define C_BYTE 0x03

volatile int STOP = FALSE;

enum STATE {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV, 
    BCC_OK
};

int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    // Set the initial state of the machine
    int state = START;

    // Loop for input
    //unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
    unsigned char reply[BUF_SIZE] = {0x7E, 0x03, 0x07, 0x03^0x07, 0x7E};

    unsigned char in_byte = 0;
    int bytes = 0;

    while (STOP == FALSE)
    {   
        bytes = read(fd, &in_byte, 1);
        printf("Current state: %d\n", state);
        printf("Byte read: 0x%02X\n", in_byte);
        
        switch (state)
        {
            case START:
                if (in_byte == FLAG)
                    state = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (in_byte == A_BYTE)
                    state = A_RCV;
                else if (in_byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case A_RCV:
                if (in_byte == FLAG)
                    state = FLAG_RCV;
                else if (in_byte == C_BYTE)
                    state = C_RCV;
                else 
                    state = START;
                break;

            case C_RCV:
                if (in_byte == (A_BYTE^C_BYTE))
                    state = BCC_OK;
                else if (in_byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case BCC_OK:
                if (in_byte == FLAG)
                    STOP = TRUE;
                else
                    state = START;
                break;

            default:
                state = START;
                break;
        }

        printf("New state: %d\n", state);
    }

    bytes = write(fd, reply, BUF_SIZE);
    printf("%d bytes written\n", bytes);

    // Wait until all bytes have been written to the serial port
    sleep(1);

    unsigned char DISC[5] = {0x7E, 0x03, 0x0B, 0x03^0x0B, 0x7E};

    write(fd, DISC, BUF_SIZE);

    // The while() cycle should be changed in order to respect the specifications
    // of the protocol indicated in the Lab guide

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
