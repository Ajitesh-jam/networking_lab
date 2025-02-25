#include <stdio.h>
#include <stdbool.h>
#include <strings.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>

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

struct message
{
    char *msg;
    int ktp_socket_id;
};

struct KTP_socket
{
    bool isFree;
    int processId;
    int udpSocketId;
    char *ipAddress;
    int port;

    struct message send_buffer[SENDBUFFERSIZE];
    struct message receive_buffer[RECEIVEBUFFERSIZE];
};
typedef struct KTP_socket KTP_socket;

struct KTP_socket KTP_sockets[N]; // shared mem
int ktp_pointer = 0;

struct sent_message
{
    struct message msg;
    time_t send_time;
};
struct sent_message *send_buffer; // shared mem

int send_buffer_size; // shared mem
int send_pointer = 0; // shared mem

// The  thread  R  behaves  in  the  following  manner.  It  waits  for  a  message  to  come  in  a 
// recvfrom()  call  from  any  of  the  UDP  sockets  (you  need  to  use  select()  to  keep  on 
// checking whether there is any incoming message on any of the UDP sockets, on timeout 
// check  whether  a  new  KTP  socket  has  been  created  and  include  it  in  the  read/write  set 
// accordingly).  When  it  receives  a  message,  if  it  is  a  data  message,  it  stores  it  in  the 
// receiver-side message buffer for the corresponding KTP socket (by searching SM with the 
// IP/Port), and sends an ACK message to the sender. In addition it also sets a flag nospace if 
// the available space at the receive buffer is zero. On a timeout over select() it additionally 
// checks whether the flag nospace was set but now there is space available in the receive 
// buffer. In that case, it sends a duplicate ACK message with the last acknowledged sequence 
// number but with the updated rwnd size, and resets the flag (there might be a problem here - 
// try to find it out and resolve!). If the received message is an ACK message in response to a 
// previously  sent  message,  it  updates  the  swnd  and  removes  the  message  from  the 
// sender-side message buffer for the corresponding KT



void recv_Ack()
{
    //remove from sent buffer and ktpsocket.sendbuffer
}

void ktp_recv(int socketFd, char *buf){

}





int main()
{
    struct sockaddr_in cliAddr;
    socklen_t len;

    while (1)
    {
        // check if there is any incoming message on any of the UDP sockets
        fd_set read_set;
        FD_ZERO(&read_set);
        for (int i = 0; i < N; i++)
        {
            FD_SET(KTP_sockets[i].udpSocketId, &read_set);
        }
        select(FD_SETSIZE, &read_set, NULL, NULL, NULL);
        for (int i = 0; i < N; i++){
            if (FD_ISSET(KTP_sockets[i].udpSocketId, &read_set))
            {
            // receive message
            struct sockaddr_in cliaddr;
            socklen_t cliaddr_len = sizeof(cliaddr);
            char buffer[1024];
            len=sizeof(cliaddr);
            int len = recvfrom(KTP_sockets[i].udpSocketId, buffer, 100,0 ,(struct sockaddr*)&cliAddr, &len);
            buffer[len] = '\0';    
            if(buffer[0]=='D'){

                //its a datapacket 

                //get client port 
                int clientPort = cliaddr.sin_port;
                //send ack

                

            }


            }
        }



    }    
}