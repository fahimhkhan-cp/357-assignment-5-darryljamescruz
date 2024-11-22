//  UTILS.H

#ifndef UTILS_H
#define UTILS_H

void handle_request(int client_fd);
void send_response(int client_fd, const char *header, const char *content, size_t content_length);
void send_error(int client_fd, const char *header, const char *message);
void handle_cgi_request(int client_fd, const char *path);
void handle_get_request(int client_fd, const char *filename);
void handle_head_request(int client_fd, const char *filename);

#endif