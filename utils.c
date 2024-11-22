// UTILS.C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "utils.h"

// Process and respond to client requests
void handle_request(int client_fd) {
    char *line = NULL;
    size_t size = 0;
    FILE *client_stream = fdopen(client_fd, "r");

    if (getline(&line, &size, client_stream) > 0) {
        char method[10], path[1024];
        sscanf(line, "%s %s", method, path);
        free(line);

        // Prevent directory traversal attacks
        if (strstr(path, "..")) {
            send_error(client_fd, "HTTP/1.0 403 Forbidden\r\n", "403 Forbidden");
            return;
        }

        // Route the request based on the method
        if (strncmp(path, "/cgi-like/", 10) == 0) {
            handle_cgi_request(client_fd, path + 10); // Handle CGI-like requests
        } else if (strcmp(method, "GET") == 0) {
            handle_get_request(client_fd, path + 1); // Serve static files
        } else if (strcmp(method, "HEAD") == 0) {
            handle_head_request(client_fd, path + 1); // Serve headers only
        } else {
            send_error(client_fd, "HTTP/1.0 501 Not Implemented\r\n", "501 Not Implemented");
        }
    }

    fclose(client_stream); // Close the stream
}

// Handle GET requests (serve a static file)
void handle_get_request(int client_fd, const char *filename) {
    struct stat file_stat;

    // Check if the file exists and is readable
    if (stat(filename, &file_stat) == -1) {
        send_error(client_fd, "HTTP/1.0 404 Not Found\r\n", "404 Not Found");
        return;
    }

    if (!(file_stat.st_mode & S_IROTH)) { // Check read permissions
        send_error(client_fd, "HTTP/1.0 403 Forbidden\r\n", "403 Forbidden");
        return;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        send_error(client_fd, "HTTP/1.0 500 Internal Server Error\r\n", "500 Internal Server Error");
        return;
    }

    // Send the HTTP response headers
    char header[1024];
    snprintf(header, sizeof(header), "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", file_stat.st_size);
    write(client_fd, header, strlen(header));

    // Send the file content
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        write(client_fd, buffer, bytes_read);
    }

    fclose(file); // Close the file
}

// Handle HEAD requests (send headers only)
void handle_head_request(int client_fd, const char *filename) {
    struct stat file_stat;

    // Check if the file exists and is readable
    if (stat(filename, &file_stat) == -1) {
        send_error(client_fd, "HTTP/1.0 404 Not Found\r\n", "404 Not Found");
        return;
    }

    if (!(file_stat.st_mode & S_IROTH)) { // Check read permissions
        send_error(client_fd, "HTTP/1.0 403 Forbidden\r\n", "403 Forbidden");
        return;
    }

    // Send the HTTP response headers (no body content)
    char header[1024];
    snprintf(header, sizeof(header), "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", file_stat.st_size);
    write(client_fd, header, strlen(header));
}

// Handle CGI-like requests (execute a program and return its output)
void handle_cgi_request(int client_fd, const char *path) {
    char *cmd = strtok(path, "?");
    char *args[10] = {NULL};
    char *arg = strtok(NULL, "&");

    int i = 0;
    while (arg && i < 9) {
        args[i++] = arg;
        arg = strtok(NULL, "&");
    }

    char temp_file[1024];
    snprintf(temp_file, sizeof(temp_file), "/tmp/cgi_output_%d", getpid());

    pid_t pid = fork();

    if (pid == 0) { // Child process
        int fd = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        dup2(fd, STDOUT_FILENO); // Redirect stdout to the temp file
        close(fd);
        execvp(cmd, args); // Execute the program
        exit(EXIT_FAILURE); // Exit if execvp fails
    } else if (pid > 0) { // Parent process
        wait(NULL); // Wait for the child process to finish

        FILE *file = fopen(temp_file, "r");
        if (!file) {
            send_error(client_fd, "HTTP/1.0 500 Internal Server Error\r\n", "500 Internal Server Error");
            return;
        }

        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        rewind(file);

        // Send the HTTP response headers
        char header[1024];
        snprintf(header, sizeof(header), "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n", size);
        write(client_fd, header, strlen(header));

        // Send the output of the CGI program
        char buffer[1024];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            write(client_fd, buffer, bytes_read);
        }

        fclose(file);
        unlink(temp_file); // Delete the temporary file
    }
}

// Send an HTTP error response
void send_error(int client_fd, const char *header, const char *message) {
    char response[1024];
    snprintf(response, sizeof(response), "%sContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", header, strlen(message), message);
    write(client_fd, response, strlen(response));
}
