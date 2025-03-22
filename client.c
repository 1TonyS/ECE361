#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_NAME 50
#define MAX_DATA 1024
#define MAX_LINE 1200

typedef struct {
    unsigned int type;
    unsigned int size;
    unsigned char source[MAX_NAME];
    unsigned char data[MAX_DATA];
} Message;



int sockfd = -1;
char client_id[MAX_NAME] = {0};
char current_session[MAX_NAME] = {0};
bool insession = false;
int logged_in = 0;
pthread_mutex_t sockfd_mutex = PTHREAD_MUTEX_INITIALIZER;

void serialize_message(Message *msg, char *buffer) {
    snprintf(buffer, MAX_LINE, "%u:%u:%s:%s\n", 
             msg->type, msg->size, msg->source, msg->data);
}

// Revised read_line to skip empty lines
ssize_t read_line(int sockfd, char *buffer, size_t maxlen) {
    size_t n = 0;
    char c;
    while (n < maxlen - 1) {
        ssize_t rc = recv(sockfd, &c, 1, 0);
        if (rc == 1) {
            if (c == '\n' && n == 0) continue; // Skip empty lines
            buffer[n++] = c;
            if (c == '\n') break;
        } else if (rc == 0) {
            break; // Connection closed
        } else {
            return -1; // Error
        }
    }
    buffer[n] = '\0';
    return n;
}

void deserialize_message(char *buffer, Message *msg) {
    // Reset fields to avoid leftovers
    memset(msg, 0, sizeof(Message));
    
    // Split the buffer into tokens
    char *token = strtok(buffer, ":");
    if (token) msg->type = atoi(token);
    
    token = strtok(NULL, ":");
    if (token) msg->size = atoi(token);
    
    token = strtok(NULL, ":");
    if (token) strncpy(msg->source, token, MAX_NAME);
    else strncpy(msg->source, "client", MAX_NAME);
    // Get remaining data (including spaces)
    token = strtok(NULL, "\n"); // Read until newline
    if (token) {
        strncpy(msg->data, token, MAX_DATA);
    } else {
        strncpy(msg->data, msg->source, MAX_DATA); // Ensure empty string if no data
    }

    // Debug: Print parsed message
    //printf("DEBUG: Parsed message - type=%u, size=%u, source='%s', data='%s'\n",
           //msg->type, msg->size, msg->source, msg->data);
}

void *receive_handler(void *arg) {
    char buffer[MAX_LINE];
    Message msg;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = read_line(sockfd, buffer, sizeof(buffer));
        if (n <= 0) {
            printf("\nConnection lost\n");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        buffer[strcspn(buffer, "\n")] = '\0';
        
        // Debug print raw message
        //printf("DEBUG RAW: %s\n", buffer);
        
        deserialize_message(buffer, &msg);

        switch (msg.type) {
            case 2:  // LO_ACK
                logged_in = 1;
                printf("Login successful\n");
                break;
            case 3:  // LO_NAK
                printf("Login failed: %s\n", msg.data);
                break;
            case 4:  // EXIT_ACK
                logged_in = 0;
                insession = false;
                memset(current_session, 0, MAX_NAME);
                memset(client_id, 0, MAX_NAME);
                printf("Logged out successfully. You can log in again.\n");
                break;
            case 6:  // JN_ACK (Join Acknowledgment)
                // Debug: Print raw msg.data
                //printf("DEBUG: JN_ACK data = '%s'\n", msg.data);
                
                // Copy the session ID into current_session
                strncpy(current_session, msg.data, MAX_NAME);
                current_session[MAX_NAME - 1] = '\0'; // Ensure null termination
                insession = true;
                printf("Joined session: %s\n", current_session);
                break;
            case 7:  // JN_NAK
                printf("Join failed: %s\n", msg.data);
                break;
            case 8:  // LEAVE_SESS_ACK
                insession = false;
                memset(current_session, 0, MAX_NAME);
                printf("Left session: %s\n", msg.data);
                break;
            case 10: // NS_ACK
                strncpy(current_session, msg.data, MAX_NAME);
                insession = true;
                printf("Created & joined session: %s\n", current_session);
                break;
            case 11: // MESSAGE
                printf("[%s] %s\n", msg.source, msg.data);
                break;
            case 13: // QU_ACK
                printf("=== Server Status ===\n");
                char *p = msg.data;
                while (*p) {
                    if (*p == '~') {
                        printf("\n");
                    } else {
                        putchar(*p);
                    }
                    p++;
                }
                printf("\n");
                break;
            default:
                printf("Received unknown message type: %u\n", msg.type);
        }
    }
    return NULL;
}

int send_message(Message *msg) {
    char buffer[MAX_LINE];
    serialize_message(msg, buffer);
    
    pthread_mutex_lock(&sockfd_mutex);
    int result = send(sockfd, buffer, strlen(buffer), 0);
    pthread_mutex_unlock(&sockfd_mutex);
    
    return result;
}

void trim_newline(char *str) {
    int len = strlen(str);
    if (len > 0 && str[len-1] == '\n') {
        str[len-1] = '\0';
    }
}

int main() {
    pthread_t recv_thread;
    char input[MAX_DATA];

    printf("Client started. Type /login to begin.\n");

    while (1) {
        printf("> ");
        fflush(stdout);
        fgets(input, MAX_DATA, stdin);
        trim_newline(input);
        
        if (strlen(input) == 0) continue;

        char inputCopy [MAX_DATA] ;
        strncpy(inputCopy, input, MAX_DATA - 1);
        inputCopy[MAX_DATA-1] = '\0';

        char *command = strtok(input, " ");
        
        if (strcmp(command, "/login") == 0) {
            char *id = strtok(NULL, " ");
            char *pass = strtok(NULL, " ");
            char *ip = strtok(NULL, " ");
            char *port = strtok(NULL, " ");

            if (!id || !pass || !ip || !port) {
                printf("Usage: /login <ID> <password> <IP> <port>\n");
                continue;
            }

            if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                perror("Socket creation failed");
                continue;
            }

            struct sockaddr_in serv_addr = {
                .sin_family = AF_INET,
                .sin_port = htons(atoi(port))
            };
            
            if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
                perror("Invalid address");
                close(sockfd);
                sockfd = -1;
                continue;
            }

            if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                perror("Connection failed");
                close(sockfd);
                sockfd = -1;
                continue;
            }

            pthread_create(&recv_thread, NULL, receive_handler, NULL);

            Message login_msg = {0};
            login_msg.type = 1;
            strncpy(login_msg.source, id, MAX_NAME);
            strncpy(login_msg.data, pass, MAX_DATA);
            login_msg.size = strlen(pass);
            send_message(&login_msg);

            strncpy(client_id, id, MAX_NAME);

        } else if (strcmp(command, "/logout") == 0) {
            if (!logged_in) {
                printf("You are not logged in\n");
                continue;
            }

            // Send EXIT message to the server
            Message msg = {0};
            msg.type = 4; // EXIT (logout)
            strncpy(msg.source, client_id, MAX_NAME);
            send_message(&msg);

            // Reset client state
            logged_in = 0;
            insession = false;
            memset(current_session, 0, MAX_NAME);
            memset(client_id, 0, MAX_NAME);
            printf("Logged out successfully. You can log in again.\n");

        } else if (strcmp(command, "/createsession") == 0) {
            if (!logged_in) {
                printf("You must be logged in to create a session\n");
                continue;
            }

            char *session_id = strtok(NULL, " ");
            if (!session_id) {
                printf("Usage: /createsession <session_id>\n");
                continue;
            }

            Message msg = {0};
            msg.type = 9; // NEW_SESS
            msg.size = strlen(session_id);
            strncpy(msg.source, client_id, MAX_NAME);
            strncpy(msg.data, session_id, MAX_DATA);
            send_message(&msg);

        } else if (strcmp(command, "/joinsession") == 0) {
            char *session_id = strtok(NULL, " ");
            if (!session_id) {
                printf("Usage: /joinsession <session_id>\n");
                continue;
            }

            Message msg = {0};
            msg.type = 5;
            msg.size = strlen(session_id);
            strncpy(msg.source, client_id, MAX_NAME);
            strncpy(msg.data, session_id, MAX_DATA);
            send_message(&msg);

        } else if (strcmp(command, "/leavesession") == 0) {
            if (!logged_in) {
                printf("You must be logged in to leave a session\n");
                continue;
            }
            if (!insession) {
                printf("You are not in any session\n");
                continue;
            }

            Message msg = {0};
            msg.type = 8; // LEAVE_SESS
            strncpy(msg.source, client_id, MAX_NAME);
            send_message(&msg);

            // Update client state
            insession = false;
            memset(current_session, 0, MAX_NAME);
            printf("Left session successfully\n");

        } else if (strcmp(command, "/list") == 0) {
            Message msg = {0};
            msg.type = 12;
            strncpy(msg.source, client_id, MAX_NAME);
            send_message(&msg);

        } else if (strcmp(command, "/quit") == 0) {
            if (logged_in) {
                Message msg = {0};
                msg.type = 14; // QUIT (new message type)
                strncpy(msg.source, client_id, MAX_NAME);
                send_message(&msg);
            }
            printf("Exiting...\n");
            close(sockfd);
            exit(EXIT_SUCCESS);

        } else {
            if (!logged_in) {
                printf("You must be logged in to send messages\n");
                continue;
            }

            if (!insession) {
                printf("You must join a session first\n");
                continue;
            }

            Message msg = {0};
            msg.type = 11;
            msg.size = strlen(inputCopy);
            strncpy(msg.source, client_id, MAX_NAME);
            strncpy(msg.data, inputCopy, MAX_DATA);
            send_message(&msg);
        }
    }

    return 0;
}