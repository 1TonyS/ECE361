#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_NAME 50
#define MAX_DATA 1024
#define MAX_CLIENTS 100
#define MAX_SESSIONS 50
#define BUF_SIZE 2048

typedef struct {
    unsigned int type;
    unsigned int size;
    unsigned char source[MAX_NAME];
    unsigned char data[MAX_DATA];
} Message;

typedef struct {
    char id[MAX_NAME];
    char password[MAX_NAME];
    int socket;
    char session[MAX_NAME];
    int active;
} Client;

typedef struct {
    char session_id[MAX_NAME];
    Client* participants[MAX_CLIENTS];
    int count;
} Session;

Client clients[MAX_CLIENTS];
Session sessions[MAX_SESSIONS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

const Client valid_clients[] = {
    {"a", "1", -1, "", 0},
    {"b", "2", -1, "", 0},
    {"c", "3", -1, "", 0},
    {"d", "4", -1, "", 0},
};
const int num_valid_clients = 4;

void serialize_message(Message *msg, char *buffer) {
    snprintf(buffer, BUF_SIZE, "%u:%u:%s:%s\n", 
            msg->type, msg->size, msg->source, msg->data);
}

ssize_t read_line(int sockfd, char *buffer, size_t maxlen) {
    size_t n = 0;
    char c;
    while (n < maxlen - 1) {
        ssize_t rc = recv(sockfd, &c, 1, 0);
        if (rc == 1) {
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
    sscanf(buffer, "%u:%u:%[^:]:%[^\n]", 
          &msg->type, &msg->size, msg->source, msg->data);
}

int find_client_index(char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int find_session_index(char *session_id) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (strlen(sessions[i].session_id) > 0 && 
            strcmp(sessions[i].session_id, session_id) == 0) {
            return i;
        }
    }
    return -1;
}

void broadcast_message(Message *msg, char *session_id) {
    pthread_mutex_lock(&sessions_mutex);
    int session_idx = find_session_index(session_id);
    if (session_idx == -1) {
        pthread_mutex_unlock(&sessions_mutex);
        return;
    }

    char buffer[BUF_SIZE];
    serialize_message(msg, buffer);
    
    Session *sess = &sessions[session_idx];
    for (int i = 0; i < sess->count; i++) {
        if (sess->participants[i]->socket != -1) {
            send(sess->participants[i]->socket, buffer, strlen(buffer), 0);
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
}

void remove_from_session(int client_index) {
    pthread_mutex_lock(&sessions_mutex);
    Client *client = &clients[client_index];
    if (strlen(client->session) == 0) {
        pthread_mutex_unlock(&sessions_mutex);
        return;
    }

    int session_idx = find_session_index(client->session);
    if (session_idx == -1) {
        pthread_mutex_unlock(&sessions_mutex);
        return;
    }

    Session *sess = &sessions[session_idx];
    for (int i = 0; i < sess->count; i++) {
        if (sess->participants[i] == client) {
            // Shift remaining participants
            for (int j = i; j < sess->count - 1; j++) {
                sess->participants[j] = sess->participants[j+1];
            }
            sess->count--;
            break;
        }
    }

    if (sess->count == 0) {
        memset(sess, 0, sizeof(Session));
    }
    memset(client->session, 0, MAX_NAME);
    pthread_mutex_unlock(&sessions_mutex);
}

void *client_handler(void *arg) {
    int client_socket = *((int *)arg);
    char buffer[BUF_SIZE];
    Message msg, response;

    while (1) {
        ssize_t bytes = read_line(client_socket, buffer, sizeof(buffer));
        if (bytes <= 0) break;

        deserialize_message(buffer, &msg);
        memset(&response, 0, sizeof(Message));

        switch (msg.type) {
            case 1: { // LOGIN
                int valid = 0;
                for (int i = 0; i < num_valid_clients; i++) {
                    if (strcmp(valid_clients[i].id, msg.source) == 0 &&
                        strcmp(valid_clients[i].password, msg.data) == 0) {
                        valid = 1;
                        break;
                    }
                }

                pthread_mutex_lock(&clients_mutex);
                if (valid && find_client_index(msg.source) == -1) {
                    int index = -1;
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (!clients[i].active) {
                            index = i;
                            clients[i] = valid_clients[0]; // Copy valid client
                            clients[i].socket = client_socket;
                            clients[i].active = 1;
                            strcpy(clients[i].id, msg.source);
                            break;
                        }
                    }
                    response.type = (index != -1) ? 2 : 3; // LO_ACK/LO_NAK
                    if (index == -1) strcpy(response.data, "Server full");
                } else {
                    response.type = 3; // LO_NAK
                    strcpy(response.data, "Invalid credentials");
                }
                pthread_mutex_unlock(&clients_mutex);
                break;
            }

            // In client_handler() switch-case:
            case 4: { // EXIT (logout)
                pthread_mutex_lock(&clients_mutex);
                int client_idx = find_client_index(msg.source);
                if (client_idx != -1) {
                    // Remove from session
                    remove_from_session(client_idx);
                    // Clear client data
                    clients[client_idx].active = 0;
                    clients[client_idx].socket = -1;
                    memset(clients[client_idx].session, 0, MAX_NAME);
                }
                pthread_mutex_unlock(&clients_mutex);

                // Acknowledge logout
                Message response = {0};
                response.type = 4; // EXIT_ACK
                char res_buffer[BUF_SIZE];
                serialize_message(&response, res_buffer);
                send(client_socket, res_buffer, strlen(res_buffer), 0);

                // Do NOT close the socket or exit the thread
                break;
            }

            case 5: { // JOIN
                pthread_mutex_lock(&clients_mutex);
                int client_idx = find_client_index(msg.source);
                pthread_mutex_unlock(&clients_mutex);

                pthread_mutex_lock(&sessions_mutex);
                int session_idx = find_session_index(msg.data);
                
                if (client_idx == -1) {
                    response.type = 7; // JN_NAK
                    strcpy(response.data, "Not logged in");
                }
                else if (session_idx == -1) {
                    response.type = 7; // JN_NAK
                    strcpy(response.data, "Session not found");
                }
                else if (strlen(clients[client_idx].session) > 0) {
                    response.type = 7; // JN_NAK
                    strcpy(response.data, "Already in session");
                }
                else {
                    Session *sess = &sessions[session_idx];
                    if (sess->count < MAX_CLIENTS) {
                        pthread_mutex_lock(&clients_mutex);
                        sess->participants[sess->count++] = &clients[client_idx];
                        strcpy(clients[client_idx].session, sess->session_id);
                        pthread_mutex_unlock(&clients_mutex);
                        
                        response.type = 6; // JN_ACK
                        strcpy(response.data, sess->session_id);
                    } else {
                        response.type = 7; // JN_NAK
                        strcpy(response.data, "Session full");
                    }
                }
                pthread_mutex_unlock(&sessions_mutex);
                break;
            }
            // In client_handler() switch-case:
            case 8: { // LEAVE_SESS
                pthread_mutex_lock(&clients_mutex);
                int client_idx = find_client_index(msg.source);
                if (client_idx == -1) {
                    // Not logged in
                    response.type = 3; // LO_NAK
                    strcpy(response.data, "Not logged in");
                } else if (strlen(clients[client_idx].session) == 0) {
                    // Not in a session
                    response.type = 7; // JN_NAK
                    strcpy(response.data, "Not in any session");
                } else {
                    // Remove from session
                    remove_from_session(client_idx);
                    response.type = 8; // LEAVE_SESS_ACK
                    strcpy(response.data, "Left session successfully");
                }
                pthread_mutex_unlock(&clients_mutex);
                break;
            }
            case 9: { // NEW_SESS
                pthread_mutex_lock(&clients_mutex);
                int client_idx = find_client_index(msg.source);
                if (client_idx != -1 && strlen(clients[client_idx].session) > 0) {
                    pthread_mutex_unlock(&clients_mutex);
                    response.type = 7; // JN_NAK
                    strcpy(response.data, "Already in a session");
                    break;
                }
                pthread_mutex_unlock(&clients_mutex);

                pthread_mutex_lock(&sessions_mutex);
                int session_idx = find_session_index(msg.data);
                if (session_idx != -1) {
                    // Session already exists, reject the request
                    response.type = 7; // JN_NAK
                    strcpy(response.data, "Session already exists");
                } else {
                    // Session doesn't exist, create it
                    int created = 0;
                    for (int i = 0; i < MAX_SESSIONS; i++) {
                        if (strlen(sessions[i].session_id) == 0) {
                            strcpy(sessions[i].session_id, msg.data);
                            sessions[i].count = 0;

                            pthread_mutex_lock(&clients_mutex);
                            if (client_idx != -1) {
                                sessions[i].participants[sessions[i].count++] = &clients[client_idx];
                                strcpy(clients[client_idx].session, sessions[i].session_id);
                            }
                            pthread_mutex_unlock(&clients_mutex);

                            response.type = 10; // NS_ACK
                            //strcpy(response.source, "CLIENT"); //testing of client field
                            strcpy(response.data, sessions[i].session_id);
                            created = 1;
                            break;
                        }
                    }
                    if (!created) {
                        response.type = 7; // JN_NAK
                        strcpy(response.data, "Max sessions reached");
                    }
                }
                pthread_mutex_unlock(&sessions_mutex);
                break;
            }

            case 11: // MESSAGE
                pthread_mutex_lock(&clients_mutex);
                int client_idx = find_client_index(msg.source);
                if (client_idx != -1 && strlen(clients[client_idx].session) > 0) {
                    broadcast_message(&msg, clients[client_idx].session);
                }
                pthread_mutex_unlock(&clients_mutex);
                break;

            

            case 12: { // QUERY
                pthread_mutex_lock(&clients_mutex);
                pthread_mutex_lock(&sessions_mutex);
                
                char list[BUF_SIZE] = {0};
                // Build list with \n
                strcat(list, "=== Online Users ===\n");
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].active) {
                        char user_entry[100];
                        snprintf(user_entry, sizeof(user_entry), 
                                "- %s (in %s)\n", 
                                clients[i].id, clients[i].session);
                        strcat(list, user_entry);
                    }
                }
                strcat(list, "\n=== Active Sessions ===\n");
                for (int i = 0; i < MAX_SESSIONS; i++) {
                    if (strlen(sessions[i].session_id) > 0) {
                        char session_entry[100];
                        snprintf(session_entry, sizeof(session_entry), 
                                "- %s (%d participants)\n", 
                                sessions[i].session_id, sessions[i].count);
                        strcat(list, session_entry);
                    }
                }

                // Replace newlines with ~
                for (char *p = list; *p; p++) {
                    if (*p == '\n') *p = '~';
                }

                response.type = 13;
                response.size = strlen(list);
                strcpy(response.source, "SERVER");
                strncpy(response.data, list, MAX_DATA);

                pthread_mutex_unlock(&sessions_mutex);
                pthread_mutex_unlock(&clients_mutex);
                break;
            }
            case 14: { // QUIT
                pthread_mutex_lock(&clients_mutex);
                int client_idx = find_client_index(msg.source);
                if (client_idx != -1) {
                    // Remove from session
                    remove_from_session(client_idx);
                    // Clear client data
                    clients[client_idx].active = 0;
                    clients[client_idx].socket = -1;
                    memset(clients[client_idx].session, 0, MAX_NAME);
                }
                pthread_mutex_unlock(&clients_mutex);

                // Acknowledge quit
                Message response = {0};
                response.type = 14; // QUIT_ACK
                char res_buffer[BUF_SIZE];
                serialize_message(&response, res_buffer);
                send(client_socket, res_buffer, strlen(res_buffer), 0);

                // Close the socket and exit the thread
                close(client_socket);
                pthread_exit(NULL);
                break;
            }

            default:
                response.type = 3;
                strcpy(response.data, "Unknown command");
        }

        if (response.type != 0) {
            char res_buffer[BUF_SIZE];
            serialize_message(&response, res_buffer);
            send(client_socket, res_buffer, strlen(res_buffer), 0);
        }
    }

    // Cleanup
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == client_socket) {
            remove_from_session(i);
            clients[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    close(client_socket);
    pthread_exit(NULL);
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    memset(clients, 0, sizeof(clients));
    memset(sessions, 0, sizeof(sessions));

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(atoi(argv[1]));

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %s\n", argv[1]);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, 
                                 (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, (void *)&new_socket) < 0) {
            perror("could not create thread");
            close(new_socket);
        }
        pthread_detach(thread_id);
    }

    return 0;
}