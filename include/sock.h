#ifndef SOCK_H
#define SOCK_H

int create_tcp_socket(int port);
int accept_connection(int tcp_sock, int unix_sock);
void send_error(int client_sock, const char *message);
void send_response(int client_sock, const char *content_type, const char *content);

#endif