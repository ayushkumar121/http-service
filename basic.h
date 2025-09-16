#ifndef BASIC_H
#define BASIC_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DEBUG(format, ...) fprintf(stderr, "DEBUG: " format "\n", ##__VA_ARGS__)
#define INFO(format, ...) fprintf(stderr, "INFO: " format "\n", ##__VA_ARGS__)
#define WARN(format, ...) fprintf(stderr, "WARN: " format "\n", ##__VA_ARGS__)
#define ERROR(format, ...) fprintf(stderr, "ERROR: " format "\n", ##__VA_ARGS__)

#define PAIR(T1, T2)                                                           \
  struct {                                                                     \
    T1 first;                                                                  \
    T2 second;                                                                 \
  }

#define ARRAY_INIT_CAP 2
#define ARRAY(T)                                                               \
  struct {                                                                     \
    size_t length;                                                             \
    size_t capacity;                                                           \
    T *items;                                                                  \
  }

#define array_append(array, item)                                              \
  do {                                                                         \
    if ((array)->capacity < (array)->length + 1) {                             \
      (array)->capacity =                                                      \
          ((array)->capacity == 0) ? ARRAY_INIT_CAP : (array)->capacity * 1.5; \
      void* ptr = realloc(                                                     \
          (array)->items, (array)->capacity * sizeof(*(array)->items));        \
      assert(ptr != NULL);                                                     \
      (array)->items = ptr;                                                    \
    }                                                                          \
    (array)->items[(array)->length++] = (item);                                \
  } while (0)

#define array_remove(array, i)                                                 \
  do {                                                                         \
    size_t j = (i);                                                            \
    assert(j < (array)->length);                                               \
    (array)->items[j] = (array)->items[--(array)->length];                     \
  } while (0)

#define array_free(array)                                                      \
  do {                                                                         \
    if ((array)->capacity > 0)                                                 \
      free((array)->items);                                                \
  } while (0)

// Temp Allocator
#define TEMP_BUFFER_CAP (4 * 1024)
void *talloc(size_t bytes);
void treset();

// String
typedef struct {
  size_t length;
  size_t capacity;
  char *items;
} StringBuilder;

typedef struct {
  size_t length;
  char *items;
} String;

#define StringNil (String){0}

void sb_resize(StringBuilder *sb, size_t new_capacity);
void sb_free(const StringBuilder *sb);

void sb_push_str(StringBuilder *sb, const char *str);
void sb_push_sv(StringBuilder *sb, String sv);
void sb_push_char(StringBuilder *sb, char ch);
void sb_push_long(StringBuilder *sb, long l);
void sb_push_double(StringBuilder *sb, double d);
String sb_to_sv(const StringBuilder *sb);
StringBuilder sb_clone(const StringBuilder *sb);

#define SV_Fmt "%.*s"
#define SV_Arg(sv) (int)(sv).length, (sv).items

#define SV2(s, len) (String){len, s}
#define SV(s) (String){sizeof(s)-1, s}

bool sv_equal(String s1, String s2);
bool sv_equal_ignore_case(String s1, String s2);
ssize_t sv_find(String sv, const char *str);
String sv_trim_left(String sv);
String sv_trim_right(String sv);
String sv_trim(String sv);

typedef PAIR(String, String) StringPair;

StringPair sv_split_delim(String sv, char delim);
StringPair sv_split_str(String sv, const char *str);
String sv_clone(String sv); // Clones the string on the heap

String tprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
String tvprintf(const char *format, va_list);

// endptr saves the
long sv_to_long(String sv, char **endptr);
int sv_to_int(String sv, char **endptr);

String sv_escape(String sv); // This heap allocates memory

// Hash Table

typedef struct {
  void *key;
  void *value;
} HashTableEntry;

typedef bool (*KeyEqFunc)(void *a, void *b);
typedef size_t (*KeyHashFunc)(size_t capacity, void *a);

typedef struct {
  HashTableEntry *entries;
  size_t length;
  size_t capacity;

  KeyEqFunc key_eq;
  KeyHashFunc key_hash;
} HashTable;

HashTable hash_table_init(size_t capacity, KeyEqFunc key_eq,
                          KeyHashFunc key_hash);
bool hash_table_set(HashTable *v, void *key, void *val);
bool hash_table_get(const HashTable *v, void *key, void **out);
bool hash_table_remove(HashTable *v, void *key, void **out);
void hash_table_free(HashTable *v);

// Error Handling
typedef struct {
  String message;
} Error;

#define MAX_ERROR_LENGTH 500
#define ErrorNil (Error){0}

Error error(char *message);
Error error_sv(String message);
Error errorf(const char *format, ...) __attribute__((format(printf, 1, 2)));
bool has_error(Error err);

void try_(Error err, char *file, int line);
#define try(err) try_(err, __FILE__, __LINE__)

// JSON Encoding & Decoding

#define JsonErrorEOF                                                           \
  (Error) { SV("json eof") }
#define JsonErrorUnexpectedToken                                               \
  (Error) { SV("json unexpected token") }

typedef enum {
  JSON_NULL,
  JSON_BOOL,
  JSON_NUMBER,
  JSON_STRING,
  JSON_OBJECT,
  JSON_ARRAY
} JsonType;

typedef struct JsonObjectEntry JsonObjectEntry;
typedef struct JsonValue JsonValue;
typedef double JsonNumber;
typedef bool JsonBoolean;
typedef String JsonString;
typedef ARRAY(JsonValue *) JsonArray;
typedef ARRAY(JsonObjectEntry) JsonObject;

typedef struct JsonValue {
  JsonType type;
  union {
    JsonBoolean boolean;
    JsonNumber number;
    JsonString string;
    JsonArray array;
    JsonObject object;
  } as;
} JsonValue;

typedef struct JsonObjectEntry {
  String key;
  JsonValue *value;
} JsonObjectEntry;

JsonValue *json_new_null(void);
JsonValue *json_new_bool(bool b);
JsonValue *json_new_number(double n);
JsonValue *json_new_string(String s);
JsonValue *json_new_cstr(char *s);
JsonValue *json_new_array(void);
JsonValue *json_new_object(void);

Error json_decode(String sv, JsonValue **out);
JsonNumber json_get_number(const JsonValue *json);
JsonString json_get_string(const JsonValue *json);

JsonValue *json_object_get(const JsonValue *json, String key);
JsonValue *json_get(const JsonValue *json, String key);
void json_object_set(JsonValue *json, String key, JsonValue *val);
bool json_object_remove(JsonValue *json, String key);

JsonValue *json_array_get(const JsonValue *json, int index);
void json_array_append(JsonValue *json, JsonValue *val);
void json_array_remove(JsonValue *json, size_t index);

void json_encode(JsonValue json, StringBuilder *sb, int pp);
void json_print(FILE *file, JsonValue json, int pp);
void json_free(JsonValue *json);

// File I/O

#define ErrorReadFile                                                          \
  (Error) { SV("failed to read file") }
#define ErrorWriteFile                                                         \
  (Error) { SV("failed to write file") }
#define ErrorFileEmpty                                                         \
  (Error) { SV("file is empty") }

size_t file_size(const char *path);
bool file_exists(const char *path);
Error read_entire_file(const char *path, StringBuilder *sb);
Error write_entire_file(const char *path, String sv);

// UUID
#define RANDOM_ID_LEN 12

Error get_random_bytes(char* buf, size_t n);
String random_id(void);

#endif // BASIC_H
