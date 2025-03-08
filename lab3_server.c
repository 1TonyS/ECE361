#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include "packet.h"

#define BUFFER_SIZE 1300       // Must be large enough for header plus file data.
#define DROP_THRESHOLD 0.95    // Simulate dropping 70% of packets.
#define MAX_FILEDATA_SIZE 1000 // Assumed maximum file data per packet from packet.h

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
    FILE *file = NULL;
    char current_filename[150] = ""; // Buffer for storing the output file name.

    if (udp_port <= 0) {
        fprintf(stderr, "Invalid port number: %d\n", udp_port);
        exit(EXIT_FAILURE);
    }

    // Seed the random number generator for packet drop simulation.
    srand(time(NULL));

    // Create UDP socket.
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address.
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(udp_port);

    // Bind the socket.
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", udp_port);

    // Initial handshake: wait for client to send "ftp" message.
    int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&client_addr, &addr_len);
    if (n < 0) {
        perror("Failed to receive initial message");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    buffer[n] = '\0';
    printf("Received initial message: %s\n", buffer);
    // Reply with "yes" to allow file transfer.
    sendto(sockfd, "yes", 3, 0, (struct sockaddr *)&client_addr, addr_len);

    unsigned int expected_frag = 1;
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (n <= 0) {
            perror("Failed to receive packet");
            continue;
        }
        // Parse the header.
        struct packet pkt;
        char temp_filename[150];  // Temporary storage for the filename.
        int parsed = sscanf(buffer, "%u:%u:%u:%99[^:]:", 
                            &pkt.total_frag, &pkt.frag_no, &pkt.size, temp_filename);
        if (parsed < 4) {
            fprintf(stderr, "Malformed packet received. Skipping...\n");
            continue;
        }
        // Allocate memory for the filename.
        pkt.filename = malloc(strlen(temp_filename) + 1);
        if (!pkt.filename) {
            perror("Memory allocation error");
            continue;
        }
        strcpy(pkt.filename, temp_filename);

        // Compute header length using snprintf.
        int header_len = snprintf(NULL, 0, "%u:%u:%u:%s:", 
                                  pkt.total_frag, pkt.frag_no, pkt.size, pkt.filename);
        if (header_len < 0 || header_len >= BUFFER_SIZE) {
            fprintf(stderr, "Header length error. Skipping packet.\n");
            free(pkt.filename);
            continue;
        }
        if (pkt.size > MAX_FILEDATA_SIZE) {
            fprintf(stderr, "Packet size too large. Skipping packet.\n");
            free(pkt.filename);
            continue;
        }
        // Copy the file data from the correct offset.
        memcpy(pkt.filedata, buffer + header_len, pkt.size);

        // Simulate packet drop: generate a random number in [0,1)
        double r = (double)rand() / RAND_MAX;
        if (r < DROP_THRESHOLD) {
            printf("Simulated drop for fragment %u\n", pkt.frag_no);
            free(pkt.filename);
            continue; // Skip processing this packet; no ACK is sent.
        }

        //printf("\tTesting\n");
        // If this is the first fragment, open the file.
        if (pkt.frag_no == 1) {
            snprintf(current_filename, sizeof(current_filename), "received_%s", pkt.filename);
            if (file) fclose(file);
            file = fopen(current_filename, "wb");
            if (!file) {
                perror("Failed to open file for writing");
                free(pkt.filename);
                exit(EXIT_FAILURE);
            }
            printf("Receiving file: %s (Total Fragments: %u)\n", current_filename, pkt.total_frag);
            expected_frag = 1;
        }

        // Check for expected fragment.
        if (pkt.frag_no != expected_frag) {
            fprintf(stderr, "Unexpected fragment %u (expected %u). Skipping...\n", pkt.frag_no, expected_frag);
            free(pkt.filename);
            continue;
        }

        // Write file data.
        fwrite(pkt.filedata, 1, pkt.size, file);
        printf("Received and wrote fragment %u of %u\n", pkt.frag_no, pkt.total_frag);

        // Send ACK.
        char ack[20];
        snprintf(ack, sizeof(ack), "ACK %u", pkt.frag_no);
        sendto(sockfd, ack, strlen(ack), 0, (struct sockaddr *)&client_addr, addr_len);
        printf("Sent ACK for fragment %u\n", pkt.frag_no);

        expected_frag++;
        free(pkt.filename); // Free the allocated memory for the filename.

        // If this is the last fragment, close the file.
        if (pkt.frag_no == pkt.total_frag) {
            printf("File transfer complete. File saved as: %s\n", current_filename);
            fclose(file);
            file = NULL;
            break;
        }
    }

    close(sockfd);
    return 0;
}
