#include "ktpheader.h"

#define SHM_KEY 1234 // Unique key for shared memory

#define NOSPACE true
// golabl
struct KTP_socket *KTP_sockets;
sem_t *semaphore; // Binary semaphore for mutual exclusion
int global_error;
bool flag_no_space = false;

static int dropMessage(float p)
{
    float r = (float)rand() / (float)RAND_MAX;
    return (r < p); // Returns 1 if message should be dropped, 0 if sent
}

static void SendMsg(struct message *Message, int ktp_socket_id, int seq_num)
{
    if (seq_num < 0)
        return;

    if (dropMessage(P))
    {
        printf("[DROP] Message dropped (probability check) with seq: %d\n", seq_num);
        // for(int i = 0; i <2; i++)printf("__________________________________\n");
        return; // Message is dropped, do not send
    }

    struct KTP_socket *ktp_socket = &KTP_sockets[ktp_socket_id];
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_addr.s_addr = inet_addr(ktp_socket->ipAddress);
    servaddr.sin_port = htons(ktp_socket->port);
    servaddr.sin_family = AF_INET;

    char buffer[MAX_MSG_SIZE + 6]; // 'D ', 3-digit seq num, space, and message
    snprintf(buffer, sizeof(buffer), "D%03d%s", seq_num, Message->msg);

    sendto(ktp_socket->udpSocketId, buffer, strlen(buffer) + 1, 0,
           (struct sockaddr *)&servaddr, sizeof(servaddr));

    printf("[SUCCESSFUL UDP SEND] Message \"%s\" sent with seq: %d\n",
           buffer, seq_num);
    // for (int i = 0; i < 2; i++)
    //     printf("_____________________________________\n");
}
// void *clean(void *arg)
// {
//     while (1)
//     {
//         sleep(T * 2);
//         down(semaphore);
//         for (int i = 0; i < N; i++)
//         {
//             if (!KTP_sockets[i].isFree)
//             {
//                 if ((kill(SM[i].pid, 0)) == -1)
//                 {
//                     printf("Process %d terminated\n", i);
//                     close(SM[i].socket);
//                     SM[i].socket = 0;
//                     SM[i].state = FREE;
//                 }
//             }
//         }
//         V(semid);
//     }
//     return NULL;
// }

static void SendAck(int ktp_socket_id, int seq_num, int rwnd, struct sockaddr *destination)
{

    // make ack packet and send to the destination
    // if (!dropMessage(P))
    // {
    // printf("[DROP] ACK dropped with seq: %d\n",seq_num);
    // return;
    // }

    // make ack packet
    char buffer[MAX_MSG_SIZE];
    sprintf(buffer, "A%03d%01d", seq_num, rwnd);
    sendto(KTP_sockets[ktp_socket_id].udpSocketId, buffer, strlen(buffer) + 1, 0, destination, sizeof(*destination));
    printf("[SUCCESSFUL UDP ACK SEND] Message sent with msg %s to port: %d\n", buffer, ntohs(((struct sockaddr_in *)destination)->sin_port));
}

void get_shared_resources()
{
    int shmid = shmget(SHM_KEY, sizeof(struct KTP_socket) * N, 0666);
    if (shmid < 0)
    {
        perror("[ERROR] Shared memory access failed");
        return;
    }

    KTP_sockets = (struct KTP_socket *)shmat(shmid, NULL, 0);
    if (KTP_sockets == (void *)-1)
    {
        perror("[ERROR] Shared memory attachment failed");
        return;
    }

    // Open the semaphore
    semaphore = sem_open("/ktp_sem", 0);
    if (semaphore == SEM_FAILED)
    {
        perror("[ERROR] Semaphore access failed");

        return;
    }
    return;
}

int getSeqNum(char *msg)
{
    int a = msg[1] - '0';
    int b = msg[2] - '0';
    int c = msg[3] - '0';

    return a * 100 + b * 10 + c;
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
        tv.tv_sec = T;
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
            // printf("\n\t\t\tNo data in selected socket\n");
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
                printf("\n\t\t\t\t\t\t\t\t\tData in selected socket \n\t\t\t\t\t\t\t\t\tRcvd: %s\n\n", recv_buffer);
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
                    // 3 char ko int me convert karna hai

                    int seq_num = getSeqNum(recv_buffer);

                    // Check sequence number and send ACK
                    if ((seq_num <= KTP_sockets[ktp_socket_id].rwnd.expected_seq_number + WINDOW_SIZE - 1) && seq_num >= KTP_sockets[ktp_socket_id].rwnd.expected_seq_number)
                    {
                        // check if inorder packet
                        if (KTP_sockets[ktp_socket_id].rwnd.expected_seq_number == seq_num)
                        {
                            printf("[RECV] Inorder Data packet with seq: %d\n", seq_num);

                            // add message to ktp recvr sockt
                            KTP_sockets[ktp_socket_id].rwnd.rwndWindow--;
                            strcpy(KTP_sockets[ktp_socket_id].rwnd.messageBuffer[KTP_sockets[ktp_socket_id].rwnd.end], recv_buffer + 4);
                            printf("Recv buffer updated with  message buffer [%d] = %s \n\n", KTP_sockets[ktp_socket_id].rwnd.end, KTP_sockets[ktp_socket_id].rwnd.messageBuffer[KTP_sockets[ktp_socket_id].rwnd.end]);
                            KTP_sockets[ktp_socket_id]
                                .rwnd.rcvd[KTP_sockets[ktp_socket_id].rwnd.end] = true;
                            KTP_sockets[ktp_socket_id].rwnd.end = (KTP_sockets[ktp_socket_id].rwnd.end + 1) % WINDOW_SIZE;
                            KTP_sockets[ktp_socket_id].rwnd.expected_seq_number = (KTP_sockets[ktp_socket_id].rwnd.expected_seq_number + 1) % MAX_SEQ;

                            // send Ack message
                            struct sockaddr_in source;
                            source.sin_family = AF_INET;
                            inet_aton(KTP_sockets[ktp_socket_id].ipAddress, &source.sin_addr);
                            source.sin_port = htons(KTP_sockets[ktp_socket_id].port);

                            // kitney packet aagey ke pehele se mil gaye??
                            int cnt = 0;
                            while (KTP_sockets[ktp_socket_id].rwnd.rcvd[(seq_num + cnt) % WINDOW_SIZE])
                            {
                                cnt++;
                                KTP_sockets[ktp_socket_id].rwnd.expected_seq_number = (KTP_sockets[ktp_socket_id].rwnd.expected_seq_number + 1) % MAX_SEQ;
                                KTP_sockets[ktp_socket_id].rwnd.end = (KTP_sockets[ktp_socket_id].rwnd.end + 1) % WINDOW_SIZE;
                                printf("Agey ke pehele se milgya  expected : %d \n", KTP_sockets[ktp_socket_id].rwnd.expected_seq_number);
                            }

                            SendAck(ktp_socket_id, seq_num + cnt, KTP_sockets[ktp_socket_id].rwnd.rwndWindow, (struct sockaddr *)&source);
                            if (KTP_sockets[ktp_socket_id].rwnd.rwndWindow == 0)
                            {
                                printf("Window is full, to prevent deadlock we need to continuous send the Ack\n");
                                flag_no_space = NOSPACE;
                                while (KTP_sockets[ktp_socket_id].rwnd.rwndWindow)
                                {
                                    sleep(10);
                                    SendAck(ktp_socket_id, seq_num + cnt, KTP_sockets[ktp_socket_id].rwnd.rwndWindow, (struct sockaddr *)&source);
                                }
                            }
                        }
                        else
                        {

                            // out of order message
                            printf("[RECV] Outorder Data packet with seq: %d\n", seq_num);
                            int out_order_index = seq_num - KTP_sockets[ktp_socket_id].rwnd.expected_seq_number;
                            // Ensure out_order_index is within valid range
                            if (out_order_index < 0 || out_order_index >= WINDOW_SIZE)
                            {
                                printf("ERROR: Invalid out_of_order index: %d\n", out_order_index);
                                continue;
                            }
                            int buffer_index = (KTP_sockets[ktp_socket_id].rwnd.end + out_order_index) % WINDOW_SIZE;

                            // Check if this message has already been received
                            if (KTP_sockets[ktp_socket_id].rwnd.rcvd[buffer_index])
                            {
                                printf("Duplicate packet detected, ignoring\n");
                                continue;
                            }

                            KTP_sockets[ktp_socket_id].rwnd.rcvd[buffer_index] = true;
                            // KTP_sockets[ktp_socket_id].rwnd.end = buffer_index;
                            KTP_sockets[ktp_socket_id].rwnd.rwndWindow--;
                            memset(KTP_sockets[ktp_socket_id].rwnd.messageBuffer[buffer_index], 0, MAX_MSG_SIZE); // Clear before copying
                            strncpy(KTP_sockets[ktp_socket_id].rwnd.messageBuffer[buffer_index], recv_buffer + 4, MAX_MSG_SIZE - 1);
                            KTP_sockets[ktp_socket_id].rwnd.messageBuffer[buffer_index][recv_len - 1] = '\0'; // Ensure null termination
                            printf("Recv buffer updated with  message buffer [%d] = %s\n\n ", buffer_index, KTP_sockets[ktp_socket_id].rwnd.messageBuffer[buffer_index]);
                        }
                    }
                    else if (seq_num < KTP_sockets[ktp_socket_id].rwnd.expected_seq_number)
                    {
                        // rcvd out of order message so drop it
                        printf("Out of order packet with seq num: %d sending duplicate Ack\n", seq_num);

                        // send Duplicate Ack message
                        struct sockaddr_in source;
                        source.sin_family = AF_INET;
                        inet_aton(KTP_sockets[ktp_socket_id].ipAddress, &source.sin_addr);
                        source.sin_port = htons(KTP_sockets[ktp_socket_id].port);
                        SendAck(ktp_socket_id, seq_num, KTP_sockets[ktp_socket_id].rwnd.rwndWindow, (struct sockaddr *)&source);
                    }
                    else
                    {
                        printf("Out of order packet with seq num: %d\n", seq_num);
                    }
                }
                else if (recv_buffer[0] == 'A')
                {
                    // recvd an Acknowledgement packet
                    // remove from sender side buffer
                    int ack_till_seq = getSeqNum(recv_buffer);

                    int recvr_side_buffer_size = recv_buffer[4] - '0';

                    int base_msg_seq_num = KTP_sockets[ktp_socket_id].swnd.messageBuffer[KTP_sockets[ktp_socket_id].swnd.base].seq;
                    if (ack_till_seq < base_msg_seq_num)
                    {
                        printf("Duplciate Ack packet \n");
                        continue;
                    }
                    printf("Base *index* KtpSocket[%d] updated to: %d + %d - %d  + 1 = %d\n", ktp_socket_id, KTP_sockets[ktp_socket_id].swnd.base, ack_till_seq, base_msg_seq_num, KTP_sockets[ktp_socket_id].swnd.base + ack_till_seq - base_msg_seq_num + 1);
                    // restore buffer to default
                    int cnt = 0;

                    while (cnt < ack_till_seq - base_msg_seq_num + 1)
                    {
                        printf("Making msgBuffer[%d] to default\n", KTP_sockets[ktp_socket_id].swnd.base + cnt);
                        KTP_sockets[ktp_socket_id].swnd.messageBuffer[KTP_sockets[ktp_socket_id].swnd.base + cnt++].seq = -1; // making packet default
                    }
                    KTP_sockets[ktp_socket_id].swnd.swndWindow = recvr_side_buffer_size;
                    printf("Swnd size: %d\n", recvr_side_buffer_size);

                    KTP_sockets[ktp_socket_id]
                        .swnd.base = (KTP_sockets[ktp_socket_id].swnd.base + ack_till_seq - base_msg_seq_num + 1) % WINDOW_SIZE;
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

        // check for timeouts for any message in ktp socket buffer

        for (int i = 0; i < 1; i++) // change to N --------------->
        {
            if (KTP_sockets[i].isFree)
                continue;
            // check if the message of base had timed out
            if (curr_time - KTP_sockets[i].swnd.messageBuffer[KTP_sockets[i].swnd.base].send_time >= timeout && KTP_sockets[i].swnd.swndWindow > 0)
            {

                // for (int j = KTP_sockets[i].swnd.base; j < KTP_sockets[i].swnd.end; j++)
                int j = KTP_sockets[i].swnd.base;
                while (KTP_sockets[i].swnd.messageBuffer[j].seq != -1)
                {
                    // print debugging statment that message is being resent
                    printf("\n[Timeout] Message [%s] from seq : %d , base: %d ,end : %d and j = %d\n",
                           KTP_sockets[i].swnd.messageBuffer[j].msg,
                           KTP_sockets[i].swnd.messageBuffer[j].seq,
                           KTP_sockets[i].swnd.base, KTP_sockets[i].swnd.end,
                           j);

                    // Send the message again
                    SendMsg(&KTP_sockets[i].swnd.messageBuffer[j], i, KTP_sockets[i].swnd.messageBuffer[j].seq);
                    // update time
                    KTP_sockets[i].swnd.messageBuffer[j].send_time = curr_time;
                    j = (j + 1) % WINDOW_SIZE;
                }
            }
        }
    }
}

void initialize()
{
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

    // printf("[INIT] Shared memory attached successfully\n");

    // Initialize binary semaphore
    semaphore = sem_open("/ktp_sem", O_CREAT, 0666, 1);
    if (semaphore == SEM_FAILED)
    {
        perror("[ERROR] Semaphore initialization failed");
        exit(EXIT_FAILURE);
    }

    // Initialize KTP sockets
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
        KTP_sockets[i].isBinded = false;

        KTP_sockets[i].swnd.base = 0;
        KTP_sockets[i].swnd.end = 0;
        KTP_sockets[i].swnd.swndWindow = WINDOW_SIZE;
        KTP_sockets[i].swnd.seq_number = 1;
        for (int j = 0; j < WINDOW_SIZE; j++)
        {
            KTP_sockets[i].swnd.messageBuffer[j].seq = -1;
            strcpy(KTP_sockets[i].swnd.messageBuffer[j].msg, "Dummy Message");
        }

        KTP_sockets[i].rwnd.rwndWindow = WINDOW_SIZE;
        KTP_sockets[i].rwnd.base = 0;
        KTP_sockets[i].rwnd.end = 0;
        KTP_sockets[i].rwnd.expected_seq_number = 1;
        for (int j = 0; j < WINDOW_SIZE; j++)
        {
            KTP_sockets[i].rwnd.rcvd[j] = false;
            strcpy(KTP_sockets[i].rwnd.messageBuffer[j], "Dummy Message");
        }
    }

    // Start threads
    pthread_t r_thread, s_thread;
    if (pthread_create(&r_thread, NULL, R, NULL) != 0)
    {
        perror("Receiver thread creation failed");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&s_thread, NULL, S, NULL) != 0)
    {
        perror("Sender thread creation failed");
        exit(EXIT_FAILURE);
    }

    pthread_detach(r_thread);
    pthread_detach(s_thread);

    printf("[INIT] KTP service initialized successfully\n");
}

int main()
{
    initialize();
    get_shared_resources();
    printf("KTP service is running...\n");
    while (1)
    {
        // printf("\n\nChecking shared resources\n");
        for (int i = 0; i < N; i++)
        {
            // printf("Socket %d isFree: %s, Port: %d, UDP FD: %d\n",
            //        i, KTP_sockets[i].isFree ? "true" : "false",
            //        !KTP_sockets[i].isFree ? KTP_sockets[i].port : -1,
            //        KTP_sockets[i].udpSocketId
            //     );

            if (!KTP_sockets[i].isFree && !KTP_sockets[i].isBinded)
            {
                struct sockaddr_in *clientAddr = (struct sockaddr_in *)&KTP_sockets[i].client;
                socklen_t addr_len = sizeof(*clientAddr);
                if (bind(KTP_sockets[i].udpSocketId, (struct sockaddr *)clientAddr, addr_len) < 0)
                {
                    perror("Failed to bind UDP socket");
                    continue; // Prevents exiting the loop, allowing retries
                }
                else
                {
                    printf("Successfully bind   ktp Socket [%d] to UDP socket %d to %s:%d\n",
                           i,
                           KTP_sockets[i].udpSocketId,
                           inet_ntoa(clientAddr->sin_addr),
                           ntohs(clientAddr->sin_port));
                    KTP_sockets[i].isBinded = true;
                }
            }
        }
        sleep(10); // Keep checking periodically
    }
}