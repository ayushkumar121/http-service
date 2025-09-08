#include "config.h"

static Config* config = NULL;
static JsonValue* json = NULL;

// const char config[] = 

Error config_load(char *path) {
  assert(path != NULL);
  if (!file_exists(path)) {
    return errorf("file %s does not exist", path);
  }

  StringBuilder sb = {0};
  try(read_entire_file(path, &sb));
  String file_content = sb_to_sv(&sb);
  
  Error err = json_decode(file_content, &json);
  if (has_error(err)) {
    return err;
  }
  sb_free(&sb);

  config = MEM_REALLOC(NULL, sizeof(Config));
  config->port = json_get_number(json_object_get(json, "port"));

  JsonValue* mysql = json_object_get(json, "mysql");
  config->mysql.host = json_get_string(json_object_get(mysql, "host"));
  config->mysql.port = json_get_number(json_object_get(mysql, "port"));
  config->mysql.user = json_get_string(json_object_get(mysql, "user"));
  config->mysql.password = json_get_string(json_object_get(mysql, "password"));
  config->mysql.database = json_get_string(json_object_get(mysql, "database"));
  return ErrorNil;
}

Config* config_get(void) {
  assert(config != NULL);
  return config;
}

void config_free(void) {
  MEM_FREE(config);
  json_free(json);
}