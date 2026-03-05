#include <sqlite3.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>   // rand, srand
#include <time.h>     // time
#include <string.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "wasi_socket_ext.h" //socket(),bind()等をwasi socket APIに変換

#define PORT 8080
#define BACKLOG 128

static sqlite3 *db;

static void init_db(int n_entries) {
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
    fprintf(stderr, "[ERROR] %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    exit(1);
  }
  char *err_msg = NULL;
  sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS Sample"
      "(key INTEGER PRIMARY KEY, value INTEGER);",
      NULL, NULL, &err_msg);
  printf("CREATE TABLE!\n");

  sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);

  srand((unsigned int)time(NULL));
  for (int i = 1; i <= n_entries; i++) {
    char sql[128];
    snprintf(sql, sizeof(sql),
      "INSERT INTO Sample VALUES (%d, %d);",
      i, rand() % 1000);
    if(sqlite3_exec(db, sql, NULL, NULL, &err_msg) != SQLITE_OK) {
       fprintf(stderr, "INSERT failed: %s\n", err_msg);
       sqlite3_free(err_msg);
    }
  }
  sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
  printf("Inserted %d entries.\n", n_entries);
}

static int select_value(int key, int *out) {
  char *sql_command =
    "SELECT value FROM Sample WHERE key = ?;";
  sqlite3_stmt* pStmt;

  int status = sqlite3_prepare_v2(db, sql_command, -1, &pStmt, NULL);
  if (status != SQLITE_OK) {
    fprintf(stderr, "sqlite3_prepare_v2 failed: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  sqlite3_bind_int(pStmt, 1, key);

  int count = 0;
  while ((status = sqlite3_step(pStmt)) == SQLITE_ROW) {
    *out = sqlite3_column_int(pStmt, 0);
    count++;
  }

  if (count == 0) {
    printf("The given key %d is not found.\n", key);
  }

  if (status != SQLITE_DONE) {
    fprintf(stderr, "sqlite3_step failed: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(pStmt);
    return -1;
  }

  sqlite3_reset(pStmt);
  sqlite3_clear_bindings(pStmt);
  sqlite3_finalize(pStmt);

  return 0;
}

static int parse_key(const char *req) {
  // char e.g. GET/get?key=42
  const char *p = strstr(req, "key=");
  if (!p) return -1;
  return atoi(p + 4);
}

static void handle_client(int fd) {
  char buf[1024];
  int n = read(fd, buf, sizeof(buf) - 1);
  if (n <= 0) return;
  buf[n] = '\0';

  int key = parse_key(buf);
  int val;

  if (key < 0 || select_value(key, &val) != 0) {
    dprintf(fd,
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Type: text/plain\r\n\r\n"
      "not found\n");
    return;
  }

  dprintf(fd,
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n\r\n"
    "%d\n", val);
}

int main(int argc, char **argv)
{
  setvbuf(stdout, NULL, _IONBF, 0); //containerd-shimの出力バッファを無効化

  init_db(100);
  
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    exit(1);
  }

  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = INADDR_ANY,
    .sin_port = htons(PORT),
  };

  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    exit(1);
  }

  if (listen(sock, BACKLOG) < 0) {
    perror("listen");
    exit(1);
  }

  printf("Listening on 0.0.0.0:%d\n", PORT);

  while (1) {
    int client = accept(sock, NULL, NULL);
    if (client < 0) continue;
    handle_client(client);
    shutdown(client, SHUT_RDWR);
    close(client);
  }

  sqlite3_close(db);
  return 0;
}