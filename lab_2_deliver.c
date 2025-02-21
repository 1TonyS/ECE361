#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include "packet.h"

#define BUFFER_SIZE 1300       // Enough space for header + file data
//#define TIMEOUT 2              // Timeout in seconds for ACKs
#define FILENAME_SIZE 100      // Maximum filename length for input

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server address> <server port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUFFER_SIZE];
    char filename_new[FILENAME_SIZE];

    if (server_port <= 0) {
        fprintf(stderr, "Invalid port number: %d\n", server_port);
        exit(EXIT_FAILURE);
    }

    // Create UDP socket.
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address.
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Prompt for file transfer command.
    printf("Enter message (e.g., ftp <file name>): ");
    fgets(buffer, BUFFER_SIZE, stdin);
    buffer[strcspn(buffer, "\n")] = '\0';

    char *command = strtok(buffer, " ");
    char *filename = strtok(NULL, " ");
    if (command == NULL || filename == NULL || strcmp(command, "ftp") != 0) {
        printf("Invalid command format.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    strncpy(filename_new, filename, FILENAME_SIZE - 1);
    filename_new[FILENAME_SIZE - 1] = '\0';

    // Check if file exists.
    if (access(filename_new, F_OK) == -1) {
        perror("File does not exist");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Initial handshake: send "ftp" and wait for response.
    const char *init_message = "ftp";
    struct timeval start, end;
    gettimeofday(&start, NULL);
    sendto(sockfd, init_message, strlen(init_message), 0, (struct sockaddr *)&server_addr, addr_len);

    int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&server_addr, &addr_len);
    if (n < 0) {
        perror("Failed to receive response");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    buffer[n] = '\0';
    gettimeofday(&end, NULL);

    //rtt calculation, subtract time before handshake from time after handshake
    double rtt = (end.tv_sec - start.tv_sec) * 1000.0;
    rtt += (end.tv_usec - start.tv_usec) / 1000.0;
    printf("Server response: %s\n", buffer);
    printf("Round Trip Time (RTT): %.3f ms\n", rtt);

    // Open the file in binary mode.
    FILE *file = fopen(filename_new, "rb");
    if (!file) {
        perror("Failed to open file");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    struct stat st;
    if (stat(filename_new, &st) < 0) {
        perror("Failed to get file stats");
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    unsigned int total_frag = (st.st_size + MAX_FILEDATA_SIZE - 1) / MAX_FILEDATA_SIZE;

    // Send file fragments.
    for (unsigned int frag_no = 1; frag_no <= total_frag; frag_no++) {
        struct packet pkt;
        pkt.total_frag = total_frag;
        pkt.frag_no = frag_no;
        pkt.size = fread(pkt.filedata, 1, MAX_FILEDATA_SIZE, file);

        // Dynamically allocate memory for the filename and copy the filename.
        pkt.filename = malloc(strlen(filename_new) + 1);
        if (!pkt.filename) {
            perror("Memory allocation error");
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        strcpy(pkt.filename, filename_new);

        // Serialize header into buffer.
        int header_len = snprintf(buffer, BUFFER_SIZE, "%u:%u:%u:%s:", 
                                      pkt.total_frag, pkt.frag_no, pkt.size, pkt.filename);
        if (header_len < 0 || header_len >= BUFFER_SIZE) {
            fprintf(stderr, "Error creating header\n");
            free(pkt.filename);
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        // Append the binary file data immediately after the header.
        memcpy(buffer + header_len, pkt.filedata, pkt.size);
        int total_packet_len = header_len + pkt.size;

        // Debug: print header portion.
        // printf("Sending packet header: ");
        // for (int i = 0; i < header_len; i++) {
        //     printf("%c", buffer[i]);
        // }
        // printf("\n");

        // Send the packet and wait for ACK.
        while (1) {
            sendto(sockfd, buffer, total_packet_len, 0, (struct sockaddr *)&server_addr, addr_len);

            n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, NULL, NULL);
            if (n > 0 && strstr(buffer, "ACK") != NULL) {
                printf("Received ACK for fragment %u\n", pkt.frag_no);
                break;
            } else {
                printf("Timeout or no ACK for fragment %u. Retrying...\n", pkt.frag_no);
            }
        }
        free(pkt.filename); // Free the dynamically allocated filename.
    }

    printf("File transfer completed successfully.\n");
    fclose(file);
    close(sockfd);
    return 0;
}
