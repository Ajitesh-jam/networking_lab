// ktp.c
#include "ktpheader.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>

#define SHM_KEY 1234 // Unique key for shared memory

// Global variables
#define BUFFER_SIZE 1024 // Define BUFFER_SIZE with an appropriate value

static int send_buffer_size = 0;
static int send_pointer = 0;
static int global_error = 0;
bool flag_no_space = false;
bool flagInitalized = false;
// Removed duplicate declaration
// struct sent_message *send_buffer;  // This was duplicated

static int dropMessage(float p)
{
    float r = (float)rand() / (float)RAND_MAX;
    return (r < p); // Returns 1 if message should be dropped, 0 if sent
}

static void SendMsg(struct message *Message,int ktp_socket_id, int seq_num)
{
    if (dropMessage(P))
    {
        printf("[DROP] Message dropped (probability check)\n");
        return; // Message is dropped, do not send
    }

    struct KTP_socket *ktp_socket = &KTP_sockets[ktp_socket_id];
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_addr.s_addr = inet_addr(ktp_socket->ipAddress);
    servaddr.sin_port = htons(ktp_socket->port);
    servaddr.sin_family = AF_INET;

    char buffer[MAX_MSG_SIZE + 1];
    buffer[0] = 'D';
    buffer[1] = '0' + seq_num;
    strncpy(buffer + 2, Message->msg, MAX_MSG_SIZE - 2);
    buffer[MAX_MSG_SIZE] = '\0'; // Ensure null termination

    sendto(ktp_socket->udpSocketId, buffer, strlen(buffer) + 1, 0,
           (struct sockaddr *)&servaddr, sizeof(servaddr));


    printf("[SUCCESSFUL UDP SEND] Message sent to IP %s port %d\n", ktp_socket->ipAddress, ktp_socket->port);
}

static void SendAck(int seq_num, int rwnd, struct sockaddr *destination){
    
    //make ack packet and send to the destination
    // if (!dropMessage(P))
    // {
    //     printf("[DROP] ACK dropped (probability check)\n");
    //     return;
    // }
    
    //make ack packet
    char buffer[MAX_MSG_SIZE] = "A";
    //buffer[1] = '0' + seq_num;
    //char *updated_size ='0';
    //strcpy(buffer + 1,rwnd  );
    sprintf(buffer, "A%01d%01d", seq_num, rwnd);

    buffer[strlen(buffer)-1] = '\0';
    sendto(KTP_sockets[0].udpSocketId, buffer, strlen(buffer) + 1, 0, destination, sizeof(*destination));
    printf("[SUCCESSFUL UDP ACK SEND] Message sent with msg %s\n", buffer);
}

void *R(void *arg)
{


    printf("Reliable Recving thread running...\n");
    fflush(stdout);
    while (1)
    {
        // Check if there is any incoming message on any of the UDP sockets
        fd_set read_set;
        FD_ZERO(&read_set);
        int max_fd = 0;
        for (int i = 0; i < N; i++)
        {
            if (!KTP_sockets[i].isFree && KTP_sockets[i].udpSocketId > 0)
            {
                // Check if the socket is valid
                int flags = fcntl(KTP_sockets[i].udpSocketId, F_GETFL, 0);
                if (flags == -1)
                {
                    perror("[ERROR] Invalid socket detected in R()");
                    continue; // Skip this socket
                }

                FD_SET(KTP_sockets[i].udpSocketId, &read_set);
                if (KTP_sockets[i].udpSocketId > max_fd)
                {
                    max_fd = KTP_sockets[i].udpSocketId;
                }
            }
        }

        // Add timeout to prevent blocking indefinitely
        struct timeval tv;
        tv.tv_sec = T; // 1 second timeout
        tv.tv_usec = 0;

        int ret = select(max_fd + 1, &read_set, NULL, NULL, &tv);
        if (ret < 0)
        {
            perror("Select failed");
            continue;
        }
        if (ret == 0)
        {
            // Timeout, no data available
            continue;
        }

        for (int i = 0; i < N; i++)
        {
            if (!KTP_sockets[i].isFree && KTP_sockets[i].udpSocketId > 0 &&
                FD_ISSET(KTP_sockets[i].udpSocketId, &read_set))
            {
                // Receive message
                struct sockaddr_in cliaddr;
                socklen_t cliaddr_len = sizeof(cliaddr);
                char recv_buffer[MAX_MSG_SIZE + 1];
                int recv_len = recvfrom(KTP_sockets[i].udpSocketId, recv_buffer, MAX_MSG_SIZE, 0,
                                        (struct sockaddr *)&cliaddr, &cliaddr_len);

                if (recv_len <= 0)
                {
                    continue;
                }

                recv_buffer[recv_len] = '\0';
                int ktp_socket_id;
                // iterate in ktp socket to find which client had send the message and get its ktp id
                for (int j = 0; j < N; j++)
                {
                    if (!KTP_sockets[j].isFree && KTP_sockets[j].port == ntohs(cliaddr.sin_port)
                        //&& KTP_sockets[i].ipAddress == inet_aton(cliaddr.sin_addr.s_addr, &cliaddr)
                    )
                    {
                        ktp_socket_id = j;
                        break;
                    }
                }
                if (recv_buffer[0] == 'D')
                {
                    // It's a data packet
                    int seq_num = recv_buffer[1] - '0';
                    printf("[RECV] Data packet received from port %d, seq: %d\n", ntohs(cliaddr.sin_port), seq_num);


                    // Check sequence number and send ACK
                    if ((seq_num <= KTP_sockets[ktp_socket_id].rwnd.expected_seq_number + WINDOW_SIZE - 1 ) && seq_num>=KTP_sockets[ktp_socket_id].rwnd.expected_seq_number)
                    {
                        //check if inorder packet
                        if (KTP_sockets[ktp_socket_id].rwnd.expected_seq_number == seq_num){

                            // add message to ktp recvr sockt
                            
                            KTP_sockets[ktp_socket_id].rwnd.rwndWindow--;
                            strcpy(KTP_sockets[ktp_socket_id].rwnd.messageBuffer[KTP_sockets[ktp_socket_id].rwnd.end], recv_buffer);
                            KTP_sockets[ktp_socket_id].rwnd.rcvd[KTP_sockets[ktp_socket_id].rwnd.end] = true;

                            KTP_sockets[ktp_socket_id].rwnd.end = (KTP_sockets[ktp_socket_id].rwnd.end + 1) % WINDOW_SIZE;
                            KTP_sockets[ktp_socket_id].rwnd.expected_seq_number = (KTP_sockets[ktp_socket_id].rwnd.expected_seq_number + 1) % MAX_SEQ;
                            
                            
                            
                            // send Ack message
                            struct sockaddr_in source;
                            source.sin_family = AF_INET;
                            inet_aton(KTP_sockets[ktp_socket_id].ipAddress, &source.sin_addr );
                            source.sin_port = htons(KTP_sockets[ktp_socket_id].port);

                            //kitney packet aagey ke pehele se mil gaye??
                            int cnt=0;
                            while (KTP_sockets[ktp_socket_id].rwnd.rcvd[cnt + 1]){
                                cnt++;
                            }

                            SendAck(seq_num + cnt, KTP_sockets[ktp_socket_id].rwnd.rwndWindow, (struct sockaddr *)&source);
                        }
                        else{

                            //out of order message

                            printf("Out of order message stored in buuffer\n");

                            int out_order_index = seq_num - KTP_sockets[ktp_socket_id].rwnd.expected_seq_number;

                            //check if this message already rcvd
                            if (KTP_sockets[ktp_socket_id].rwnd.rcvd[ KTP_sockets[ktp_socket_id].rwnd.end + out_order_index ]) {
                                //duplicate packet
                                printf("Duplicate packet\n");
                                continue;
                            
                            }

                            KTP_sockets[ktp_socket_id].rwnd.rcvd[KTP_sockets[ktp_socket_id].rwnd.end + out_order_index] = true;
                            KTP_sockets[ktp_socket_id].rwnd.end = (KTP_sockets[ktp_socket_id].rwnd.end + out_order_index) % WINDOW_SIZE;
                            KTP_sockets[ktp_socket_id].rwnd.rwndWindow--;
                            strcpy(KTP_sockets[ktp_socket_id].rwnd.messageBuffer[KTP_sockets[ktp_socket_id].rwnd.end], recv_buffer);

                        }
                        
                    }
                    else{
                        //rcvd out of order message so drop it 
                        printf("Crazy packet mil gya %d\n", seq_num);
                        //eat 5 star do nothing
                    }
                }
                else if(recv_buffer[0]=='A')
                {
                    //recvd an Acknowledgement packet
                    //remove from sender side buffer
                    int ack_till_seq = recv_buffer[1] - '0';

                    int recvr_side_buffer_size = recv_buffer[2] - '0';
                    KTP_sockets[ktp_socket_id].swnd.swndSize =  recvr_side_buffer_size ;

                    int base_msg_seq_num = KTP_sockets[ktp_socket_id].swnd.messageBuffer[KTP_sockets[ktp_socket_id].swnd.base].seq;
                    KTP_sockets[ktp_socket_id]
                        .swnd.base = (KTP_sockets[ktp_socket_id].swnd.base + ack_till_seq - base_msg_seq_num) % WINDOW_SIZE;
                }
                
            }
        }
    }


}

void *S(void *arg)
{
    printf("Reliable transmission thread running...\n");
    fflush(stdout);
    while (1)
    {
        sleep(timeout / 2); // Wait half the timeout period
        time_t curr_time = time(NULL);

        //check for timeouts for any message in ktp socket buffer
        for(int i = 0; i < N; i++){
            if (KTP_sockets[i].isFree || KTP_sockets[i].swnd.base == KTP_sockets[i].swnd.end)
                continue;

            // Check if the socket is valid
            int flags = fcntl(KTP_sockets[i].udpSocketId, F_GETFL, 0);
            if (flags == -1)
            {
                perror("[ERROR] Invalid socket detected in S()");
                continue; // Skip this socket
            }

            //check if the message of base had timed out
            if(curr_time - KTP_sockets[i].swnd.messageBuffer[ KTP_sockets[i].swnd.base].send_time >= timeout)
            {

                for (int j = KTP_sockets[i].swnd.base; j <= KTP_sockets[i].swnd.end; j++)
                {
                    
                    // print debugging statment that message is being resent
                    
                    printf("\n[Timeout] Message %s resent from ktp socket id : %d\n\n", KTP_sockets[i].swnd.messageBuffer[j].msg,i);
                    // Send the message again
                    SendMsg(&KTP_sockets[i].swnd.messageBuffer[j], i, KTP_sockets[i].swnd.messageBuffer[j].seq);
                    //update time 
                    KTP_sockets[i].swnd.messageBuffer[j].send_time = curr_time;
                }
            }
        }
    }

}

void initialize()
{
    
    if(flagInitalized) return ;

    int shmid = shmget(SHM_KEY, sizeof(struct KTP_socket) * N, IPC_CREAT | 0666);
    if (shmid < 0)
    {
        perror("[ERROR] Shared memory allocation failed");
        exit(EXIT_FAILURE);
    }

    KTP_sockets = (struct KTP_socket *)shmat(shmid, NULL, 0);
    if (KTP_sockets == (void *)-1)
    {
        perror("[ERROR] Shared memory attachment failed");
        exit(EXIT_FAILURE);
    }

    printf("[INIT] Shared memory attached successfully\n");

    for (int i = 0; i < N; i++)
    {
        KTP_sockets[i].isFree = true;
        KTP_sockets[i].processId = -1;
        KTP_sockets[i].udpSocketId = socket(AF_INET, SOCK_DGRAM, 0);
        if (KTP_sockets[i].udpSocketId < 0)
        {
            perror("Failed to create UDP socket");
            exit(EXIT_FAILURE);
        }
        KTP_sockets[i].port = -1;

        KTP_sockets[i].swnd.base = 0;
        KTP_sockets[i].swnd.end = 0;
        KTP_sockets[i].swnd.swndWindow = WINDOW_SIZE;
        KTP_sockets[i].swnd.seq_number = 1;                 //start with seqnace nunmber 1

        // for(int j=0;j<WINDOW_SIZE;j++){
        //     KTP_sockets[i].swnd.notAcknowledged[j] = false;
        // }
        KTP_sockets[i].rwnd.rwndWindow = WINDOW_SIZE;
        KTP_sockets[i].rwnd.base = 0;
        KTP_sockets[i].rwnd.expected_seq_number = 1;        //expected start with seqnace nunmber 1
        for (int j = 0; j < WINDOW_SIZE; j++) {
            //KTP_sockets[i].rwnd.messageBuffer[j] = '\0';
            KTP_sockets[i].rwnd.rcvd[j]=false;
        }
    }

    pthread_t r_thread, s_thread;
    
    
    
    if (pthread_create(&r_thread, NULL, R, NULL) != 0)
    {
        perror("Receiver thread creation failed");
        return ;
    }
    
    if (pthread_create(&s_thread, NULL, S, NULL) != 0)
    {
        perror("Sender thread creation failed");
        return ;
    }
    
    pthread_detach(r_thread); 
    pthread_detach(s_thread); 
    
    flagInitalized = true;
    return ; // Return success
}

int k_socket(int family, int protocol, int flag, int processID)
{
    int free_slot = -1;
    for (int i = 0; i < N; i++)
    {
        if (KTP_sockets[i].isFree)
        {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1)
    {
        return ENOSPACE;
    }

   
    KTP_sockets[free_slot].isFree = false;
    KTP_sockets[free_slot].processId = processID;

    return free_slot;
}

int k_bind(int ktp_socketId, struct sockaddr *source, struct sockaddr *destination)
{
    if (ktp_socketId < 0 || ktp_socketId >= N || KTP_sockets[ktp_socketId].isFree)
    {
        return INVALIDSOCKET;
    }

    if (bind(KTP_sockets[ktp_socketId].udpSocketId, source, sizeof(*source)) < 0)
    {
        return BINDFAILED;
    }

    // Store destination address
    struct sockaddr_in *dest = (struct sockaddr_in *)destination;
    strncpy(KTP_sockets[ktp_socketId].ipAddress,
            inet_ntoa(dest->sin_addr),
            INET_ADDRSTRLEN);
    KTP_sockets[ktp_socketId].port = ntohs(dest->sin_port);

    return 0;
}

int k_sendto(int ktp_sockfd_id, char *mess, int maxSize, int flag, struct sockaddr *destination)
{
    if (ktp_sockfd_id < 0 || ktp_sockfd_id >= N || KTP_sockets[ktp_sockfd_id].isFree)
    {
        return INVALIDSOCKET;
    }
    if (KTP_sockets[ktp_sockfd_id].swnd.swndWindow == 0)
    {
        global_error = ENOSPACE;
        printf("[ERROR] Send buffer full for socket %d\n", ktp_sockfd_id);
        return ENOSPACE;
    }

    struct message m;
    //strncpy(m.msg, mess, MAX_MSG_SIZE - 1);
    strcpy(m.msg,mess);
    m.msg[strlen(m.msg)-1]='\0';

    int seq_num = KTP_sockets[ktp_sockfd_id].swnd.seq_number;
    m.seq = seq_num;

    time_t curr_time = time(NULL);
    m.send_time = curr_time;


    KTP_sockets[ktp_sockfd_id].swnd.seq_number = (KTP_sockets[ktp_sockfd_id].swnd.seq_number + 1)%MAX_SEQ;
    
    printf("[SEND] Attempting to send %s message to destination port %d\n", m.msg, KTP_sockets[ktp_sockfd_id].port);
    SendMsg(&m, ktp_sockfd_id, seq_num);
    
    //add to ktp socket buffer
    //KTP_sockets[ktp_sockfd_id].swnd.notAcknowledged[KTP_sockets[ktp_sockfd_id].swnd.end] = true;
    KTP_sockets[ktp_sockfd_id].swnd.messageBuffer[KTP_sockets[ktp_sockfd_id].swnd.end] = m;
    KTP_sockets[ktp_sockfd_id].swnd.end = (KTP_sockets[ktp_sockfd_id].swnd.end + 1) % WINDOW_SIZE; // MAX SEQ NUM
    KTP_sockets[ktp_sockfd_id].swnd.swndWindow--; //decrease buffer size
    return SENDFAILED;
}

int k_recvfrom(int ktp_sockfd, char *message, int maxSize, int flag, struct sockaddr *source)
{
    if (ktp_sockfd < 0 || ktp_sockfd >= N || KTP_sockets[ktp_sockfd].isFree)
    {
        return INVALIDSOCKET;
    }
    while (KTP_sockets[ktp_sockfd].rwnd.base == KTP_sockets[ktp_sockfd].rwnd.end)
    {
        printf("[ERROR] No message available in receive buffer for socket %d\n", ktp_sockfd);
        sleep(10);

        // return -1; // No message available
    }

    printf("[RECV] Waiting for message on socket %d (port %d)\n",
           ktp_sockfd, KTP_sockets[ktp_sockfd].port);

    
    //read from recieve buffer and delete the message from recvr buffer
    strcpy(message, KTP_sockets[ktp_sockfd].rwnd.messageBuffer[KTP_sockets[ktp_sockfd].rwnd.base]);

    //phele se hi iske message me hona chahiye tab recieve call karna nahi to error dega ....yaha see
    //strcpy(message,  KTP_sockets[ktp_sockfd].rwnd.messageBuffer[KTP_sockets[ktp_sockfd].rwnd.base].msg);

    //delete the base th message
    KTP_sockets[ktp_sockfd].rwnd.rcvd[KTP_sockets[ktp_sockfd].rwnd.base] = false;
    KTP_sockets[ktp_sockfd].rwnd.base = (KTP_sockets[ktp_sockfd].rwnd.base + 1) % WINDOW_SIZE;  //overwrite hojaega kabhi na kabhi :)

    return strlen(message);
}

void k_close(int ktp_sockfd, struct sockaddr *dest)
{
    if (ktp_sockfd < 0 || ktp_sockfd >= N || KTP_sockets[ktp_sockfd].isFree)
    {
        return; // Invalid socket or already closed
    }


    KTP_sockets[ktp_sockfd].isFree = true;
    KTP_sockets[ktp_sockfd].processId = -1;

    KTP_sockets[ktp_sockfd].port = -1;

    KTP_sockets[ktp_sockfd].swnd.base = 0;
    KTP_sockets[ktp_sockfd].swnd.end = 0;
    KTP_sockets[ktp_sockfd].swnd.swndWindow = WINDOW_SIZE;
    KTP_sockets[ktp_sockfd].swnd.seq_number = 1; // start with seqnace nunmber 1

    // for (int j = 0; j < WINDOW_SIZE; j++)
    // {
    //     KTP_sockets[ktp_sockfd].swnd.notAcknowledged[j] = false;
    // }
    KTP_sockets[ktp_sockfd].rwnd.rwndWindow = WINDOW_SIZE;
    KTP_sockets[ktp_sockfd].rwnd.base = 0;
    KTP_sockets[ktp_sockfd].rwnd.expected_seq_number = 1; // expected start with seqnace nunmber 1



    printf("[CLOSE] Socket %d closed\n", ktp_sockfd);
}


