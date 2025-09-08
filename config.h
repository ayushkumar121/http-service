#ifndef CONFIG_H
#define CONFIG_H

#include "basic.h"

typedef struct {
  String host;
  int port;
  String user;
  String password;
  String database;
} MysqlConfig;

typedef struct {
  int port;
  MysqlConfig mysql;
} Config;

Error config_load(char *path);
Config* config_get(void);
void config_free(void);

#endif