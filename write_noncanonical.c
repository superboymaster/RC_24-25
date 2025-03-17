// Write to serial port in non-canonical mode
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
#include <signal.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 5

#define FLAG 0x7E
#define A_BYTE 0x03
#define C_BYTE 0x07

volatile int STOP = FALSE;
volatile int REPEAT_SET = 0;

enum STATE {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV, 
    BCC_OK
};

// ALARM VARIABLES
int alarmEnabled = FALSE;
int alarmCount = 0;

// Alarm function handler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
    REPEAT_SET = 1;
    printf("Alarm #%d\n", alarmCount);
	
}

int read_command(int fd, unsigned char A, unsigned char C, unsigned char *RPT);
//int calculate_BCC2(unsigned char *data, int size, unsigned char *result);
int send_chunk(int fd, unsigned char *chunk, int size);

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

    // Open serial port device for reading and writing, and not as controlling tty
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
    newtio.c_cc[VMIN] = 0;  // Non-Blocking

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
    
    // SEND ------------------------------------------------------------

	// Set alarm function handler
    (void)signal(SIGALRM, alarmHandler);

    // DATA TO BE SENT 
    unsigned char data[5] = {0x0A, 0x0B, 0x7D, 0x0D, 0x0E}; 
    // SET command
    unsigned char SET[5] = {0x7E, 0x03, 0x03, 0x00, 0x7E};
    // DISC command
    unsigned char DISC[5] = {0x7E, 0x03, 0x0B, 0x03^0x0B, 0x7E};
    // UA command
    unsigned char UA[5] = {0x7E, 0x03, 0x07, 0x03^0x07, 0x7E};

    // Write SET command
    int bytes = write(fd, SET, BUF_SIZE);
    printf("%d bytes written\n", bytes);
    for (int i = 0; i < BUF_SIZE; i++)
    {
        printf("Sent: 0x%02X, ", SET[i]);
    }
    printf("\n");
    // Wait until all bytes have been written to the serial port
    sleep(1);

    //------------------------------------------------------------------

    // READ ------------------------------------------------------------
    
    // Read UA command and wait
    /*if (read_command(fd, 0x03, 0x07, SET))
    {
        printf("An error has occured reading UA\n");
    }*/
    //printf("UA command received successfully!\n");
    // If we reach this point we can start sending I frames
    
    send_chunk(fd, data, 5);
    

    //------------------------------------------------------------------

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}

int read_command(int fd, unsigned char A, unsigned char C, unsigned char *RPT)
{
    unsigned char in_byte = 0;
    int state = START;

    unsigned char BCC1 = A^C;

    while (STOP == FALSE)
    {
        if (alarmEnabled == FALSE)
        {
            alarm(3); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
        }
        if (REPEAT_SET)
		{
			write(fd, RPT, BUF_SIZE);
			sleep(1);
			REPEAT_SET = 0;
	    }
        
        int bytes = read(fd, &in_byte, 1);
        printf("Current state: %d\n", state);
        printf("Byte read: 0x%02X\n", in_byte);
        
        switch (state)
        {
            case START:
                if (in_byte == FLAG)
                    state = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (in_byte == A)
                    state = A_RCV;
                else if (in_byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case A_RCV:
                if (in_byte == FLAG)
                    state = FLAG_RCV;
                else if (in_byte == C)
                    state = C_RCV;
                else 
                    state = START;
                break;

            case C_RCV:
                if (in_byte == (BCC1))
                    state = BCC_OK;
                else if (in_byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case BCC_OK:
                if (in_byte == FLAG)
                {
                    STOP = TRUE;
                    printf("Read successfull\n");
                    alarm(0); // Disable alarm
                    alarmEnabled = FALSE;
                }
                else
                    state = START;
                break;

            default:
                state = START;
                break;
        }

        printf("New state: %d\n", state);
    }
    STOP = FALSE;
    return 0;
}

/*int calculate_BCC2(unsigned char *data, int size, unsigned char *result)
{
    if (size < 2)
    {
        printf("Cannot perform XOR on array with size less than 2");
        return -1;
    }

    unsigned char BCC2 = data[0] ^ data[1];

    for (int i = 2; i < size; i++)
    {
        BCC2 = BCC2 ^ data[i];
    }

    if (BCC2 == 0x7E || BCC2 == 0x7D)
    {
        BCC2 = (0x7D << 4) | (BCC2^0x20);
    }

    result = BCC2;

    return 0;
}*/

// Send a chunk of data to the serial port
int send_chunk(int fd, unsigned char *chunk, int size)
{
    // The chunk needs to be constructed into an I frame.
    // We'll need 6 bytes for the frame header and footer, however
    // we need to figure out how many bytes we need for the data field,
    // taking into account byte stuffing.
    //unsigned char I_frame[6+size];

    // Check the chunk for 0x7E and 0x7D bytes 
    int final_size = size;
    for (int i = 0; i < size; i++)
    {
        if (chunk[i] == 0x7E || chunk[i] == 0x7D)
        {
            final_size++;
        }
    }

    // BCC2 may also need byte stuffing
    int is_BCC2_stuffed = FALSE;
    unsigned char extra_BCC2 = 0;
    unsigned char BCC2 = chunk[0] ^ chunk[1];

    for (int i = 2; i < size; i++)
    {
        BCC2 = BCC2 ^ chunk[i];
    }

    if (BCC2 == 0x7E || BCC2 == 0x7D)
    {
        is_BCC2_stuffed = TRUE;
        extra_BCC2 = BCC2 ^ 0x20;
    }

    // We can now allocate the I frame with the correct size
    unsigned char I_frame[6 + final_size + is_BCC2_stuffed];

    // Construct the I frame
    I_frame[0] = FLAG;                      // Flag
    I_frame[1] = 0x03;                      // A
    I_frame[2] = 0x00;                      // C
    I_frame[3] = I_frame[1]^I_frame[2];     // BCC1
    
    // In order to create the data field of the I frame we need to
    // take into account byte stuffing. This occurs if 0x7E or 0x7D 
    // are found in the data field. If so, the byte 0x7D is sent
    // followed by the byte XORed with 0x20.
    int pos = 0;
    while (pos < final_size)
    {
        if (chunk[pos] == 0x7E || chunk[pos] == 0x7D)
        {
            I_frame[4+pos] = 0x7D;
            I_frame[5+pos] = chunk[pos] ^ 0x20;
            pos += 2;
        }
        else
        {
            I_frame[4+pos] = chunk[pos];
            pos++;
        }
    }

    //calculate_BCC2(chunk, size, &BCC2);   // BCC2 is performed with the original data
    I_frame[4+size] = BCC2;                 // BCC2
    if (is_BCC2_stuffed)
        I_frame[5+size] = extra_BCC2;
    I_frame[5+size+is_BCC2_stuffed] = FLAG; // Flag


    // write to port
    int bytes = write(fd, I_frame, 6+final_size+is_BCC2_stuffed);
    printf("%d, %d\n", is_BCC2_stuffed, final_size);
    printf("%d bytes written\n", bytes);
    for (int i = 0; i < 6+final_size+is_BCC2_stuffed; i++)
    {
        printf("Sent: 0x%02X, ", I_frame[i]);
    }
    printf("\n");
    // Wait until all bytes have been written to the serial port
    sleep(1);
}