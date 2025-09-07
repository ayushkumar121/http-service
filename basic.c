#include "./basic.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
  memset(ptr, 0, size);
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

Error json_error(Error cause, String s) {
  return errorf(SV_Fmt ", " SV_Fmt, SV_Arg(cause.message), SV_Arg(s));
}

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
    return JsonErrorEOF;

  String str = {0};
  str.items = sv->items;

  while (sv->length > 0 && *sv->items != '\"') {
    if (!json_consume_char(sv))
      return JsonErrorEOF;
    str.length++;
  }

  if (!json_consume_char(sv))
    return JsonErrorEOF;

  *out = json_new_string(str);
  return ErrorNil;
}

Error json_decode_array(String *sv, JsonValue **out) {
  if (*sv->items != '[')
    return json_error(JsonErrorUnexpectedToken, *sv);
  if (!json_consume_char(sv))
    return JsonErrorEOF;

  JsonArray values = {0};

  while (sv->length > 0) {
    *sv = sv_trim_left(*sv);
    if (*sv->items == ']') {
      if (!json_consume_char(sv))
        return JsonErrorEOF;
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
        return JsonErrorEOF;
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
    return JsonErrorEOF;

  JsonObject object = {0};

  while (sv->length > 0) {
    *sv = sv_trim_left(*sv);
    if (*sv->items == '}') {
      if (!json_consume_char(sv))
        return JsonErrorEOF;
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
      return JsonErrorEOF;

    JsonValue *value = NULL;
    err = json_decode_value(sv, &value);
    if (has_error(err))
      return err;
    entry.value = value;

    *sv = sv_trim_left(*sv);
    if (*sv->items == ',') {
      if (!json_consume_char(sv))
        return JsonErrorEOF;
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
    return JsonErrorEOF;

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

JsonValue *json_object_get(JsonValue *json, const char *key) {
  assert(json->type == JSON_OBJECT);
  int n = strlen(key);
  for (size_t i = 0; i < json->value.object.length; i++) {
    JsonObjectEntry entry = json->value.object.items[i];
    if (entry.key->value.string.length == n && memcmp(entry.key->value.string.items, key, n) == 0) {
      return entry.value;
    }
  }
  return NULL;
}

void json_object_set(JsonValue *json, const char *key, JsonValue *val) {
  assert(json->type == JSON_OBJECT);
  int n = strlen(key);
  int index = -1;
  for (size_t i = 0; i < json->value.object.length; i++) {
    JsonObjectEntry entry = json->value.object.items[i];
    if (entry.key->value.string.length == n && memcmp(entry.key->value.string.items, key, n) == 0) {
      index = i;
      break;
    }
  }

  if (index >= 0) {
    json->value.object.items[index].value = val;
  } else {
    array_append(&json->value.object,
                 ((JsonObjectEntry){json_new_cstring((char *)key), val}));
  }
}

bool json_object_remove(JsonValue *json, const char *key) {
  assert(json->type == JSON_OBJECT);
  int n = strlen(key);
  int index = -1;
  for (size_t i = 0; i < json->value.object.length; i++) {
    JsonObjectEntry entry = json->value.object.items[i];
    if (memcmp(entry.key->value.string.items, key, n) == 0) {
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

// HTTP Server

// For hashtable
bool string_key_eq(void *a, void *b) {
  String *sa = a;
  String *sb = b;
  return sv_equal(*sa, *sb);
}

// hash: https://theartincode.stanis.me/008-djb2/
size_t string_key_hash(int cap, void *a) {
  String *s = a;
  size_t hash = 5381;
  for (size_t i = 0; i < s->length; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)s->items[i];
  }
  return hash % cap;
}

HashTable http_headers_init(void) {
  return hash_table_init(HTTP_HEADER_CAPACITY, string_key_eq, string_key_hash);
}

bool http_headers_set(HashTable *headers, String key, String value) {
  assert(headers != NULL);
  String *key_copy = MEM_REALLOC(NULL, sizeof(String));
  *key_copy = key;
  String *value_copy = MEM_REALLOC(NULL, sizeof(String));
  *value_copy = value;
  return hash_table_set(headers, key_copy, value_copy);
}

String *http_headers_get(HashTable *headers, String key) {
  assert(headers != NULL);
  assert(key.length > 0);

  void *value = NULL;
  if (hash_table_get(headers, &key, &value)) {
    return (String *)value;
  }
  return NULL;
}

void http_headers_free(HashTable *headers) {
  assert(headers != NULL);
  if (headers->entries != NULL) {
    for (size_t i = 0; i < headers->capacity; i++) {
      HashTableEntry entry = headers->entries[i];
      if (entry.key != NULL) {
        String *k = (String *)entry.key;
        String *v = (String *)entry.value;
        MEM_FREE(k);
        MEM_FREE(v);
      }
    }
  }
  hash_table_free(headers);
}

Error http_server_init(HttpServer *server) {
  HttpServerInitOptions options = {
      .port = HTTP_DEFAULT_PORT,
      .backlog = HTTP_BACKLOG,
      .header_capacity = HTTP_HEADER_CAPACITY,
  };
  return http_server_init_opts(server, options);
}

Error http_server_init_opts(HttpServer *server, HttpServerInitOptions opt) {
  assert(server != NULL);

  server->sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->sockfd < 0) {
    return errorf("socket failed: %s", strerror(errno));
  }

  int socket_options = 1;
#ifdef SO_REUSEADDR
  if (setsockopt(server->sockfd, SOL_SOCKET, SO_REUSEADDR, &socket_options,
                 sizeof(socket_options)) < 0) {
    return errorf("setsockopt failed: %s", strerror(errno));
  }
#endif

#ifdef SO_REUSEPORT
  if (setsockopt(server->sockfd, SOL_SOCKET, SO_REUSEPORT, &socket_options,
                 sizeof(socket_options)) < 0) {
    return errorf("setsockopt failed: %s", strerror(errno));
  }
#endif

  server->addr.sin_family = AF_INET;
  server->addr.sin_addr.s_addr = INADDR_ANY;
  server->addr.sin_port = htons(opt.port);

  if (bind(server->sockfd, (struct sockaddr *)&server->addr,
           sizeof(server->addr)) < 0) {
    return errorf("bind failed: %s", strerror(errno));
  }

  return ErrorNil;
}

#define CRLF "\r\n"

void http_response_write(int clientfd, char *buffer, size_t length) {
  assert(buffer != NULL);
  assert(length > 0);

  size_t total_written = 0;
  while (total_written < length) {
    int n = write(clientfd, buffer + total_written, length - total_written);
    if (n < 0) {
      fprintf(stderr, "write failed: %s", strerror(errno));
      if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK) {
        continue;
      }
      break;
    }
    if (n == 0) {
      break;
    }
    total_written += n;
  }
}

#define HTTP_READ_BUFFER_SIZE 512

typedef enum {
  HttpErrorNil,
  HttpErrorEOF,
  HttpErrorConnectionReset,
  HttpErrorRead,
  HttpErrorParse,
  HttpErrorUnknown,
} HttpError;

String http_error_to_string(HttpError err) {
  switch (err) {
  case HttpErrorNil:
    return sv_new("nil");
  case HttpErrorEOF:
    return sv_new("eof");
  case HttpErrorConnectionReset:
    return sv_new("connection closed");
  case HttpErrorRead:
    return sv_new("read error");
  case HttpErrorParse:
    return sv_new("parse error");
  case HttpErrorUnknown:
    return sv_new("unknown");
  }
}

HttpError http_parse_request(int client, StringBuilder *sb, HttpRequest *request) {
  assert(request != NULL);

  ssize_t header_end = -1;
  char *buffer = talloc(HTTP_READ_BUFFER_SIZE); // Temp allocated buffer

  while (true) {
    int n = read(client, buffer, HTTP_READ_BUFFER_SIZE);
    if (n < 0) {
      if (errno == ECONNRESET) {
        return HttpErrorConnectionReset;
      }
      return HttpErrorRead;
    }
    if (n == 0) {
      return HttpErrorEOF;
    }
    sb_push_sv(sb, sv_new2(buffer, n));
    header_end = sv_find(sb_to_sv(sb), CRLF CRLF);
    if (header_end != -1) {
      break;
    }
  }

  String request_str = sb_to_sv(sb);

  // Parsing the request
  StringPair p0 = sv_split_str(request_str, CRLF); // (status_line vs rest)
  StringPair p1 = sv_split_delim(p0.first, ' ');   // (method vs rest)
  StringPair p2 = sv_split_delim(p1.second, ' ');  // (path vs rest)
  if (p0.first.length == 0 || p1.first.length == 0 || p2.first.length == 0) {
    return HttpErrorParse;
  }

  request->id = getpid();
  request->method = p1.first;
  request->path = p2.first;
  request->headers = http_headers_init();

  // Parsing the headers
  size_t content_length = 0;
  String sv = sv_trim(p0.second);
  while (sv.length > 0) {
    StringPair p0 = sv_split_str(sv, CRLF);        // header_line vs rest
    StringPair p1 = sv_split_delim(p0.first, ':'); // header_key vs header_value

    String key = sv_trim(p1.first);
    String value = sv_trim(p1.second);
    if (key.length == 0 || value.length == 0) {
      break;
    }

    if (sv_equal(key, sv_new("Content-Length"))) {
      content_length = atoi(value.items);
    }

    http_headers_set(&request->headers, key, value);
    sv = p0.second;
  }

  // Read body if not read yet
  while (sb->length < (header_end + 4 + content_length)) {
    int to_read = header_end + 4 + content_length - sb->length;
    if (to_read > HTTP_READ_BUFFER_SIZE) {
      to_read = HTTP_READ_BUFFER_SIZE;
    }
    int n = read(client, buffer, to_read);
    if (n < 0) {
      if (errno == ECONNRESET || errno == EPIPE) {
        return HttpErrorConnectionReset;
      }
      return HttpErrorRead;
    }
    if (n == 0) {
      return HttpErrorEOF;
    }
    sb_push_sv(sb, sv_new2(buffer, n));
  }
  request->body = sv_new2(sb->items + header_end + 4, content_length);
  request->raw_request = sb_to_sv(sb);

  return 0;
}

String http_status_code_to_string(int status_code) {
  switch (status_code) {
  case 200:
    return sv_new("OK");
  case 201:
    return sv_new("Created");
  case 301:
    return sv_new("Moved Permanently");
  case 400:
    return sv_new("Bad Request");
  case 404:
    return sv_new("Not Found");
  case 500:
    return sv_new("Internal Server Error");
  default:
    return sv_new("Unknown");
  }
}

void http_response_encode(HttpResponse *response, StringBuilder *sb) {
  sb_push_str(sb, "HTTP/1.1 ");
  sb_push_long(sb, response->status_code);
  sb_push_str(sb, " ");
  sb_push_sv(sb, http_status_code_to_string(response->status_code));
  sb_push_str(sb, CRLF);
  sb_push_str(sb, "Content-Length: ");
  sb_push_long(sb, response->body.length);
  sb_push_str(sb, CRLF);
  if (response->keep_alive) {
    sb_push_str(sb, "Connection: keep-alive");
    sb_push_str(sb, CRLF);
  } else {
    sb_push_str(sb, "Connection: close");
    sb_push_str(sb, CRLF);
  }
  for (int i = 0; i < response->headers.capacity; i++) {
    HashTableEntry entry = response->headers.entries[i];
    if (entry.key != NULL) {
      sb_push_sv(sb, *(String *)entry.key);
      sb_push_str(sb, ": ");
      sb_push_sv(sb, *(String *)entry.value);
      sb_push_str(sb, CRLF);
    }
  }
  sb_push_str(sb, CRLF);
  sb_push_sv(sb, response->body);
}

typedef struct {
  int clientfd;
  HttpListenCallback callback;
} ClientThreadArgs;

void *handle_client(void *arg) {
  ClientThreadArgs *args = arg;
  int clientfd = args->clientfd;
  HttpListenCallback callback = args->callback;
  MEM_FREE(arg);

  StringBuilder request_sb = {0};
  StringBuilder response_sb = {0};

  sb_resize(&request_sb, HTTP_READ_BUFFER_SIZE);
  sb_resize(&response_sb, HTTP_READ_BUFFER_SIZE);

  while (true) {
    request_sb.length = 0;
    response_sb.length = 0;

    HttpRequest request = {0};
    HttpError err = http_parse_request(clientfd, &request_sb, &request);
    if (err == HttpErrorEOF || err == HttpErrorConnectionReset) {
      break;
    }
    if (err != HttpErrorNil) {
      fprintf(stderr, "ERROR: http parse request failed: " SV_Fmt "\n",
              SV_Arg(http_error_to_string(err)));
      break;
    }

    HttpResponse response = callback(&request);

    http_response_encode(&response, &response_sb);
    http_response_write(clientfd, response_sb.items, response_sb.length);

    // Cleanup
    if (response.free_body_after_use)
      MEM_FREE(response.body.items);
    http_headers_free(&response.headers);

    if (!response.keep_alive) {
      break;
    }
  }

  close(clientfd);
  sb_free(&request_sb);
  sb_free(&response_sb);
  return NULL;
}

Error http_server_listen(HttpServer *server, HttpListenCallback callback) {
  assert(server != NULL);
  assert(server->sockfd > 0);
  assert(callback != NULL);

  if (listen(server->sockfd, HTTP_BACKLOG) < 0) {
    return errorf("listen failed: %s", strerror(errno));
  }

  while (true) {
    int clientfd = accept(server->sockfd, NULL, NULL);
    if (clientfd < 0) {
      fprintf(stderr, "ERROR: accept failed: %s", strerror(errno));
      continue;
    }

    ClientThreadArgs *arg = MEM_REALLOC(NULL, sizeof(ClientThreadArgs));
    arg->clientfd = clientfd;
    arg->callback = callback;

    pthread_t tid;
    if (pthread_create(&tid, NULL, handle_client, arg) < 0) {
      close(clientfd);
      fprintf(stderr, "ERROR: pthread create: %s", strerror(errno));
      continue;
    }

    pthread_detach(tid);
  }

  return ErrorNil;
}

void http_server_free(HttpServer *server) { close(server->sockfd); }

HttpResponse http_response_init(int status_code) {
  HttpResponse response = {0};
  response.headers = http_headers_init();
  response.status_code = status_code;
  response.keep_alive = true;
  response.free_body_after_use = false;
  return response;
}