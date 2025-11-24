#include <sqlite3.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>   // rand, srand
#include <time.h>     // time, clock_gettime
#include <sys/time.h> // for clock_gettime

#define NUM_ENTRIES 100
#define MAX_VALUE 1000
#define LOG_FILE "/host/log.csv"
#define QUERY_REPORT_INTERVAL 100
#define ENTRY_LIMITS 10000

// Function prototypes
int initialize_database(sqlite3 **db);
int create_table(sqlite3 *db);
int insert_entries(sqlite3 *db);
int perform_queries(sqlite3 *db);
int insert(int key, int val, sqlite3 *db);
int kvs_select(int key, sqlite3 *db);

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0); // Disable output buffering for containerd-shim

  sqlite3 *db;
  if (initialize_database(&db) != 0) {
    return -1;
  }

  if (create_table(db) != 0) {
    sqlite3_close(db);
    return -1;
  }

  if (insert_entries(db) != 0) {
    sqlite3_close(db);
    return -1;
  }

  if (perform_queries(db) != 0) {
    sqlite3_close(db);
    return -1;
  }

  sqlite3_close(db);
  return 0;
}

int initialize_database(sqlite3 **db) {
  if (sqlite3_open(":memory:", db) != SQLITE_OK) {
    fprintf(stderr, "[ERROR] %s\n", sqlite3_errmsg(*db));
    return -1;
  }
  return 0;
}

int create_table(sqlite3 *db) {
  char *err_msg = NULL;
  int err = sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS Sample "
      "(key INTEGER PRIMARY KEY, value INTEGER);",
      NULL, NULL, &err_msg);
  if (err != SQLITE_OK) {
    fprintf(stderr, "Failed to create table: %s\n", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  printf("CREATE TABLE!\n");
  return 0;
}

int insert_entries(sqlite3 *db) {
  srand((unsigned int)time(NULL));

  char *err_msg = NULL;
  sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);

  for (int i = 1; i <= NUM_ENTRIES; i++) {
    int val = rand() % MAX_VALUE;
    if (insert(i, val, db) != 0) {
      fprintf(stderr, "Failed to insert key %d\n", i);
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, &err_msg);
      return -1;
    }
  }

  sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
  printf("Inserted %d entries.\n", NUM_ENTRIES);
  return 0;
}

int perform_queries(sqlite3 *db) {
  struct timespec cur, now;
  int count = 0;
  int query = 0;

  clock_gettime(CLOCK_REALTIME, &cur);

  // while (1) {
  while (query < ENTRY_LIMITS) {
    query++;
    int key = (rand() % NUM_ENTRIES) + 1;  // 1 to NUM_ENTRIES
    if (kvs_select(key, db) != 0) {
      fprintf(stderr, "Failed to select key %d\n", key);
      return -1;
    }
    count++;

    clock_gettime(CLOCK_REALTIME, &now);
    // if (cur.tv_sec != now.tv_sec) {
    //   // Log every second
    //   double unix_time = now.tv_sec + now.tv_nsec / 1e9;
    //   FILE *fp = fopen(LOG_FILE, "a");
    //   if (!fp) {
    //     perror("fopen");
    //     return -1;
    //   }
    //   fprintf(fp, "%.6f,%d\n", unix_time, count);
    //   fflush(fp);
    //   fclose(fp);
    //   count = 0;
    //   cur = now;
    // }

    if (query % QUERY_REPORT_INTERVAL == 0) {
      printf("query count %d\n", query);
    }
  }

  return 0;
}

// insert関数は元コードと同じ
int insert(int key, int val, sqlite3 *db) {
  char *sql_command =
    "INSERT INTO Sample (key, value) VALUES (?, ?);";
  sqlite3_stmt* pStmt;

  int status = sqlite3_prepare_v2(db, sql_command, -1, &pStmt, NULL);
  if (status != SQLITE_OK) {
    fprintf(stderr, "sqlite3_prepare_v2 failed: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  sqlite3_bind_int(pStmt, 1, key);
  sqlite3_bind_int(pStmt, 2, val);

  do {
    status = sqlite3_step(pStmt);
  } while(status == SQLITE_BUSY);

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

// kvs_select: renamed from select to avoid conflict with system select()
int kvs_select(int key, sqlite3 *db) {
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
    int value = sqlite3_column_int(pStmt, 0);
    //printf("{key, value} = {%d, %d}\n", key, value);
    fflush(stdout);
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