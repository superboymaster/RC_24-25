#include "../include/linklayer.h"

#define DEBUG

// Serial port settings
static struct termios oldtio, newtio;

// Keep track when we need to repeat the last command sent.
volatile int RETRANSMIT = FALSE;

// Define the states of the state machine.
//volatile int STOP = FALSE;
enum STATE {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV, 
    BCC_OK,
    STOP
};

// Alarm variables. 
int alarmEnabled = FALSE;
int alarmCount = 0;

// Fixed commands that will be sent or read.
// SET command
unsigned char SET[5] = {0x7E, 0x03, 0x03, 0x00, 0x7E};
// DISC command
unsigned char DISC[5] = {0x7E, 0x03, 0x0B, 0x03^0x0B, 0x7E};
// UA command
unsigned char UA[5] = {0x7E, 0x03, 0x07, 0x03^0x07, 0x7E};


// Alarm function handler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
    RETRANSMIT = TRUE;

    #ifdef DEBUG
    printf("Alarm triggered: No. %d\n", alarmCount);
    #endif
	
}

int read_command(int fd, unsigned char *CMD, unsigned char *RPT)
{
    // return if NULL
    if (CMD == NULL)
    {
        return -1;
    }
    
    unsigned char in_byte = 0;
    unsigned char A = CMD[1];
    unsigned char C = CMD[2];
    unsigned char BCC1 = A^C;
    
    int state = START;
    int run = TRUE;

    // Reset the alarm counter
    alarmCount = 0;
    alarm(ALARM_TIMEOUT);

    while (run)
    {
        // Verify that we have not exceeded the maximum number of retries.
        if (alarmCount >= MAX_RETRIES)
        {
            // Retries exceeded
            #ifdef DEBUG
            printf("[Read command] Time out. Returning\n");
            #endif
            return TIMEOUT_ERROR;
        }

        // Check if we need to retransmit and if the command to retransmit is valid.
        if (RETRANSMIT && RPT != NULL)
        {
            // Reset retransmit state
            RETRANSMIT = FALSE;
            // Resend frame
            write(fd, RPT, BUF_SIZE);
            sleep(1);

            // Assuming we have not yet exceeded the retries, restart the alarm
            alarm(ALARM_TIMEOUT);
            alarmEnabled = TRUE;
        }
 
        
        int bytes = read(fd, &in_byte, 1);
        //printf("Current state: %d\n", state);
        //printf("Byte read: 0x%02X\n", in_byte);
        
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
                    state = STOP;
                    //printf("Read successfull\n");
                    alarm(0); // Disable alarm
                    alarmEnabled = FALSE;
                }
                else
                    state = START;
                break;
            case STOP:
                    run = FALSE;
                break;

            default:
                state = START;
                break;
        }

        //printf("New state: %d\n", state);
    }

    return 0;
}

int llopen(int portNumber, int role)
{
    // Check if the status is valid
    if (role != TX && role != RX)
    {
        perror("Invalid role");
        return DEFAULT_ERROR;
    }

    // Create the serial port name
    char serialPortName[20];
    sprintf(serialPortName, "/dev/ttyS%d", portNumber);

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    // Check if the port was opened successfully
    if (fd < 0)
    {
        perror(serialPortName);
        return DEFAULT_ERROR;
    }

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        return DEFAULT_ERROR;
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
        return DEFAULT_ERROR;
    }

    //printf("New termios structure set\n");

	// Set alarm function handler
    (void)signal(SIGALRM, alarmHandler);

    printf("Will open in %d mode\n", role);

    // In case it is called by RX device.
    if (role == RX)
    {
        // NO TIMEOUT
        // Wait forever until something is received 
        
        int state = START;
        int run = TRUE;
        unsigned char in_byte;

        while (run)
        {
            int bytes = read(fd, &in_byte, 1);
            if (bytes <= 0)
            {
                continue;
            }
        
            switch (state)
            {
                case START:
                    if (in_byte == FLAG)
                        state = FLAG_RCV;
                    break;

            case FLAG_RCV:
                if (in_byte == SET[1])
                    state = A_RCV;
                else if (in_byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case A_RCV:
                if (in_byte == FLAG)
                    state = FLAG_RCV;
                else if (in_byte == SET[2])
                    state = C_RCV;
                else 
                    state = START;
                break;

            case C_RCV:
                if (in_byte == SET[3])
                    state = BCC_OK;
                else if (in_byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case BCC_OK:
                if (in_byte == FLAG)
                {
                    state = STOP;
                    run = FALSE;
                }
                else
                    state = START;
                break;
            case STOP:
                    run = FALSE;
                break;

            default:
                state = START;
                break;
            }

            printf("New state: %d\n", state);
        }

        // Write UA command to begin communication
        #ifdef DEBUG
        printf("Sending UA command...\n");
        #endif
        int bytes = write(fd, UA, BUF_SIZE);

        // Wait until all bytes have been written to the serial port
        sleep(1);

        if (bytes == BUF_SIZE)
        {
            #ifdef DEBUG
            printf("%d bytes written\nSent:\n", bytes);
            for (int i = 0; i < BUF_SIZE; i++)
            {
                printf("0x%02X, ", UA[i]);
            }
            printf("\n");
            #endif
        }
        else if (bytes <= 0)
        {
            perror("Could not write frame\n");
            return DEFAULT_ERROR;
        }

        // If we reach this point connection has been established.
        printf("[LL] Connection established\n");
        // Return the file descriptor of the serial port
        return fd;
    }

    // This portion will only execute if called as TX device.

    // Write SET command to begin communication
    #ifdef DEBUG
    printf("Sending SET command...\n");
    #endif
    int bytes = write(fd, SET, BUF_SIZE);

    // Wait until all bytes have been written to the serial port
    sleep(1);

    if (bytes == BUF_SIZE)
    {
        #ifdef DEBUG
        printf("%d bytes written\nSent:\n", bytes);
        for (int i = 0; i < BUF_SIZE; i++)
        {
            printf("0x%02X, ", SET[i]);
        }
        printf("\n");
        #endif
    }
    else if (bytes == -1)
    {
        perror("write");
        return DEFAULT_ERROR;
    }

    // Read UA command and wait
    int err = read_command(fd, UA, SET);
    if (err != 0)
        return err;

    #ifdef DEBUG
    else
    {
        printf("UA command received successfully!\n");
    }
    #endif

    // At this point the connection has been established
    printf("[LL] Connection established\n");

    // Return the file descriptor of the serial port
    return fd;
}

int llwrite(int fd, unsigned char *buffer, int length)
{
    // Starts at 0 and will switch between 0 and 1 for I0 and I1 respectively
    static int sequenceNumber = 0;

    // Check if the buffer is valid or not
    if (buffer == NULL)
    {
        perror("Buffer is NULL");
        return -1;
    }

    // The buffer needs to be constructed into an I frame.
    // We'll need 6 bytes for the frame header and footer, however
    // we need to figure out how many bytes we need for the data field,
    // taking into account byte stuffing.

    // Calculate the size of the I-frame, including byte stuffing
    int final_size = length;
    for (int i = 0; i < length; i++)
    {
        if (buffer[i] == 0x7E || buffer[i] == 0x7D)
        {
            final_size++;
        }
    }

    // Calculate BCC2 (error detection byte)
    unsigned char BCC2 = 0;
    for (int i = 0; i < length; i++)
    {
        BCC2 ^= buffer[i];
    }

    // Check if BCC2 needs byte stuffing
    int is_BCC2_stuffed = (BCC2 == 0x7E || BCC2 == 0x7D) ? 1 : 0;

    // Allocate memory for the I-frame
    unsigned char I_frame[6 + final_size + is_BCC2_stuffed];

    // Construct the I-frame
    I_frame[0] = FLAG;                      // Start flag
    I_frame[1] = 0x03;                      // Address field
    I_frame[2] = sequenceNumber == 0 ? 0x00 : 0x40; // Control field (I0 or I1)
    I_frame[3] = I_frame[1] ^ I_frame[2];   // BCC1 (Address XOR Control)

    // In order to create the data field of the I frame we need to
    // take into account byte stuffing. This occurs if 0x7E or 0x7D 
    // are found in the data field. If so, the byte 0x7D is sent
    // followed by the byte XORed with 0x20.

    // Add the data field with byte stuffing
    int pos = 0, frame_pos = 0;
    while (pos < length)
    {
        if (buffer[pos] == 0x7E || buffer[pos] == 0x7D)
        {
            I_frame[4 + frame_pos] = 0x7D;
            I_frame[5 + frame_pos] = buffer[pos] ^ 0x20;
            frame_pos += 2;
        }
        else
        {
            I_frame[4 + frame_pos] = buffer[pos];
            frame_pos++;
        }
        pos++;
    }

    // Add BCC2 with byte stuffing if necessary
    if (is_BCC2_stuffed)
    {
        I_frame[4 + frame_pos] = 0x7D;
        I_frame[5 + frame_pos] = BCC2 ^ 0x20;
        frame_pos += 2;
    }
    else
    {
        I_frame[4 + frame_pos] = BCC2;
        frame_pos++;
    }

    // Add the end flag
    I_frame[4 + frame_pos] = FLAG;

    // Write the I-frame to the serial port
    int bytes_written = write(fd, I_frame, 5 + frame_pos);
    if (bytes_written < 0)
    {
        perror("Error writing to serial port");
        return -1;
    }

    #ifdef DEBUG
    printf("%d bytes written\n", bytes_written);
    for (int i = 0; i < 5 + frame_pos; i++)
    {
        printf("Sent: 0x%02X\n", I_frame[i]);
    }
    #endif


    // We need to wait for an acknowledgment from the receiver.
    unsigned char in_byte = 0;
    int state = START;
    int run = TRUE;
    unsigned char command_read[BUF_SIZE];

    // Wait untill a valid command is read or timeout.
    while (run)
    {
        if (alarmEnabled == FALSE)
        {
            alarm(ALARM_TIMEOUT); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
        }
        if (RETRANSMIT)
		{
			bytes_written = write(fd, I_frame, 5 + frame_pos);
            if (bytes_written < 0)
            {
                perror("Error writing to serial port\n");
                return -1;
            }
			sleep(1);
			RETRANSMIT = 0;
	    }
        if (alarmCount > MAX_RETRIES)
        {
            alarm(0); // Disable alarm
            alarmEnabled = FALSE;
            alarmCount = 0;
            return -2;
        }
        
        int bytes = read(fd, &in_byte, 1);
        
        switch (state)
        {
            case START:
                if (in_byte == FLAG)
                    state = FLAG_RCV;
                    command_read[0] = in_byte;
                break;

            case FLAG_RCV:
                if (in_byte == 0x03)
                {
                    state = A_RCV;
                    command_read[1] = in_byte;
                }
                else if (in_byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case A_RCV:
                if (in_byte == FLAG)
                    state = FLAG_RCV;
                else if (in_byte == 0x05 || in_byte == 0x85 || in_byte == 0x01 || in_byte == 0x81)
                {
                    state = C_RCV;
                    command_read[2] = in_byte;
                }
                else 
                    state = START;
                break;

            case C_RCV:
                if (in_byte == (command_read[1] ^ command_read[2]))
                {
                    state = BCC_OK;
                    command_read[3] = in_byte;
                }
                else if (in_byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case BCC_OK:
                if (in_byte == FLAG)
                {
                    command_read[4] = in_byte;
                    state = STOP;
                    //printf("Read successfull\n");
                    alarm(0); // Disable alarm
                    alarmEnabled = FALSE;
                    alarmCount = 0;
                }
                else
                    state = START;
                break;
            case STOP:
                    run = FALSE;
                break;

            default:
                state = START;
                break;
        }

        //printf("New state: %d\n", state);
    }


    // Check if the response is an RR (Receiver Ready) or REJ (Reject)
    // If sequence number is 0 we are expecting RR1 or REJ0
    // If sequence number is 1 we are expecting RR0 or REJ1
    if (command_read[2] == (sequenceNumber == 0 ? 0x85 : 0x05)) // RR1 or RR0
    {
        sequenceNumber = 1 - sequenceNumber; // Switch sequence number

        #ifdef DEBUG
        printf("[LL] Acknowledgment (RR%d) received\n", sequenceNumber);
        #endif

        return bytes_written;
    }
    else if (command_read[2] == (sequenceNumber == 0 ? 0x01 : 0x81)) // REJ0 or REJ1
    {
        #ifdef DEBUG
        printf("[LL] Negative acknowledgment (REJ%d) received\n", sequenceNumber);
        #endif

        return -2; // Retransmission can be handled by the caller
    }

    return bytes_written;
}

int send_ack(int fd, unsigned char C_BYTE)
{
    unsigned char ACK[5] = {0x7E, 0x03, C_BYTE, 0x03^C_BYTE, 0x7E};
    int bytes = write(fd, ACK, BUF_SIZE);
    sleep(1);

    if (bytes == BUF_SIZE)
    {
        #ifdef DEBUG
        printf("%d bytes written\nSent:\n", bytes);
        for (int i = 0; i < BUF_SIZE; i++)
        {
            printf("0x%02X, ", ACK[i]);
        }
        printf("\n");
        #endif
    }
    else if (bytes == -1)
    {
        perror("[LL] Could not write ACK\n");
        return -1;
    }
}

int llread(int fd, unsigned char* buffer)
{
    // In this function we are repeatedly reading I frames and sending
    // back an acknowledgment to the transmitter. We will keep reading
    // until we receive the correct sequence number.

    unsigned char frame[2*MAX_SIZE+6]; // MAX SIZE of frame if all data is stuffed
    unsigned char in_byte;
    int frameLength = 0;
    static int exepectedSequenceNumber = 0;
    int receivedSequenceNumber = 0;

    // State machine variables
    int state = START;
    int run = TRUE;

    // Wait until a frame with a valid header is received
    while (run)
    {
        // just timout, no need to repeat
        if (alarmEnabled == FALSE)
        {
            alarm(3); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
            RETRANSMIT = FALSE;
        }
        if (alarmCount > ALARM_TIMEOUT)
        {
            //printf("Read timed out. Did not receive any response from receiver side.\n");
            return -2;
        }

        int bytes = read(fd, &in_byte, 1); // Read one byte at a time
        if (bytes <= 0)
        {
            continue;
        }
        #ifdef DEBUG
        printf("[LL] Byte read: 0x%02X\n", in_byte);
        #endif

        switch (state)
        {
        case START:
            if (in_byte == FLAG)
            {
                frameLength = 0;
                frame[frameLength++] = in_byte;
                state = FLAG_RCV;
            }
            break;

        case FLAG_RCV:
            if (in_byte == FLAG)
                frameLength = 1; // Keep flag
            else if (in_byte == 0x03)
            {
                frame[frameLength++] = in_byte;
                state = A_RCV;
            }
            else
            {
                state = START;
                frameLength = 0; // Reset frame
            }
            break;

        case A_RCV:
            if (in_byte == FLAG)
            {
                state = FLAG_RCV;
                frameLength = 1; // Keep flag
            }   
            else if (in_byte == 0x00 || in_byte == 0x40)
            {
                frame[frameLength++] = in_byte;
                receivedSequenceNumber = (in_byte == 0x00) ? 0 : 1;
                state = C_RCV;
            }
            else
            {
                state = START;
                frameLength = 0; // Reset frame
                send_ack(fd, exepectedSequenceNumber == 0 ? 0x01 : 0x81); // Send REJ0 or REJ1
                printf("[LL] received invalid sequence number %02x\n", in_byte);
                #ifdef DEBUG
                printf("[LL] REJ%d sent in state machine\n", exepectedSequenceNumber);
                #endif
            }
            break;

        case C_RCV:
            if (in_byte == FLAG)
            {
                state = FLAG_RCV;
                frameLength = 1; // Keep flag
            }
            else if (in_byte == (frame[1] ^ frame[2])) // Check BCC1
            {
                frame[frameLength++] = in_byte;
                state = BCC_OK;
                #ifdef DEBUG
                printf("[LL] BCC1 OK\n");
                #endif
            }
            else
            {
                state = START; // Invalid BCC1, discard frame and start again
                frameLength = 0; // Reset frame
            }
            break;

        case BCC_OK:
            if (in_byte == FLAG)
            {
                frame[frameLength++] = in_byte;
                state = STOP;
                run = FALSE;
                #ifdef DEBUG
                printf("[LL] Frame received successfully!\n");
                #endif
            }
            else
                frame[frameLength++] = in_byte;
            break;

        default:
            state = START;
            break;
        }
    }

    #ifdef DEBUG
    printf("[LL] Frame length: %d\n", frameLength);
    #endif

    // At this point we have a frame with a valid header
    // We need to read the data field and BCC2, however BCC2 was
    // performed on the original data field (not stuffed). 
    // need to perform destuffing first.
    
    // Destuff BCC2
    unsigned char rcv_BCC2;
    int wasBCC2Stuffed = FALSE;
    if (frame[frameLength-1 -2] == 0x7D) // Byte stuffing detected
    {
        rcv_BCC2 = frame[frameLength-1 -1] ^ 0x20; // XOR the next byte with 0x20
        wasBCC2Stuffed = TRUE;
    }
    else
    {
        rcv_BCC2 = frame[frameLength-1 -1];
    }

    #ifdef DEBUG
    printf("[LL] BCC2 received: 0x%02X\n", rcv_BCC2);
    #endif

    int dataLength = frameLength - 6 - wasBCC2Stuffed; // Exclude FLAG, A, C, BCC1, FLAG, and BCC2
    #ifdef DEBUG
    printf("[LL] Data length: %d\n", dataLength);
    #endif
    unsigned char destuffedData[dataLength];
    int destuffedLength = 0;

    for (int i = 4; i < dataLength+4; i++) // Begin where data begins and end before BCC2
    {   
        if (frame[i] == 0x7D) // Byte stuffing detected
        {
            destuffedData[destuffedLength++] = frame[++i] ^ 0x20; // XOR the next byte with 0x20
        }
        else
        {
            destuffedData[destuffedLength++] = frame[i];
        }
    }

    #ifdef DEBUG
    printf("[LL] Destuffed data length: %d\n", destuffedLength);
    printf("[LL] Data received after destuff: ");
    for (int i = 0; i < destuffedLength; i++)
    {
        printf("0x%02X, ", destuffedData[i]);
    }
    printf("\n");
    #endif

    // Check if BCC2 is correct
    unsigned char checkBCC2 = 0;
    for (int i = 0; i < destuffedLength; i++)
    {
        checkBCC2 ^= destuffedData[i];
    }
    if (checkBCC2 != rcv_BCC2)
    {
        #ifdef DEBUG
        printf("[LL] BCC2 error\n");
        #endif
        send_ack(fd, receivedSequenceNumber == 0 ? 0x01 : 0x81); // Send REJ0 or REJ1
        return -1;
    }

    #ifdef DEBUG
    printf("[LL] BCC2 OK\n");
    #endif

    // If we reach this point the frame is valid and we can tell the receiver
    // that we are ready for the next frame.

    // Copy the data to the output buffer
    memcpy(buffer, destuffedData, destuffedLength);

    // Send acknowledgment
    send_ack(fd, receivedSequenceNumber == 0 ? 0x85 : 0x05); // Send RR1 or RR0
    exepectedSequenceNumber = 1 - exepectedSequenceNumber; // Switch sequence number

    return destuffedLength;
}

int force_close_port(int fd)
{
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

    close(fd);

    return 0;
}

int llclose(int fd, int role)
{
    
    if (role != TX && role != RX)
    {
        perror("Invalid role");
        force_close_port(fd);
        return -1;
    }

    if (role == RX)
    {
        // Read DISC command and wait
        int err = read_command(fd, DISC, NULL);
        if (err == -1)
        {
            printf("NULL command\n");
            return -1;
        }
        else if (err == -2)
        {
            printf("Read timed out. Did not receive any response.\n"
                   "The program will end.\n");
            return -1;
        }
        #ifdef DEBUG
        else
        {
            printf("DISC command received successfully!\n");
        }
        #endif

        // Respond with DISC command
        int bytes = write(fd, DISC, BUF_SIZE);
        sleep(1);

        if (bytes == BUF_SIZE)
        {
            #ifdef DEBUG
            printf("%d bytes written\nSent:\n", bytes);
            for (int i = 0; i < BUF_SIZE; i++)
            {
                printf("0x%02X, ", DISC[i]);
            }
            printf("\n");
            #endif
        }
        else if (bytes == -1)
        {
            perror("did not write all bytes");
            return -1;
        }

        // Read UA command and wait
        err = read_command(fd, UA, DISC);
        if (err == -1)
        {
            printf("NULL command\n");
            return -1;
        }
        else if (err == -2)
        {
            printf("Read timed out. Did not receive any response.\n"
                   "The program will end.\n");
            return -1;
        }
        #ifdef DEBUG
        else
        {
            printf("UA command received successfully!\n");
        }
        #endif

        // Restore the old port settings
        if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
        {
            perror("tcsetattr");
            return -1;
        }

        close(fd);

        printf("[LL] Connection closed\n");

        return 0;
    }

    // Send DISC command
    #ifdef DEBUG
    printf("Disconnecting");
    #endif
    int bytes = write(fd, DISC, BUF_SIZE);
    sleep(1);

    if (bytes == BUF_SIZE)
    {
        #ifdef DEBUG
        printf("%d bytes written\nSent:\n", bytes);
        for (int i = 0; i < BUF_SIZE; i++)
        {
            printf("0x%02X, ", DISC[i]);
        }
        printf("\n");
        #endif
    }
    else if (bytes == -1)
    {
        perror("did not write all bytes");
        return -1;
    }

    // Read DISC command and wait
    int err = read_command(fd, DISC, DISC);
    if (err == -1)
    {
        printf("NULL command\n");
        return -1;
    }
    else if (err == -2)
    {
        printf("Read timed out. Did not receive any response.\n"
               "The program will end.\n");
        return -1;
    }
    #ifdef DEBUG
    else
    {
        printf("DISC command received successfully!\n");
    }
    #endif

    // Respond with UA to close connection
    bytes = write(fd, UA, BUF_SIZE);
    sleep(1);

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

    close(fd);

    printf("[LL] Connection closed\n");

    return 0;

}