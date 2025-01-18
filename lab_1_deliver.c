#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server address> <server port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Prompt the user for a message
    printf("Enter message (e.g., ftp <file name>): ");
    fgets(buffer, BUFFER_SIZE, stdin);
    buffer[strcspn(buffer, "\n")] = '\0'; // Remove trailing newline

    // Check if the file exists
    char *command = strtok(buffer, " ");
    char *filename = strtok(NULL, " ");
    if (command == NULL || filename == NULL || strcmp(command, "ftp") != 0) {
        printf("Invalid command format.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (access(filename, F_OK) == -1) {
        perror("File does not exist");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Send "ftp" to the server
    const char *message = "ftp";
    sendto(sockfd, message, strlen(message), 0, (const struct sockaddr *)&server_addr, addr_len);

    // Receive server response
    int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&server_addr, &addr_len);
    if (n < 0) {
        perror("Failed to receive response");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    buffer[n] = '\0'; // Null-terminate the received message
    printf("Server response: %s\n", buffer);

    if (strcmp(buffer, "yes") == 0) {
        printf("A file transfer can start.\n");
    }

    close(sockfd);
    return 0;
}
