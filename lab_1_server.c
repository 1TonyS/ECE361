#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <UDP listen port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int udp_port = atoi(argv[1]);
    int sockfd;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(udp_port);

    // Bind the socket to the port
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", udp_port);

    // Listen for incoming messages
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("Failed to receive message");
            continue;
        }
        buffer[n] = '\0'; // Null-terminate the received message
        printf("Received message: %s\n", buffer);

        // Process the message
        if (strcmp(buffer, "ftp") == 0) {
            const char *response = "yes";
            sendto(sockfd, response, strlen(response), 0, (const struct sockaddr *)&client_addr, addr_len);
            printf("Replied with: %s\n", response);
        } else {
            const char *response = "no";
            sendto(sockfd, response, strlen(response), 0, (const struct sockaddr *)&client_addr, addr_len);
            printf("Replied with: %s\n", response);
        }
    }

    close(sockfd);
    return 0;
}
