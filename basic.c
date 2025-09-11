#include "./basic.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <sys/stat.h>

// Globals
_Thread_local size_t temp_allocated = 0;
_Thread_local uint8_t temp_buffer[TEMP_BUFFER_CAP];

size_t align(size_t size) {
  if (size % 8 == 0)
    return size;
  return size + (8 - size % 8);
}

void *talloc(size_t n) {
  size_t size = align(n);
  assert(size <= TEMP_BUFFER_CAP);

  if (temp_allocated + size >= TEMP_BUFFER_CAP) {
    temp_allocated = 0;
  }

  void *ptr = &temp_buffer[temp_allocated];
  temp_allocated += size;
  return ptr;
}

void treset() { temp_allocated = 0; }

// Error Handling

Error error(char *message) { return error_sv(sv_new(message)); }
Error error_sv(String message) { return (Error){.message = message}; }

Error errorf(const char *format, ...) {
  va_list args;

  va_start(args, format);
  String str = tvprintf(format, args);
  va_end(args);

  return error_sv(str);
}

bool has_error(Error err) { return err.message.length > 0; }

void _try(Error err, char *file, int line) {
  if (has_error(err)) {
    fprintf(stderr, "%s:%d: thread paniced: " SV_Fmt "\n", file, line,
            SV_Arg(err.message));
    exit(1);
  }
}

/* String Builder */

void sb_resize(StringBuilder *sb, size_t new_capacity) {
  sb->capacity = new_capacity;
  sb->items = MEM_REALLOC(sb->items, sb->capacity + 1);
}

void sb_free(StringBuilder *sb) { array_free(sb); }

String sb_to_sv(StringBuilder *sb) {
  String sv = {0};
  sv.items = sb->items;
  sv.length = sb->length;
  return sv;
}

void sb_push_str(StringBuilder *sb, char *str) {
  size_t item_len = strlen(str);
  if (sb->capacity < (sb->length + item_len + 1)) {
    sb_resize(sb, sb->capacity + item_len);
  }

  memcpy(sb->items + sb->length, str, item_len);
  sb->length += item_len;
  sb->items[sb->length] = 0;
}

void sb_push_sv(StringBuilder *sb, String sv) {
  if (sb->capacity < (sb->length + sv.length + 1)) {
    sb_resize(sb, sb->capacity + sv.length);
  }

  memcpy(sb->items + sb->length, sv.items, sv.length);
  sb->length += sv.length;
  sb->items[sb->length] = 0;
}

void sb_push_char(StringBuilder *sb, char ch) {
  if (sb->capacity < (sb->length + 2)) {
    sb_resize(sb, sb->capacity + 1);
  }

  sb->items[sb->length] = ch;
  sb->length += 1;
  sb->items[sb->length] = 0;
}

void sb_push_long(StringBuilder *sb, long i) {
  if (i == 0) {
    sb_push_char(sb, '0');
    return;
  }

  bool is_neg = i < 0;
  if (is_neg)
    i *= -1;

  int k = (is_neg) ? log10(i) + 2 : log10(i) + 1;
  sb_resize(sb, sb->capacity + k);

  int j = k;
  while (i != 0) {
    sb->items[sb->length + j - 1] = i % 10 + '0';
    i = i / 10;
    j -= 1;
  }

  if (is_neg)
    sb->items[sb->length] = '-';
  sb->length += k;
  sb->items[sb->length] = 0;
}

void sb_push_double(StringBuilder *sb, double d) {
  // Handling int part
  int i = (d < 0) ? ceil(d) : floor(d);
  sb_push_long(sb, i);

  // Handling fractional part
  int f = (d - i) * 1000000;
  if (f < 0)
    f *= -1;
  if (f > 0) {
    sb_push_char(sb, '.');
    sb_push_long(sb, f);
  }
}

void sb_push_float(StringBuilder *sb, float f) {
  sb_push_double(sb, (double)f);
}

StringBuilder sb_clone(StringBuilder *sb) {
  StringBuilder clone = {0};
  sb_push_str(&clone, sb->items);
  return clone;
}

/* String View */

String sv_new2(char *str, size_t len) {
  return (String){.length = len, .items = str};
}

String sv_new(char *str) {
  String sv = {0};
  sv.items = str;
  sv.length = strlen(str);
  return sv;
}

bool sv_equal(String s1, String s2) {
  if (s1.length != s2.length)
    return false;

  for (size_t i = 0; i < s1.length; i++) {
    if (s1.items[i] != s2.items[i])
      return false;
  }

  return true;
}

bool sv_equal_ignorecase(String s1, String s2) {
  if (s1.length != s2.length)
    return false;

  for (size_t i = 0; i < s1.length; i++) {
    if (tolower(s1.items[i]) != tolower(s2.items[i]))
      return false;
  }
  
  return true;
}

String sv_trim_left(String sv) {
  String result = sv;
  while (result.length > 0 && isspace(*result.items)) {
    result.items++;
    result.length--;
  }
  return result;
}

String sv_trim_right(String sv) {
  String result = sv;
  while (result.length > 0 && isspace(result.items[result.length - 1]))
    result.length--;
  return result;
}

String sv_trim(String sv) { return sv_trim_left(sv_trim_right(sv)); }

StringPair sv_split_delim(String sv, char delim) {
  StringPair result = {0};

  size_t i = 0;
  while (i < sv.length && sv.items[i] != delim)
    i++;

  // No delimiter found
  if (i == sv.length) {
    result.first = sv;
    result.second = StringNil;
    return result;
  }

  result.first = sv_new2(sv.items, i);
  result.second = sv_new2(sv.items + i + 1, sv.length - i - 1);

  return result;
}

ssize_t sv_find(String sv, char *str) {
  size_t i = 0;
  size_t n = strlen(str);

  for (i = 0; i < sv.length; i++) {
    if (sv.items[i] == str[0] && i + n <= sv.length) {
      if (memcmp(str, &sv.items[i], n) == 0) {
        return i;
      }
    }
  }

  return -1;
}

StringPair sv_split_str(String sv, char *str) {
  StringPair result = {0};

  size_t n = strlen(str);
  if (n == 0 || sv.length < n) {
    result.first = sv;
    result.second = StringNil;
    return result;
  }

  size_t i = 0;
  bool found = false;
  for (i = 0; i + n <= sv.length; i++) {
    if (memcmp(str, &sv.items[i], n) == 0) {
      found = true;
      break;
    }
  }

  // No match found
  if (!found) {
    result.first = sv;
    result.second = StringNil;
    return result;
  }

  result.first = sv_new2(sv.items, i);
  result.second = sv_new2(sv.items + i + n, sv.length - i - n);

  return result;
}

String sv_clone(String sv) {
  char *str_copy = MEM_REALLOC(NULL, sv.length + 1);
  memcpy(str_copy, sv.items, sv.length);
  str_copy[sv.length] = 0;
  return sv_new2(str_copy, sv.length);
}

String tprintf(const char *format, ...) {
  va_list args, args2;

  va_start(args, format);
  va_copy(args2, args);

  size_t n = vsnprintf(NULL, 0, format, args);
  assert(n > 0);

  char *str = talloc(n + 1);
  vsnprintf(str, n + 1, format, args2);

  va_end(args2);
  va_end(args);

  return sv_new2(str, n);
}

String tvprintf(const char *format, va_list args) {
  va_list args2;
  va_copy(args2, args);

  size_t n = vsnprintf(NULL, 0, format, args);
  assert(n > 0);

  char *str = talloc(n + 1);
  vsnprintf(str, n + 1, format, args2);

  va_end(args2);

  return sv_new2(str, n);
}

/* Hash Table */

HashTable hash_table_init(size_t capacity, KeyEqFunc key_eq,
                          KeyHashFunc key_hash) {
  assert(key_eq != NULL && "key_eq is required");
  assert(key_hash != NULL && "key_hash is required");

  HashTable v = {0};

  size_t sz = capacity * sizeof(HashTableEntry);
  v.entries = (HashTableEntry *)MEM_REALLOC(NULL, sz);
  v.capacity = capacity;
  v.key_eq = key_eq;
  v.key_hash = key_hash;

  memset(v.entries, 0, sz);
  return v;
}

bool hash_table_set(HashTable *v, void *key, void *val) {
  assert(v != NULL && "map is null");
  assert(key != NULL && "key is null");
  assert(val != NULL && "value is null");
  assert(v->entries != NULL && "uninitialized map");

  size_t index = v->key_hash(v->capacity, key);
  bool index_found = false;

  for (size_t i = 0; i < v->capacity; ++i) {
    size_t try_index = (i + index) % v->capacity;
    HashTableEntry entry = v->entries[try_index];

    if (entry.key == NULL || v->key_eq(entry.key, key)) {
      index = try_index;
      index_found = true;
      break;
    }
  }
  if (!index_found)
    return false;

  v->entries[index].key = key;
  v->entries[index].value = val;
  v->length++;

  return true;
}

bool hash_table_get(HashTable *v, void *key, void **out) {
  assert(v != NULL && "map is null");
  assert(key != NULL && "key is null");
  assert(v->entries != NULL && "uninitialized map");

  size_t index = v->key_hash(v->capacity, key);
  bool index_found = false;

  for (size_t i = 0; i < v->capacity; ++i) {
    size_t try_index = (i + index) % v->capacity;
    HashTableEntry entry = v->entries[try_index];

    if (entry.key != NULL && v->key_eq(entry.key, key)) {
      index = try_index;
      index_found = true;
      break;
    }
  }
  if (!index_found)
    return false;
  if (out != NULL) {
    *out = v->entries[index].value;
  }

  return true;
}

bool hash_table_remove(HashTable *v, void *key, void **out) {
  assert(v != NULL && "map is null");
  assert(key != NULL && "key is null");
  assert(v->entries != NULL && "uninitialized map");

  size_t index = v->key_hash(v->capacity, key);
  bool index_found = false;

  for (size_t i = 0; i < v->capacity; ++i) {
    size_t try_index = (i + index) % v->capacity;
    HashTableEntry entry = v->entries[try_index];

    if (entry.key != NULL && v->key_eq(entry.key, key)) {
      index = try_index;
      index_found = true;
      break;
    }
  }
  if (!index_found)
    return false;

  if (out != NULL) {
    *out = v->entries[index].value;
  }

  v->entries[index].key = NULL;
  v->entries[index].value = NULL;
  v->length--;

  return true;
}

void hash_table_free(HashTable *v) {
  assert(v != NULL && "map is null");

  if (v->entries) {
    MEM_FREE(v->entries);
    v->entries = NULL;
    v->capacity = 0;
  }
}

// Json Encoding & Decoding

JsonValue *json_new_null(void) {
  JsonValue *value = MEM_REALLOC(NULL, sizeof(JsonValue));
  value->type = JSON_NULL;
  return value;
}

JsonValue *json_new_bool(bool b) {
  JsonValue *value = MEM_REALLOC(NULL, sizeof(JsonValue));
  value->type = (b) ? JSON_TRUE : JSON_FALSE;
  return value;
}

JsonValue *json_new_number(double n) {
  JsonValue *value = MEM_REALLOC(NULL, sizeof(JsonValue));
  value->type = JSON_NUMBER;
  value->value.number = n;
  return value;
}

JsonValue *json_new_string(String s) {
  JsonValue *value = MEM_REALLOC(NULL, sizeof(JsonValue));
  value->type = JSON_STRING;
  value->value.string = sv_clone(s);
  return value;
}

JsonValue *json_new_cstring(char *s) {
  JsonValue *value = MEM_REALLOC(NULL, sizeof(JsonValue));
  value->type = JSON_STRING;
  value->value.string = sv_clone(sv_new(s));
  return value;
}

JsonValue *json_new_array(void) {
  JsonValue *value = MEM_REALLOC(NULL, sizeof(JsonValue));
  value->type = JSON_ARRAY;
  value->value.array = (JsonArray){0};
  return value;
}

JsonValue *json_new_object(void) {
  JsonValue *value = MEM_REALLOC(NULL, sizeof(JsonValue));
  value->type = JSON_OBJECT;
  value->value.object = (JsonObject){0};
  return value;
}

Error _json_error(Error cause, String s, const char* file, int line) {
  return errorf(SV_Fmt ": \"" SV_Fmt "\" at %s:%d", SV_Arg(cause.message), SV_Arg(s), file, line);
}

#define json_error(cause, s) _json_error(cause, s, __FILE__, __LINE__)

bool json_is_end_char(char ch) { return ch == ',' || ch == ']' || ch == '}'; }

bool json_consume_char(String *sv) {
  if (sv->length == 0)
    return false;
  sv->items++;
  sv->length--;
  return true;
}

bool json_consume_literal(String *sv, char *str, size_t n) {
  if (sv->length < n)
    return false;

  for (size_t i = 0; i < n; i++) {
    if (*sv->items != str[i])
      return false;

    if (!json_consume_char(sv))
      return false;
  }

  return true;
}

Error json_decode_value(String *sv, JsonValue **out);

Error json_decode_null(String *sv, JsonValue **out) {
  if (!json_consume_literal(sv, "null", 4))
    return json_error(JsonErrorUnexpectedToken, *sv);

  *out = json_new_null();
  return ErrorNil;
}

Error json_decode_true(String *sv, JsonValue **out) {
  if (!json_consume_literal(sv, "true", 4))
    return json_error(JsonErrorUnexpectedToken, *sv);

  *out = json_new_bool(true);
  return ErrorNil;
}

Error json_decode_false(String *sv, JsonValue **out) {
  if (!json_consume_literal(sv, "false", 5))
    return json_error(JsonErrorUnexpectedToken, *sv);

  *out = json_new_bool(false);
  return ErrorNil;
}

Error json_decode_number(String *sv, JsonValue **out) {
  char *endptr;
  double number = strtod(sv->items, &endptr);
  if (endptr == sv->items)
    return JsonErrorUnexpectedToken;

  sv->length -= (endptr - sv->items);
  sv->items = endptr;

  *out = json_new_number(number);
  return ErrorNil;
}

Error json_decode_string(String *sv, JsonValue **out) {
  if (*sv->items != '\"')
    return json_error(JsonErrorUnexpectedToken, *sv);
  if (!json_consume_char(sv))
    return json_error(JsonErrorEOF, *sv);

  String str = {0};
  str.items = sv->items;

  while (sv->length > 0 && *sv->items != '\"') {
    if (!json_consume_char(sv))
      return json_error(JsonErrorEOF, *sv);
    str.length++;
  }

  if (!json_consume_char(sv))
    return json_error(JsonErrorEOF, *sv);

  *out = json_new_string(str);
  return ErrorNil;
}

Error json_decode_array(String *sv, JsonValue **out) {
  if (*sv->items != '[')
    return json_error(JsonErrorUnexpectedToken, *sv);
  if (!json_consume_char(sv))
    return json_error(JsonErrorEOF, *sv);

  JsonArray values = {0};

  while (sv->length > 0) {
    *sv = sv_trim_left(*sv);
    if (*sv->items == ']') {
      if (!json_consume_char(sv))
        return json_error(JsonErrorEOF, *sv);
      break;
    }

    JsonValue *value = NULL;
    Error err = json_decode_value(sv, &value);
    if (has_error(err))
      return err;

    array_append(&values, value);

    *sv = sv_trim_left(*sv);
    if (*sv->items == ',') {
      if (!json_consume_char(sv))
        return json_error(JsonErrorEOF, *sv);
    }
  }

  *out = json_new_array();
  (*out)->value.array = values;
  return ErrorNil;
}

Error json_decode_object(String *sv, JsonValue **out) {
  if (*sv->items != '{')
    return json_error(JsonErrorUnexpectedToken, *sv);
  if (!json_consume_char(sv))
    return json_error(JsonErrorEOF, *sv);

  JsonObject object = {0};

  while (sv->length > 0) {
    *sv = sv_trim_left(*sv);
    if (*sv->items == '}') {
      if (!json_consume_char(sv))
        return json_error(JsonErrorEOF, *sv);
      break;
    }

    JsonObjectEntry entry = {0};
    Error err;

    JsonValue *key = NULL;
    err = json_decode_string(sv, &key);
    if (has_error(err))
      return err;
    entry.key = key;

    *sv = sv_trim_left(*sv);
    if (*sv->items != ':') {
      return json_error(JsonErrorUnexpectedToken, *sv);
    }
    if (!json_consume_char(sv))
      return json_error(JsonErrorEOF, *sv);

    JsonValue *value = NULL;
    err = json_decode_value(sv, &value);
    if (has_error(err))
      return err;
    entry.value = value;

    *sv = sv_trim_left(*sv);
    if (*sv->items == ',') {
      if (!json_consume_char(sv))
        return json_error(JsonErrorEOF, *sv);
    }

    array_append(&object, entry);
  }

  *out = json_new_object();
  (*out)->value.object = object;
  return ErrorNil;
}

Error json_decode_value(String *sv, JsonValue **out) {
  *sv = sv_trim_left(*sv);
  if (sv->length == 0)
    return json_error(JsonErrorEOF, *sv);

  switch (*sv->items) {
  case 'n':
    return json_decode_null(sv, out);
  case 't':
    return json_decode_true(sv, out);
  case 'f':
    return json_decode_false(sv, out);
  case '\"':
    return json_decode_string(sv, out);
  case '[':
    return json_decode_array(sv, out);
  case '{':
    return json_decode_object(sv, out);
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case '-': {
    return json_decode_number(sv, out);
  }
  default:
    return json_error(JsonErrorUnexpectedToken, *sv);
  }
}

Error json_decode(String sv, JsonValue **out) {
  String sv2 = sv_trim(sv);
  Error err = json_decode_value(&sv2, out);
  if (has_error(err))
    return err;
  if (sv2.length > 0)
    return json_error(JsonErrorUnexpectedToken, sv2);
  return ErrorNil;
}

JsonNumber json_get_number(JsonValue *json) {
  assert(json->type == JSON_NUMBER);
  return json->value.number;
}

JsonString json_get_string(JsonValue *json) {
  assert(json->type == JSON_STRING);
  return json->value.string;
}

JsonValue *json_object_get(JsonValue *json, String key) {
  assert(json->type == JSON_OBJECT);
  for (size_t i = 0; i < json->value.object.length; i++) {
    JsonObjectEntry entry = json->value.object.items[i];
    if (sv_equal(entry.key->value.string, key)) {
      return entry.value;
    }
  }
  return NULL;
}

void json_object_set(JsonValue *json, String key, JsonValue *val) {
  assert(json->type == JSON_OBJECT);
  int index = -1;
  for (size_t i = 0; i < json->value.object.length; i++) {
    JsonObjectEntry entry = json->value.object.items[i];
    if (sv_equal(entry.key->value.string, key)) {
      index = i;
      break;
    }
  }

  if (index >= 0) {
    json->value.object.items[index].value = val;
  } else {
    array_append(&json->value.object,
                 ((JsonObjectEntry){json_new_string(key), val}));
  }
}

bool json_object_remove(JsonValue *json, String key) {
  assert(json->type == JSON_OBJECT);
  int index = -1;
  for (size_t i = 0; i < json->value.object.length; i++) {
    JsonObjectEntry entry = json->value.object.items[i];
    if (sv_equal(entry.key->value.string, key)) {
      index = i;
      break;
    }
  }

  if (index != -1) {
    array_remove(&json->value.object, index);
    return true;
  }

  return false;
}

JsonValue *json_array_get(JsonValue *json, int i) {
  assert(json->type == JSON_ARRAY);
  assert(i < json->value.array.length);
  return json->value.array.items[i];
}

void json_array_append(JsonValue *json, JsonValue *val) {
  assert(json->type == JSON_ARRAY);
  array_append(&json->value.array, val);
}

void json_array_remove(JsonValue *json, size_t index) {
  assert(json->type == JSON_ARRAY);
  assert(index < json->value.array.length);
  array_remove(&json->value.array, index);
}

void sb_push_whitespace(StringBuilder *sb, int indent) {
  for (int i = 0; i < indent; i++) {
    sb_push_char(sb, ' ');
  }
}

void _json_encode(JsonValue json, StringBuilder *sb, int pp, int indent) {
  switch (json.type) {
  case JSON_NULL:
    sb_push_str(sb, "null");
    break;
  case JSON_TRUE:
    sb_push_str(sb, "true");
    break;
  case JSON_FALSE:
    sb_push_str(sb, "false");
    break;
  case JSON_NUMBER:
    sb_push_double(sb, json.value.number);
    break;
  case JSON_STRING:
    sb_push_char(sb, '\"');
    sb_push_sv(sb, json.value.string);
    sb_push_char(sb, '\"');
    break;

  case JSON_ARRAY: {
    sb_push_char(sb, '[');
    if (json.value.array.length > 0 && pp > 0)
      sb_push_char(sb, '\n');

    for (int i = 0; i < json.value.array.length; i++) {
      if (pp > 0)
        sb_push_whitespace(sb, indent);
      _json_encode(*json.value.array.items[i], sb, pp, indent + pp);
      if (i < json.value.array.length - 1)
        sb_push_char(sb, ',');
      if (pp > 0)
        sb_push_char(sb, '\n');
    }
    if (json.value.array.length > 0 && pp > 0)
      sb_push_whitespace(sb, indent - pp);
    sb_push_char(sb, ']');
    break;
  }

  case JSON_OBJECT: {
    sb_push_char(sb, '{');
    if (json.value.object.length > 0 && pp > 0)
      sb_push_char(sb, '\n');

    for (int i = 0; i < json.value.object.length; i++) {
      if (pp > 0)
        sb_push_whitespace(sb, indent);
      _json_encode(*json.value.object.items[i].key, sb, pp, indent + pp);
      sb_push_char(sb, ':');
      _json_encode(*json.value.object.items[i].value, sb, pp, indent + pp);
      if (i < json.value.array.length - 1)
        sb_push_char(sb, ',');
      if (pp > 0)
        sb_push_char(sb, '\n');
    }
    if (json.value.object.length > 0 && pp > 0)
      sb_push_whitespace(sb, indent - pp);
    sb_push_char(sb, '}');
    break;
  }
  }
}

void json_encode(JsonValue json, StringBuilder *sb, int pp) {
  return _json_encode(json, sb, pp, pp);
}

void json_print(FILE *f, JsonValue json, int pp) {
  StringBuilder sb = {0};
  json_encode(json, &sb, pp);
  fprintf(f, SV_Fmt "\n", SV_Arg(sb));
  sb_free(&sb);
}

void json_free(JsonValue *json) {
  switch (json->type) {
  case JSON_NULL:
  case JSON_TRUE:
  case JSON_FALSE:
  case JSON_NUMBER:
    MEM_FREE(json);
    break;
  case JSON_STRING:
    MEM_FREE(json->value.string.items);
    MEM_FREE(json);
    break;

  case JSON_ARRAY: {
    for (int i = 0; i < json->value.array.length; i++) {
      json_free(json->value.array.items[i]);
    }
    if (json->value.array.items)
      MEM_FREE(json->value.array.items);
    MEM_FREE(json);
    break;
  }

  case JSON_OBJECT: {
    for (int i = 0; i < json->value.object.length; i++) {
      json_free(json->value.object.items[i].key);
      json_free(json->value.object.items[i].value);
    }
    if (json->value.object.items)
      MEM_FREE(json->value.object.items);
    MEM_FREE(json);
    break;
  }
  }
}

// File I/O

size_t file_size(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return st.st_size;
  }
  return 0;
}

bool file_exists(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return true;
  }
  return false;
}

Error read_entire_file(const char *path, StringBuilder *sb) {
  size_t size = file_size(path);
  if (size == 0) {
    return ErrorFileEmpty;
  }
  sb_resize(sb, size);
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    return ErrorReadFile;
  }
  int n = fread(sb->items, 1, size, file);
  if (n != size) {
    return errorf("failed to read file %s: %s", path, strerror(errno));
  }
  sb->length = n;
  sb->items[n] = 0;
  fclose(file);
  return ErrorNil;
}

Error write_entire_file(const char *path, String sv) {
  FILE *file = fopen(path, "w");
  if (file == NULL) {
    return ErrorWriteFile;
  }
  int n = fwrite(sv.items, 1, sv.length, file);
  if (n != sv.length) {
    return errorf("failed to write file %s: %s", path, strerror(errno));
  }
  fclose(file);
  return ErrorNil;
}
