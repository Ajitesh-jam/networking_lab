// == == == == == == == == == == == == == == == == == ==
// Assignment 4 Submission
// Name : Ajitesh Jamulkar
// Roll number : 22CS10004
//== == == == == == == == == == == == == == == == == ==

// ktp.h
#ifndef KTP_H
#define KTP_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>

#define MAX_SEQ 10              //should be 255
#define PORT 5000
#define MAXLINE 1000
#define N 10  // Maximum number of KTP sockets
#define P 0.5 // Probability of message drop
#define T 100  // Timeout in microseconds for receiving packets in select
#define SENDBUFFERSIZE 10
#define RECEIVEBUFFERSIZE 10
#define MAX_MSG_SIZE 1024
#define WINDOW_SIZE 10


// Error codes
#define ENOTBOUND -2
#define ENOSPACE -3
#define ENOMSG -4
#define INVALIDSOCKET -5
#define BINDFAILED -6
#define SENDFAILED -7
#define RECVFAILED -8

#define timeout 10

// Message structure
struct message
{
    int seq; // Sequence number
    char msg[MAX_MSG_SIZE];
    time_t send_time; //message send timestamp

    

};
struct SWND
{
    //bool notAcknowledged[WINDOW_SIZE]; // maximum messages  0-> message sent but not yet acknowledged 1-> message acknowledged

    int base;
    int end;
    int swndWindow; // sending window
    int seq_number; // sequence number
    int swndSize;
    struct message messageBuffer[WINDOW_SIZE];
};

struct RWND
{
    int expected_seq_number;         // sequence number

    int rwndWindow;             // free space in buffer
    int base;                   //first message to read
    int end;                    // kaha tak mil gya
    bool rcvd[WINDOW_SIZE];                 //wether msg recvd or not??
    char* messageBuffer[WINDOW_SIZE];
};

// KTP socket structure
struct KTP_socket
{
    bool isFree;
    int processId;
    int udpSocketId;
    char ipAddress[INET_ADDRSTRLEN];
    int port;

    struct SWND swnd;
    struct RWND rwnd;

};

// Sent message structure for reliability
struct sent_message
{
    struct message msg;
    time_t send_time;
};




// Function declarations
void initialize(void);
int k_socket(int family, int protocol, int flag, int processID);
int k_bind(int ktp_socketId, struct sockaddr *source, struct sockaddr *destination);
int k_sendto(int ktp_sockfd, char *mess, int maxSize, int flag, struct sockaddr *destination);
int k_recvfrom(int ktp_sockfd, char *message, int maxSize, int flag, struct sockaddr *source);
void k_close(int sockfd, struct sockaddr *dest);

#endif // KTP_H