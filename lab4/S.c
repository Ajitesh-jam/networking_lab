#include <stdio.h>
#include <stdbool.h>
#include <strings.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include "ktpheader.h"

#define MAX_SEQ 255
#define PORT 5000
#define MAXLINE 1000
#define N 10
#define P 0.5
#define SENDBUFFERSIZE 10
#define RECEIVEBUFFERSIZE 10

#define timeout 10


#define ENOTBOUND -2
#define ENOSPACE -3
#define ENOMSG -4
#define INVALIDSOCKET -5
#define BINDFAILED -6
#define SENDFAILED -7
#define RECVFAILED -8


typedef struct KTP_socket KTP_socket;

struct KTP_socket KTP_sockets[N];           // shared mem
int ktp_pointer = 0;

struct sent_message{
    struct message msg;
    time_t send_time;
};
struct sent_message* send_buffer;           // shared mem

int send_buffer_size;                       // shared mem
int send_pointer = 0;                       // shared mem



int dropMessage()
{
    float r = (float)rand() / (float)RAND_MAX;
    if (r < P)
        return 1;
    return 0;
}

void SendMsg(struct message Message)
{
    //either drop the message or send the message

    if (dropMessage() == 1){
        //drop the message    
    }
    else{
        //send the message
        KTP_socket ktp_socket = KTP_sockets[Message.ktp_socket_id];
        //send via UDP
        struct sockaddr_in servaddr, cliaddr;
        bzero(&servaddr, sizeof(servaddr));

        // Create a UDP Socket
        servaddr.sin_addr.s_addr = inet_addr(ktp_socket.ipAddress);
        servaddr.sin_port = htons(ktp_socket.port);
        servaddr.sin_family = AF_INET;

        //adding headers to packet
        char * packet = strdup("D");
        // strcat(packet, ktp_socket.port);
        // strcat(packet, ktp_socket.ipAddress);
        strcat(packet, Message.msg);

        sendto(ktp_socket.udpSocketId, packet, sizeof(packet)+1, 0,(struct sockaddr*)&servaddr, sizeof(servaddr)); //send message via UDP
    }
}

int main(){
    while (1)
    {
        sleep(timeout/2);

        // send messages periodically
        if( send_buffer_size==0 ) continue;
        time_t curr_time = time(NULL);

        //check if timeouts
        if(curr_time - send_buffer[send_pointer].send_time < timeout){
            continue;
        }

        printf("TIMEOUT\n");
        // send the message again
        SendMsg(send_buffer[send_pointer].msg);
        //update the time
        send_buffer[send_pointer].send_time=curr_time;
        printf("trying to sent again\n");
    }
}