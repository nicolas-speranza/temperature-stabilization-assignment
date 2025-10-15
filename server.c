// server.c: central temperature process
// Usage: ./server <initial_central_temp> [port]
// Default port: 5000

#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_PORT 5000
#define NCLIENTS 4
#define EPS 1e-3

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
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <initial_central_temp> [port]\n", argv[0]);
        return 1;
    }
    double central = atof(argv[1]);
    int port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_PORT;

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listenfd, NCLIENTS) < 0) { perror("listen"); return 1; }

    printf("[SERVER] Listening on port %d, initial central=%.6f\n", port, central);

    int fds[NCLIENTS] = {-1,-1,-1,-1};
    int ids[NCLIENTS]  = {0,0,0,0};
    double last_ext[NCLIENTS], curr_ext[NCLIENTS];
    bool have_last = false;

    // Accept 4 clients
    for (int i = 0; i < NCLIENTS; i++) {
        struct sockaddr_in cli; socklen_t clilen = sizeof(cli);
        int cfd = accept(listenfd, (struct sockaddr*)&cli, &clilen);
        if (cfd < 0) { perror("accept"); return 1; }
        fds[i] = cfd;
        printf("[SERVER] Client %d connected (fd=%d)\n", i+1, cfd);

        char line[256];
        if (recv_line(cfd, line, sizeof(line)) <= 0) {
            fprintf(stderr, "[SERVER] Failed to read HELLO from client fd=%d\n", cfd);
            return 1;
        }
        int cid = 0; double temp = 0.0;
        if (sscanf(line, "HELLO %d %lf", &cid, &temp) != 2) {
            fprintf(stderr, "[SERVER] Bad HELLO: '%s'\n", line);
            return 1;
        }
        ids[i] = cid;
        curr_ext[i] = temp;
        printf("[SERVER] Received HELLO from ext #%d with temp=%.6f\n", cid, temp);
    }

    int iter = 0;
    for (;;) {
        iter++;

        for (int i = 0; i < NCLIENTS; i++) {
            char out[128];
            snprintf(out, sizeof(out), "CENTRAL %.12f", central);
            if (send_line(fds[i], out) < 0) { perror("send CENTRAL"); return 1; }
        }

        for (int i = 0; i < NCLIENTS; i++) {
            char line[256];
            if (recv_line(fds[i], line, sizeof(line)) <= 0) {
                fprintf(stderr, "[SERVER] Lost client fd=%d\n", fds[i]);
                return 1;
            }
            int cid = 0; double t = 0.0;
            if (sscanf(line, "TEMP %d %lf", &cid, &t) != 2) {
                fprintf(stderr, "[SERVER] Bad TEMP: '%s'\n", line);
                return 1;
            }
            bool placed = false;
            for (int k = 0; k < NCLIENTS; k++) {
                if (ids[k] == cid) {
                    if (have_last) last_ext[k] = curr_ext[k];
                    curr_ext[k] = t;
                    placed = true; break;
                }
            }
            if (!placed) {
                fprintf(stderr, "[SERVER] Unknown client id %d\n", cid);
                return 1;
            }
        }

        bool converged = have_last;
        if (have_last) {
            for (int i = 0; i < NCLIENTS; i++) {
                double delta = curr_ext[i] - last_ext[i];
                if (delta < 0) delta = -delta;
                if (delta > EPS) { converged = false; break; }
            }
        }

        double sum_ext = 0.0;
        for (int i = 0; i < NCLIENTS; i++) sum_ext += curr_ext[i];
        double new_central = (2.0*central + sum_ext) / 6.0;

        printf("[SERVER] iter=%d central=%.6f -> %.6f   ext=[", iter, central, new_central);
        for (int i = 0; i < NCLIENTS; i++) printf("%s%.6f", (i?", ":""), curr_ext[i]);
        printf("]\n");

        if (converged) {
            printf("[SERVER] *** STABILIZED after %d iterations ***\n", iter);
            for (int i = 0; i < NCLIENTS; i++) {
                char out[128];
                snprintf(out, sizeof(out), "DONE %.12f", new_central);
                send_line(fds[i], out);
            }
            printf("[SERVER] Final central=%.6f\n", new_central);
            for (int i = 0; i < NCLIENTS; i++) {
                printf("[SERVER] Final ext[%d]=%.6f\n", ids[i], curr_ext[i]);
            }
            break;
        }

        central = new_central;
        have_last = true;
    }

    for (int i = 0; i < NCLIENTS; i++) close(fds[i]);
    close(listenfd);
    return 0;
}
