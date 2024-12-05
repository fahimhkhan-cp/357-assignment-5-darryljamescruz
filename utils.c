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
    FILE *client_stream = fdopen(client_fd, "r");   // Open the client file descriptor as a stream

    // Read the first line of the HTTP request
    if (getline(&line, &size, client_stream) > 0) {
        char method[10], path[1024];
        sscanf(line, "%s %s", method, path);

        //printf("DEBUG: Incoming request - Method: %s, Path: %s\n", method, path);

        // Map "/" to "/index.html"
        if (strcmp(path, "/") == 0) {
            strcpy(path, "/index.html");
        }

        if (strcmp(method, "GET") == 0) {
            handle_get_request(client_fd, path);
        } else if (strcmp(method, "HEAD") == 0) {
            handle_head_request(client_fd, path);
        } else {
            send_error(client_fd, "HTTP/1.0 501 Not Implemented\r\n", "Method Not Implemented");
        }
    } else {
        //printf("DEBUG: Malformed or empty request\n");
        send_error(client_fd, "HTTP/1.0 400 Bad Request\r\n", "Malformed Request");
    }

    free(line);
    fclose(client_stream);
}

void handle_head_request(int client_fd, const char *filename) {
    char file_path[1024];
    struct stat file_stat;

    // Check root directory first
    snprintf(file_path, sizeof(file_path), "./%s", filename);
    if (stat(file_path, &file_stat) == -1) {
        // Fallback to static directory
        snprintf(file_path, sizeof(file_path), "./static/%s", filename);
        if (stat(file_path, &file_stat) == -1) {
            //printf("DEBUG: File not found - %s\n", file_path);
            send_error(client_fd, "HTTP/1.0 404 Not Found\r\n", "404 Not Found");
            printf("HI");
            return;
        }
    }

    // Block directory traversal attempts
    if (strstr(filename, "..")) {
        //printf("DEBUG: Directory traversal attempt - %s\n", filename);
        send_error(client_fd, "HTTP/1.0 403 Forbidden\r\n", "403 Forbidden");
        return;
    }

    // Permission check
    if (!(file_stat.st_mode & S_IROTH)) {
        //printf("DEBUG: File not readable - %s\n", file_path);
        send_error(client_fd, "HTTP/1.0 403 Forbidden\r\n", "403 Forbidden");
        return;
    }

    // Send only the headers
    char header[1024];
    snprintf(header, sizeof(header), "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", file_stat.st_size);
    write(client_fd, header, strlen(header));
    //printf("DEBUG: HEAD request served - %s\n", file_path);
}

// Handle GET requests: send both headers and file content
void handle_get_request(int client_fd, const char *filename) {
    char file_path[1024];
    struct stat file_stat;

    //printf("DEBUG: Incoming GET request for filename - %s\n", filename);

    // Check root directory first
    snprintf(file_path, sizeof(file_path), "./%s", filename);
    //printf("DEBUG: Trying root directory path - %s\n", file_path);

    if (stat(file_path, &file_stat) == -1) {
        // Fallback to static directory
        snprintf(file_path, sizeof(file_path), "./static/%s", filename);
        printf("DEBUG: Trying static directory path - %s\n", file_path);

        if (stat(file_path, &file_stat) == -1) {
            printf("DEBUG: File not found in both root and static directories\n");
            send_error(client_fd, "HTTP/1.0 404 Not Found\r\n", "404 Not Found");
            return;
        }
    }

    //printf("DEBUG: File found - %s\n", file_path);

    // Open and serve file
    FILE *file = fopen(file_path, "r");
    if (!file) {
        //printf("DEBUG: Failed to open file - %s\n", file_path);
        send_error(client_fd, "HTTP/1.0 500 Internal Server Error\r\n", "Internal Server Error");
        return;
    }

    // Send HTTP headers
    char header[1024];
    snprintf(header, sizeof(header), "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", file_stat.st_size);
    write(client_fd, header, strlen(header));

    char buffer[1024];
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        write(client_fd, buffer, bytes_read);
    }

    fclose(file);
   // printf("DEBUG: GET request served - %s\n", file_path);
}

void handle_cgi_request(int client_fd, const char *path) {
    //printf("DEBUG: Handling CGI request for path - %s\n", path);

    char *cmd = strtok((char *)path, "?");
    char script_path[1024];
    snprintf(script_path, sizeof(script_path), "./cgi-like/%s", cmd);

    // Prevent directory traversal
    if (strstr(cmd, "..")) {
        //printf("DEBUG: Directory traversal attempt detected - %s\n", cmd);
        send_error(client_fd, "HTTP/1.0 403 Forbidden\r\n", "Forbidden");
        return;
    }

    // Validate script
    struct stat file_stat;
    if (stat(script_path, &file_stat) == -1 || !(file_stat.st_mode & S_IXUSR)) {
        //printf("DEBUG: Script not found or not executable - %s\n", script_path);
        send_error(client_fd, "HTTP/1.0 404 Not Found\r\n", "Script not found or not executable");
        return;
    }

    // Parse arguments
    char *args[10] = {NULL};
    args[0] = script_path;
    char *arg = strtok(NULL, "&");
    int i = 1;
    while (arg && i < 10) {
        args[i++] = arg;
        arg = strtok(NULL, "&");
    }
    args[i] = NULL;

    // Temporary file for output
    char temp_file[1024];
    snprintf(temp_file, sizeof(temp_file), "/tmp/cgi_output_%d", getpid());

    pid_t pid = fork();
    if (pid == 0) { // Child process
        int fd = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        dup2(fd, STDOUT_FILENO); // Redirect stdout to the temp file
        close(fd);

        execvp(script_path, args); // Execute the script
        //printf("DEBUG: Exec failed for command - %s\n", script_path);
        exit(EXIT_FAILURE);
    } else if (pid > 0) { // Parent process
        waitpid(pid, NULL, 0); // Wait for child process to finish
        //printf("DEBUG: CGI program executed - %s\n", script_path);

        FILE *file = fopen(temp_file, "r");
        if (!file) {
            send_error(client_fd, "HTTP/1.0 500 Internal Server Error\r\n", "Internal Server Error");
            return;
        }

        // Determine file size
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        rewind(file);

        // Send HTTP headers
        char header[1024];
        snprintf(header, sizeof(header), "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n", size);
        write(client_fd, header, strlen(header));

        // Send file content
        char buffer[1024];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            write(client_fd, buffer, bytes_read);
        }

        fclose(file);
        unlink(temp_file); // Remove temporary file
        printf("DEBUG: Temporary file cleaned up - %s\n", temp_file);
    }
}

// Send an HTTP error response
void send_error(int client_fd, const char *header, const char *message) {
    char response[1024];
    snprintf(response, sizeof(response), "%sContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", header, strlen(message), message);
    write(client_fd, response, strlen(response));
}
