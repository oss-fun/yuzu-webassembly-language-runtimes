#include <sqlite3.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>   // rand, srand
#include <time.h>     // time

int insert(int key, int val, sqlite3 *db);
int select(int key, sqlite3 *db);

int main(int argc, char **argv)
{
  setvbuf(stdout, NULL, _IONBF, 0); //containerd-shimの出力バッファを無効化
  sqlite3 *db;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK)
  {
    fprintf(stderr, "[ERROR] %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
  }

  char *err_msg = NULL;
  int err = sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS Sample"
      "(key INTEGER PRIMARY KEY, value INTEGER);",
      NULL, NULL, &err_msg);
  if (err != SQLITE_OK) {
    printf("%s\n", err_msg);
    sqlite3_close(db);
    return -1;
  }
  printf("CREATE TABLE!\n");

  srand((unsigned int)time(NULL));

  // 1～10000のキーを順に挿入。値はランダム(0～9999)
  /*for (int i = 1; i <= 10000; i++) {
    int val = rand() % 10000;
    if (insert(i, val, db) != 0) {
      fprintf(stderr, "Failed to insert key %d\n", i);
      sqlite3_close(db);
      return -1;
    }
  }*/
 sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);
  for (int i = 1; i <= 100; i++) {
    int val = rand() % 1000;
    insert(i, val, db);
  }
  sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
  printf("Inserted 100 entries.\n");

    FILE *fp = fopen("/host/log.csv", "a");
    if (!fp) {
        perror("fopen");
        return 1;
    }

  struct timespec now, cur;

  // 無限ループでランダムキーの値を取得し続ける
  int count = 0;
  int query = 0;
  clock_gettime(CLOCK_REALTIME, &cur);
  //clock_gettime(CLOCK_REALTIME, &start);
  while (1) {
    query++;
    int key = (rand() % 100) + 1;  // 1～100のランダムキー
    //printf("query count=%d ", count);
    if (select(key, db) != 0) {
      fprintf(stderr, "Failed to select key %d\n", key);
      sqlite3_close(db);
      return -1;
    }
    count++;
    clock_gettime(CLOCK_REALTIME, &now);
    if (cur.tv_sec != now.tv_sec) {
      //1クエリごとに見るようにする、秒差分あれば累計カウントを吐く
      double unix_time = now.tv_sec + now.tv_nsec / 1e9;
      //double diff = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
      //printf("select %d-%d time: %.6f seconds\n",count-100, count, unix_time);
      FILE *fp = fopen("/host/log.csv", "a");
      if (!fp) {
        perror("fopen");
        return 1;
      }
      fprintf(fp, "%.6f,%d\n", unix_time, count);
      fflush(fp);
      fclose(fp);
      //start = end;
      count = 0;
      cur = now;
    }
    if (query % 100 == 0){
      printf("query count %d\n", query);
    }
  }

  sqlite3_close(db);
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

// select関数も元コードとほぼ同じ
int select(int key, sqlite3 *db) {
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