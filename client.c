// client.c: external temperature process
// Usage: ./client <id 1..4> <initial_temp> [host] [port]
// Defaults: host=127.0.0.1, port=5000

#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 5000

static ssize_t recv_line(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) return 0;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') { buf[n] = '\0'; return (ssize_t)n; }
        buf[n++] = c;
    }
    buf[n] = '\0';
    return (ssize_t)n;
}

static int send_line(int fd, const char *s) {
    size_t len = strlen(s);
    if (send(fd, s, len, 0) < 0) return -1;
    if (send(fd, "\n", 1, 0) < 0) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <id 1..4> <initial_temp> [host] [port]\n", argv[0]);
        return 1;
    }
    int id = atoi(argv[1]);
    if (id < 1 || id > 4) { fprintf(stderr, "id must be 1..4\n"); return 1; }
    double ext = atof(argv[2]);
    const char *host = (argc >= 4) ? argv[3] : DEFAULT_HOST;
    int port = (argc >= 5) ? atoi(argv[4]) : DEFAULT_PORT;

    char portstr[16]; snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) { perror("getaddrinfo"); return 1; }
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { perror("socket"); return 1; }
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) { perror("connect"); return 1; }
    freeaddrinfo(res);

    printf("[CLIENT %d] connected to %s:%d with initial ext=%.6f\n", id, host, port, ext);

    char line[256];
    snprintf(line, sizeof(line), "HELLO %d %.12f", id, ext);
    if (send_line(sock, line) < 0) { perror("send HELLO"); return 1; }

    for (int iter = 1;; iter++) {
        if (recv_line(sock, line, sizeof(line)) <= 0) {
            fprintf(stderr, "[CLIENT %d] server closed\n", id);
            break;
        }

        double x = 0.0;
        if (sscanf(line, "DONE %lf", &x) == 1) {
            printf("[CLIENT %d] DONE after %d iters. central=%.6f  ext=%.6f\n", id, iter, x, ext);
            break;
        } else if (sscanf(line, "CENTRAL %lf", &x) == 1) {
            double central = x;
            double new_ext = (3.0*ext + 2.0*central) / 5.0;
            printf("[CLIENT %d] iter=%d central=%.6f  ext: %.6f -> %.6f\n",
                   id, iter, central, ext, new_ext);
            ext = new_ext;

            snprintf(line, sizeof(line), "TEMP %d %.12f", id, ext);
            if (send_line(sock, line) < 0) { perror("send TEMP"); return 1; }
        } else {
            fprintf(stderr, "[CLIENT %d] bad line from server: '%s'\n", id, line);
            break;
        }
    }

    close(sock);
    return 0;
}
