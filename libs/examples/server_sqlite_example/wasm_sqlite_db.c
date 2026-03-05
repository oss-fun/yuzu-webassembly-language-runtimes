#include <sqlite3.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "wasi_socket_ext.h"

#define PORT 8081
#define BACKLOG 128
#define BUF_SIZE 2048

static sqlite3 *db;

/* ================= DB ================= */

static void init_db(int n_entries) {
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        fprintf(stderr, "[CloudDB] sqlite open failed: %s\n",
                sqlite3_errmsg(db));
        exit(1);
    }

    sqlite3_exec(db,
        "CREATE TABLE Sample (key INTEGER PRIMARY KEY, value INTEGER);",
        NULL, NULL, NULL);

    srand((unsigned int)time(NULL));
    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    for (int i = 1; i <= n_entries; i++) {
        char sql[128];
        snprintf(sql, sizeof(sql),
                 "INSERT INTO Sample VALUES (%d, %d);",
                 i, rand() % 1000);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    printf("[CloudDB] initialized (%d entries)\n", n_entries);
}

static int select_value(int key, int *out) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        db,
        "SELECT value FROM Sample WHERE key = ?;",
        -1, &stmt, NULL);

    sqlite3_bind_int(stmt, 1, key);

    int ret = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out = sqlite3_column_int(stmt, 0);
        ret = 1;
    }

    sqlite3_finalize(stmt);
    return ret;
}

/* ================= HTTP ================= */

static int parse_key(const char *req, int *out_key) {
    const char *p = strstr(req, "GET /get?key=");
    if (!p)
        return -1;
    *out_key = atoi(p + strlen("GET /get?key="));
    return 0;
}

static void respond_200(int fd, int value) {
    dprintf(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "%d\n", value);
}

static void respond_404(int fd) {
    dprintf(fd,
        "HTTP/1.1 404 Not Found\r\n"
        "\r\n"
        "not found\n");
}

static void respond_400(int fd) {
    dprintf(fd,
        "HTTP/1.1 400 Bad Request\r\n"
        "\r\n"
        "bad request\n");
}

/* ================= handler ================= */

static void handle_client(int fd) {
    char buf[BUF_SIZE];
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0)
        return;
    buf[n] = '\0';

    int key;
    if (parse_key(buf, &key) < 0) {
        respond_400(fd);
        return;
    }

    int value;
    if (select_value(key, &value)) {
        respond_200(fd, value);
    } else {
        respond_404(fd);
    }
}

/* ================= main ================= */

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    init_db(100);

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT),
    };

    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(sock, BACKLOG);

    printf("[CloudDB] Listening on 0.0.0.0:%d\n", PORT);

    while (1) {
        int client = accept(sock, NULL, NULL);
        handle_client(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}