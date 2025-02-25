#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "ktpheader.h"

#define END_MARKER "#"
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

    // Initialize KTP protocol
    initialize();

    sleep(10);

    // Validate and copy IP addresses
    char src_ip[MAX_IP_LENGTH];
    char dest_ip[MAX_IP_LENGTH];

    if (strlen(argv[1]) >= MAX_IP_LENGTH || strlen(argv[3]) >= MAX_IP_LENGTH)
    {
        printf("Error: IP address too long\n");
        return 1;
    }

    strncpy(src_ip, argv[1], MAX_IP_LENGTH - 1);
    strncpy(dest_ip, argv[3], MAX_IP_LENGTH - 1);
    src_ip[MAX_IP_LENGTH - 1] = '\0';
    dest_ip[MAX_IP_LENGTH - 1] = '\0';

    // Convert ports
    int src_port = atoi(argv[2]);
    int dest_port = atoi(argv[4]);

    if (src_port <= 0 || dest_port <= 0)
    {
        printf("Error: Invalid port numbers\n");
        return 1;
    }

    // Create socket
    pid_t process_id = getpid();
    int ktp_sockfd = k_socket(AF_INET, SOCK_DGRAM, 0, process_id);

    if (ktp_sockfd < 0)
    {
        printf("Error in socket creation: %d\n", ktp_sockfd);
        return 1;
    }

    // Set up addresses
    struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Setup server address (destination)
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(dest_port);
    if (inet_aton(dest_ip, &servaddr.sin_addr) == 0)
    {
        printf("Error: Invalid destination IP address\n");
        k_close(ktp_sockfd, NULL);
        return 1;
    }

    // Setup client address (source)
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port = htons(src_port);
    if (inet_aton(src_ip, &cliaddr.sin_addr) == 0)
    {
        printf("Error: Invalid source IP address\n");
        k_close(ktp_sockfd, NULL);
        return 1;
    }

    // Bind socket
    if (k_bind(ktp_sockfd, (struct sockaddr *)&cliaddr, (struct sockaddr *)&servaddr) < 0)
    {
        printf("Error in binding\n");
        k_close(ktp_sockfd, NULL);
        return 1;
    }

    printf("Waiting for file transfer...\n");

    // Receive file
    char buffer[BUFFER_SIZE];
    int fd = -1;
    int total_bytes = 0;
    int received;

    // Wait for filename
    received = k_recvfrom(ktp_sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&servaddr);
    if (received < 0)
    {
        printf("Error receiving filename\n");
        k_close(ktp_sockfd, NULL);
        return 1;
    }
    buffer[received] = '\0';

    char filename[MAX_FILENAME_LENGTH];
    if (strncmp(buffer, "FILE:", 5) == 0)
    {
        // Extract filename and create output file
        strncpy(filename, buffer + 5, MAX_FILENAME_LENGTH - 1);
        filename[MAX_FILENAME_LENGTH - 1] = '\0';

        // Add prefix to output filename
        char output_filename[MAX_FILENAME_LENGTH + 32];
        snprintf(output_filename, sizeof(output_filename), "received_%s", filename);

        fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0)
        {
            printf("Error creating output file\n");
            k_close(ktp_sockfd, NULL);
            return 1;
        }
        printf("Receiving file: %s\n", filename);
    }
    else
    {
        printf("Invalid file transfer protocol\n");
        k_close(ktp_sockfd, NULL);
        return 1;
    }

    // Receive file contents
    while (1)
    {
        received = k_recvfrom(ktp_sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&servaddr);
        if (received < 0)
        {
            printf("\nError receiving file data\n");
            close(fd);
            k_close(ktp_sockfd, NULL);
            return 1;
        }
        buffer[received] = '\0';

        // Check for end marker
        if (received == 1 && strcmp(buffer, END_MARKER) == 0)
        {
            break;
        }

        // Write to file
        if (write(fd, buffer, received) != received)
        {
            printf("\nError writing to file\n");
            close(fd);
            k_close(ktp_sockfd, NULL);
            return 1;
        }

        total_bytes += received;
        printf("Received %d bytes\r", total_bytes);
        fflush(stdout);
    }

    printf("\nFile transfer completed successfully\n");
    printf("Total bytes received: %d\n", total_bytes);

    close(fd);
    k_close(ktp_sockfd, NULL);
    return 0;
}