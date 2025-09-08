#include "config.h"
#include "basic.h"

static JsonValue* config = NULL;

Error config_load(char *path) {
  assert(path != NULL);
  if (!file_exists(path)) {
    return errorf("file %s does not exist", path);
  }

  StringBuilder sb = {0};
  try(read_entire_file(path, &sb));
  String file_content = sb_to_sv(&sb);
  
  Error err = json_decode(file_content, &config);
  if (has_error(err)) {
    return err;
  }
  sb_free(&sb);
  return ErrorNil;
}

JsonValue* config_get(String key) {
  StringPair p = sv_split_delim(key, '.');
  JsonValue* value = config;
  while(p.first.length != 0) {
    value = json_object_get(value, p.first);
    if (value == NULL) {
      return NULL;
    }
    p = sv_split_delim(p.second, '.');
  }
  return value;
}

String config_get_string(String key, String default_value) {
  JsonValue* value = config_get(key);
  if (value == NULL) {
    return default_value;
  }
  return json_get_string(value);
} 

double config_get_double(String key, int default_value) {
  JsonValue* value = config_get(key);
  if (value == NULL) {
    return default_value;
  }
  return json_get_number(value);
}

int config_get_int(String key, int default_value) {
  return (int)config_get_double(key, default_value);
}

void config_free(void) {
  json_free(config);
}