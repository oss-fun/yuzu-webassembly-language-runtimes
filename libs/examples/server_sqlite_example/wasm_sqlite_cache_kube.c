#include <sqlite3.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "wasi_socket_ext.h"

#define EDGE_PORT 8080
#define CLOUD_PORT 30081
#define CLOUD_IP "192.168.25.11"

#define BACKLOG 128
#define BUF_SIZE 2048

static sqlite3 *cache_db;

/* ================= sqlite cache ================= */

static void init_cache_db(void) {
    if (sqlite3_open(":memory:", &cache_db) != SQLITE_OK) {
        fprintf(stderr, "[EDGE] sqlite open failed: %s\n",
                sqlite3_errmsg(cache_db));
        exit(1);
    }

    sqlite3_exec(
        cache_db,
        "CREATE TABLE Cache (key INTEGER PRIMARY KEY, value INTEGER);",
        NULL, NULL, NULL);

    //printf("[EDGE] Cache DB initialized\n");
}

static int cache_lookup(int key, int *out) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        cache_db,
        "SELECT value FROM Cache WHERE key = ?;",
        -1, &stmt, NULL);

    sqlite3_bind_int(stmt, 1, key);

    int ret = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out = sqlite3_column_int(stmt, 0);
        ret = 0;
    }

    sqlite3_finalize(stmt);
    return ret;
}

static void cache_insert(int key, int value) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        cache_db,
        "INSERT OR REPLACE INTO Cache VALUES (?, ?);",
        -1, &stmt, NULL);

    sqlite3_bind_int(stmt, 1, key);
    sqlite3_bind_int(stmt, 2, value);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

/* ================= HTTP helpers ================= */

static int http_parse_get_key(const char *req, int *out_key) {
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

static void respond_cache_dump(int fd) {
    sqlite3_stmt *stmt;

    dprintf(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n");

    if (sqlite3_prepare_v2(
            cache_db,
            "SELECT key, value FROM Cache ORDER BY key;",
            -1, &stmt, NULL) != SQLITE_OK) {
        dprintf(fd, "sqlite error\n");
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int key = sqlite3_column_int(stmt, 0);
        int value = sqlite3_column_int(stmt, 1);
        dprintf(fd, "key=%d value=%d\n", key, value);
    }

    sqlite3_finalize(stmt);
}

/* ================= Cloud access ================= */

static int fetch_from_cloud(int key, int *out) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CLOUD_PORT),
    };
    inet_pton(AF_INET, CLOUD_IP, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    dprintf(sock,
        "GET /get?key=%d HTTP/1.1\r\n"
        "Host: cloud-db\r\n"
        "\r\n", key);

    char buf[BUF_SIZE];
    int n = read(sock, buf, sizeof(buf) - 1);
    close(sock);

    if (n <= 0)
        return -1;

    buf[n] = '\0';

    if (strncmp(buf, "HTTP/1.1 200", 12) != 0)
        return -1;

    char *body = strstr(buf, "\r\n\r\n");
    if (!body)
        return -1;

    *out = atoi(body + 4);
    return 0;
}

/* ================= client handler ================= */

static int is_cache_dump(const char *req) {
    return strstr(req, "GET /cache") != NULL;
}

static void handle_client(int fd) {
    char buf[BUF_SIZE];
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0)
        return;
    buf[n] = '\0';

    if (is_cache_dump(buf)) {
        respond_cache_dump(fd);
        return;
    }

    int key;
    if (http_parse_get_key(buf, &key) < 0) {
        respond_400(fd);
        return;
    }

    int value;
    if (cache_lookup(key, &value) == 0) {
        //printf("[EDGE] Cache HIT key=%d\n", key);
        respond_200(fd, value);
        return;
    }

    //printf("[EDGE] Cache MISS key=%d\n", key);
    if (fetch_from_cloud(key, &value) == 0) {
        cache_insert(key, value);
        respond_200(fd, value);
    } else {
        respond_404(fd);
    }
}

/* ================= main ================= */

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    init_cache_db();

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(EDGE_PORT),
    };

    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(sock, BACKLOG);

    //printf("[EDGE] Listening on 0.0.0.0:%d\n", EDGE_PORT);

    while (1) {
        int client = accept(sock, NULL, NULL);
        handle_client(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}