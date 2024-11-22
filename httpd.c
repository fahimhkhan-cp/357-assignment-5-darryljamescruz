// HTTPD.C
// Main server logic, socket setup, connection handling, and request routing.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include "utils.h"

#define BACKLOG 10 // Maximum pending connections

void reap_child(int signo){
    while (waitpid(-1, NULL, WNOHANG) > 0) {}   // Clean up zombie processes
}

int main(int argc, char *argv[]) {
    printf("Please ")
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Grab port from input
    int port = atoi(argv[1]);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);    // Create socket for comms    
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {0};       // Server address structure
    server_addr.sin_family = AF_INET;           // IPv4
    server_addr.sin_port = htons(port);         // Convert port number to network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY;   // Bind to any available netork interfae

    // Bind the socket to the port specifed
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Set up a signal handler to reap any zombie processes created by fork()
    signal(SIGCHLD, reap_child);

    printf("Server running on port %d\n", port);    // Print that server is running

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);  // Accept Client Connection
        if (client_fd == -1) {
            perror("accept");   // Print error if accept fails
            continue;           // Skip to next i
        }

        pid_t pid = fork(); // Create new process to handle the client.
        if (pid == 0) {     // Child Process
            close(server_fd);           // Close the server socket
            handle_request(client_fd);  // Call helper function to process client requests
            close(client_fd);           // Close the client socket
            exit(EXIT_SUCCESS);         // Terminate the child process
        } else if (pid > 0) {   // Parent Process
            close(client_fd);           // Close the client socket
        } else {
            perror("fork"); // Print error if fork fails
        }
    }

    close(server_fd);   // Close the server socket (should not be reached)
    return 0;           // Exit the program (should also not be reached)
}