#ifndef LINKLAYER_H
#define LINKLAYER_H

/*------------------------------------------------------------------------
* RC 24/25 L.EEC
* File: linklayer.h
*
* Description:
* This is library contains the link layer API functionality to provide
* to the application layer.
*
* Authors: José Santos; José Filipe
*
* Date: March 2025
-------------------------------------------------------------------------*/

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
#define _POSIX_SOURCE 1 // POSIX compliant source.

#define FALSE 0
#define TRUE 1

// Define roles for the connection
#define RX 0
#define TX 1

#define BUF_SIZE 5
#define MAX_SIZE 255
#define ALARM_TIMEOUT 10  // Alarm timeout in seconds.
#define MAX_RETRIES 10

// Define the flag we are using for this protocol.
#define FLAG 0x7E

// Alarm function handler
void alarmHandler(int signal);

/*
*   Establishes a connection between the transmitter and the receiver.
*
*   @param fd File descriptor of the serial port.
*   @param role Role of the connection (Transmitter|Receiver).
*
*   @returns 0 if successful, -1 if the connection could not be established.
*/
int llopen(int fd, int role);

/*
*   Sends data to the receiver.
*   
*   @param fd File descriptor of the serial port.
*   @param *buffer Pointer to the buffer containing the data to be sent.
*
*   @returns 0 if successful, -1 if the file could not be sent.
*/
int llwrite(int fd, unsigned char *buffer, int length);

/*
*   Reads data from the receiver.
*
*   @param fd File descriptor of the serial port.
*   @param *buffer Pointer to the buffer where the data will be stored.
*
*   @returns array length of received data, negative if an error occurred.
*/
int llread(int fd, unsigned char *buffer);

/*
*   Closes the connection between the transmitter and the receiver.
*   
*   @param fd File descriptor of the serial port.
*
*   @returns 0 if successful, -1 if the connection could not be closed.
*/
int llclose(int fd, int role);

// Data link layer functions

/*
*   Reads incomming bytes until a valid command is received.
*
*   @param fd File descriptor of the serial port.
*   @param *CMD - Pointer to the command to be read.
*   @param *RPT - Pointer to the command to be repeated.
*
*   @returns 0 if successful, -1 if *CMD or *RPT is NULL and -2 if alarm timeout is reached.
*/
int read_command(int fd, unsigned char *CMD, unsigned char *RPT);

#endif // LINKLAYER_H