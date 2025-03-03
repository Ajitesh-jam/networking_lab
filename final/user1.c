#include "ktpheader.h"

#define END_MARKER "d"
#define BUFFER_SIZE 512
#define MAX_FILENAME_LENGTH 256
#define MAX_IP_LENGTH 16

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        printf("Usage: %s <src_IP> <src_port> <dest_IP> <dest_port>\n", argv[0]);
        return 1;
    }

    char src_ip[MAX_IP_LENGTH], dest_ip[MAX_IP_LENGTH];
    if (strlen(argv[1]) >= MAX_IP_LENGTH || strlen(argv[3]) >= MAX_IP_LENGTH)
    {
        printf("Error: IP address too long\n");
        return 1;
    }
    strncpy(src_ip, argv[1], MAX_IP_LENGTH - 1);
    strncpy(dest_ip, argv[3], MAX_IP_LENGTH - 1);
    src_ip[MAX_IP_LENGTH - 1] = '\0';
    dest_ip[MAX_IP_LENGTH - 1] = '\0';

    int src_port = atoi(argv[2]);
    int dest_port = atoi(argv[4]);
    if (src_port <= 0 || dest_port <= 0)
    {
        printf("Error: Invalid port numbers\n");
        return 1;
    }

    pid_t process_id = getpid();
    int ktp_sockfd = k_socket(AF_INET, SOCK_DGRAM, 0, process_id);
    if (ktp_sockfd < 0)
    {
        printf("Error in socket creation: %d\n", ktp_sockfd);
        return 1;
    }

    struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(dest_port);
    if (inet_aton(dest_ip, &servaddr.sin_addr) == 0)
    {
        printf("Error: Invalid destination IP address\n");
        k_close(ktp_sockfd);
        return 1;
    }

    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port = htons(src_port);
    if (inet_aton(src_ip, &cliaddr.sin_addr) == 0)
    {
        printf("Error: Invalid source IP address\n");
        k_close(ktp_sockfd);
        return 1;
    }

    if (k_bind(ktp_sockfd, (struct sockaddr *)&cliaddr, (struct sockaddr *)&servaddr) < 0)
    {
        printf("Error in binding\n");
        k_close(ktp_sockfd);
        return 1;
    }

    char filename[MAX_FILENAME_LENGTH];
    printf("Enter filename: ");
    if (fgets(filename, MAX_FILENAME_LENGTH, stdin) == NULL)
    {
        printf("Error reading filename\n");
        k_close(ktp_sockfd);
        return 1;
    }
    filename[strcspn(filename, "\n")] = 0;

    FILE *file;
    while ((file = fopen(filename, "r")) == NULL)
    {
        printf("File not found. Enter filename: ");
        if (fgets(filename, MAX_FILENAME_LENGTH, stdin) == NULL)
        {
            printf("Error reading filename\n");
            k_close(ktp_sockfd);
            return 1;
        }
        filename[strcspn(filename, "\n")] = 0;
    }

    char file_header[MAX_FILENAME_LENGTH + 8] = "FILE:";
    strcat(file_header, filename);
    if (k_sendto(ktp_sockfd, file_header, strlen(file_header), 0, (struct sockaddr *)&servaddr) < 0)
    {
        printf("Error sending filename\n");
        fclose(file);
        k_close(ktp_sockfd);
        return 1;
    }

    char buffer[BUFFER_SIZE];
    while (fgets(buffer, BUFFER_SIZE, file) != NULL)
    {
        int len = strlen(buffer);
        if (buffer[len - 1] == '\n')
            buffer[len - 1] = '\0';
        if (k_sendto(ktp_sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&servaddr) < 0)
        {
            printf("Error sending file data\n");
            fclose(file);
            k_close(ktp_sockfd);
            return 1;
        }
    }
    fclose(file);

    if (k_sendto(ktp_sockfd, "d", strlen("d")+1, 0, (struct sockaddr *)&servaddr) < 0)
    {
        printf("Error sending end marker\n");
        k_close(ktp_sockfd);
        return 1;
    }

    printf("File transfer completed successfully\n");
    k_close(ktp_sockfd);
    return 0;
}
