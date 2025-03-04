#include "ktpheader.h"

#define SHM_KEY 1234 // Unique key for shared memory

// Define global variables
sem_t *semaphore = NULL;
struct KTP_socket *KTP_sockets = NULL;

// Function to get shared resources
struct KTP_socket *get_shared_resources()
{
    if (KTP_sockets != NULL)
    {
        return KTP_sockets; // Avoid multiple `shmat` calls
    }

    int shmid = shmget(SHM_KEY, sizeof(struct KTP_socket) * N, 0666);
    if (shmid < 0)
    {
        perror("[ERROR] Shared memory access failed");
        return NULL;
    }

    KTP_sockets = (struct KTP_socket *)shmat(shmid, NULL, 0);
    if (KTP_sockets == (void *)-1)
    {
        perror("[ERROR] Shared memory attachment failed");
        return NULL;
    }

    // Open the semaphore
    if (!semaphore)
    {
        semaphore = sem_open("/ktp_sem", 0);
        if (semaphore == SEM_FAILED)
        {
            perror("[ERROR] Semaphore access failed");
            shmdt(KTP_sockets); // Detach shared memory
            return NULL;
        }
    }

    return KTP_sockets;
}

// Cleanup function to detach shared memory and close semaphore
void cleanup()
{
    if (KTP_sockets != NULL)
    {
        shmdt(KTP_sockets);
        KTP_sockets = NULL;
    }
    if (semaphore != NULL)
    {
        sem_close(semaphore);
        semaphore = NULL;
    }
}

int k_socket(int family, int protocol, int flag, int processID)
{
    sem_wait(semaphore);
    struct KTP_socket *sockets = get_shared_resources();
    if (!sockets)
    {
        sem_post(semaphore);
        return -1;
    }

    int free_slot = -1;
    for (int i = 0; i < N; i++)
    {
        if (sockets[i].isFree)
        {
            free_slot = i;
            break;
        }
    }
    if (free_slot == -1)
    {
        sem_post(semaphore);
        return ENOSPACE;
    }
    sockets[free_slot].isFree = false;
    sockets[free_slot].processId = processID;
    sem_post(semaphore);
    return free_slot;
}

int k_sendto(int ktp_sockfd_id, char *mess, int maxSize, int flag, struct sockaddr *destination)
{
    sem_wait(semaphore); // Lock semaphore
    struct KTP_socket *sockets = get_shared_resources();
    if (!sockets)
    {
        sem_post(semaphore); // Unlock semaphore
        return -1;
    }

    if (ktp_sockfd_id < 0 || ktp_sockfd_id >= N || sockets[ktp_sockfd_id].isFree)
    {
        sem_post(semaphore); // Unlock semaphore
        return INVALIDSOCKET;
    }

    if (sockets[ktp_sockfd_id].swnd.base == (sockets[ktp_sockfd_id].swnd.end) % WINDOW_SIZE && sockets[ktp_sockfd_id].swnd.messageBuffer->seq != -1)
    {
        printf("[LIB] ENOSPACE\n");
        sem_post(semaphore); // Unlock semaphore
        return ENOSPACE;
    }

    struct message m;
    strcpy(m.msg, mess);
    if (strlen(m.msg) < maxSize)
        m.msg[strlen(m.msg) - 1] = '\0';
    else
        m.msg[strlen(m.msg) - 1] = '\0';
    int seq_num = sockets[ktp_sockfd_id].swnd.seq_number;
    m.seq = seq_num;
    // time_t curr_time = time(NULL);
    m.send_time = 0; // to send immediately
    sockets[ktp_sockfd_id].swnd.seq_number = (sockets[ktp_sockfd_id].swnd.seq_number + 1) % MAX_SEQ;
    // SendMsg(&m, ktp_sockfd_id, seq_num);
    //  Add to ktp socket buffer

    printf("Sending message with seq %d and %d and base %d\n", sockets[ktp_sockfd_id].swnd.seq_number, sockets[ktp_sockfd_id].swnd.end, sockets[ktp_sockfd_id].swnd.base);

    sockets[ktp_sockfd_id]
        .swnd.messageBuffer[sockets[ktp_sockfd_id].swnd.end] = m;
    printf("[ATTEMPT TO SEND] Seq: %d --> Buffer[%d] = [%s]\n\n", sockets[ktp_sockfd_id].swnd.messageBuffer[sockets[ktp_sockfd_id].swnd.end].seq, sockets[ktp_sockfd_id].swnd.end, sockets[ktp_sockfd_id].swnd.messageBuffer[sockets[ktp_sockfd_id].swnd.end].msg);
    sockets[ktp_sockfd_id].swnd.end = (sockets[ktp_sockfd_id].swnd.end + 1) % WINDOW_SIZE;

    sem_post(semaphore); // Unlock semaphore
    sleep(10);
    return 1;
}

int k_bind(int ktp_socketId, struct sockaddr *source, struct sockaddr *destination)
{
    printf("Waiting to bind....");
    sem_wait(semaphore); // Lock semaphore
    struct KTP_socket *sockets = get_shared_resources();
    if (!sockets)
    {
        sem_post(semaphore); // Unlock semaphore
        return -1;
    }

    if (ktp_socketId < 0 || ktp_socketId >= N || sockets[ktp_socketId].isFree)
    {
        sem_post(semaphore); // Unlock semaphore
        return INVALIDSOCKET;
    }

    struct sockaddr_in *src = (struct sockaddr_in *)source;
    struct sockaddr_in *dest = (struct sockaddr_in *)destination;

    // Store the client's IP and port
    sockets[ktp_socketId].client = *source;

    // Store the destination IP and port
    strncpy(sockets[ktp_socketId].ipAddress, inet_ntoa(dest->sin_addr), INET_ADDRSTRLEN);
    sockets[ktp_socketId].port = ntohs(dest->sin_port);

    sem_post(semaphore); // Unlock semaphore

    // while(!sockets[ktp_socketId].isBinded){
    //     //loop till bindind successfully
    //     sleep(5);
    // }

    printf("Successfully binded socket %d to %s:%d\n", ktp_socketId, inet_ntoa(src->sin_addr), ntohs(src->sin_port));
    return 0;
}

int k_recvfrom(int ktp_sockfd, char *message, int maxSize, int flag, struct sockaddr *source)
{
    sem_wait(semaphore);
    struct KTP_socket *sockets = get_shared_resources();
    if (!sockets)
    {
        sem_post(semaphore);
        return -1;
    }

    if (ktp_sockfd < 0 || ktp_sockfd >= N || sockets[ktp_sockfd].isFree)
    {
        sem_post(semaphore);
        return INVALIDSOCKET;
    }

    while (sockets[ktp_sockfd].rwnd.base == sockets[ktp_sockfd].rwnd.end)
    {
        sem_post(semaphore);
        printf("No data so waiting ....\n");
        sleep(100);
        sem_wait(semaphore);
    }

    strncpy(message, sockets[ktp_sockfd].rwnd.messageBuffer[sockets[ktp_sockfd].rwnd.base], maxSize);
    message[maxSize - 1] = '\0';
    printf("Socket[%d] .rwnd . base is %d  with message : %s\n", ktp_sockfd, sockets[ktp_sockfd].rwnd.base, message);
    sockets[ktp_sockfd].rwnd.rcvd[sockets[ktp_sockfd].rwnd.base] = false;
    sockets[ktp_sockfd].rwnd.base = (sockets[ktp_sockfd].rwnd.base + 1) % WINDOW_SIZE;
    sockets[ktp_sockfd].rwnd.rwndWindow++;

    sem_post(semaphore);
    return strlen(message);
}

void k_close(int ktp_sockfd)
{
    printf("Waiting to send whole data\n");
    fflush(stdout);
    struct KTP_socket *sockets = get_shared_resources();
    while (sockets[ktp_sockfd].swnd.base != sockets[ktp_sockfd].swnd.end)
    {
        printf("Waiting to send whole data %d and %d \n", sockets[ktp_sockfd].swnd.base, sockets[ktp_sockfd].swnd.end);
        fflush(stdout);
        sleep(100);
    }
    sem_wait(semaphore);
    if (!sockets)
    {
        sem_post(semaphore);
        return;
    }

    if (ktp_sockfd < 0 || ktp_sockfd >= N || sockets[ktp_sockfd].isFree)
    {
        sem_post(semaphore);
        return;
    }

    sockets[ktp_sockfd].isFree = true;
    sockets[ktp_sockfd].processId = -1;
    sockets[ktp_sockfd].udpSocketId = socket(AF_INET, SOCK_DGRAM, 0); // new socket
    sockets[ktp_sockfd].port = -1;
    sockets[ktp_sockfd].swnd.base = 0;
    sockets[ktp_sockfd].swnd.end = 0;
    sockets[ktp_sockfd].swnd.swndWindow = WINDOW_SIZE;
    sockets[ktp_sockfd].swnd.seq_number = 1;
    sockets[ktp_sockfd].rwnd.rwndWindow = WINDOW_SIZE;
    sockets[ktp_sockfd].rwnd.base = 0;
    sockets[ktp_sockfd].rwnd.expected_seq_number = 1;

    printf("[CLOSE] Socket %d closed\n", ktp_sockfd);
    sem_post(semaphore);
}
