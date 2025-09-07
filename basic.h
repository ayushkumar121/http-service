#ifndef BASIC_H
#define BASIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MEM_REALLOC realloc
#define MEM_FREE(ptr) free(ptr)

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
          ((array)->capacity == 0) ? ARRAY_INIT_CAP : (array)->capacity * 2;   \
      (array)->items = MEM_REALLOC(                                            \
          (array)->items, (array)->capacity * sizeof(*(array)->items));        \
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
      MEM_FREE((array)->items);                                                \
  } while (0)

// Temp Allocator
#define TEMP_BUFFER_CAP 4 * 1024
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
void sb_free(StringBuilder *sb);

void sb_push_str(StringBuilder *sb, char *str);
void sb_push_sv(StringBuilder *sb, String sv);
void sb_push_char(StringBuilder *sb, char ch);
void sb_push_long(StringBuilder *sb, long l);
void sb_push_double(StringBuilder *sb, double d);
String sb_to_sv(StringBuilder *sb);
StringBuilder sb_clone(StringBuilder *sb);

#define SV_Fmt "%.*s"
#define SV_Arg(sv) (int)(sv).length, (sv).items

String sv_new(char *str);
String sv_new2(char *str, size_t len);
bool sv_equal(String s1, String s2);
String sv_trim_left(String sv);
String sv_trim_right(String sv);
String sv_trim(String sv);

typedef PAIR(String, String) StringPair;

StringPair sv_split_delim(String sv, char delim);
StringPair sv_split_str(String sv, char *str);
String sv_clone(String sv); // Clones the string on the heap

String tprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
String tvprintf(const char *format, va_list);

// Hash Table

typedef struct {
  void *key;
  void *value;
} HashTableEntry;

typedef bool (*KeyEqFunc)(void *a, void *b);
typedef size_t (*KeyHashFunc)(int capacity, void *a);

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
bool hash_table_get(HashTable *v, void *key, void **out);
bool hash_table_remove(HashTable *v, void *key, void **out);
void hash_table_free(HashTable *v);

// Error Handling
typedef struct {
  String message;
} Error;

#define MAX_ERROR_LENGTH 500
#define ErrorNil (Error){0}

Error error(char* message);
Error error_sv(String message);
Error errorf(const char *format, ...) __attribute__((format(printf, 1, 2)));
bool has_error(Error err);

void _try(Error err, char* file, int line);
#define try(err) _try(err, __FILE__, __LINE__)

// JSON Encoding & Decoding

#define JsonErrorEOF (Error){sv_new("json eof")}
#define JsonErrorUnexpectedToken (Error){sv_new("json unexpected token")}

typedef enum {
  JSON_NULL,
  JSON_TRUE,
  JSON_FALSE,
  JSON_NUMBER,
  JSON_STRING,
  JSON_OBJECT,
  JSON_ARRAY
} JsonType;

typedef struct JsonObjectEntry JsonObjectEntry;
typedef struct JsonValue JsonValue;
typedef double JsonNumber;
typedef String JsonString;
typedef ARRAY(JsonValue *) JsonArray;
typedef ARRAY(JsonObjectEntry) JsonObject;

typedef struct JsonValue {
  JsonType type;
  union {
    JsonNumber number;
    JsonString string;
    JsonArray array;
    JsonObject object;
  } value;
} JsonValue;

typedef struct JsonObjectEntry {
  JsonValue *key;
  JsonValue *value;
} JsonObjectEntry;

JsonValue* json_new_null(void);
JsonValue* json_new_bool(bool b);
JsonValue* json_new_number(double n);
JsonValue* json_new_string(String s);
JsonValue* json_new_cstring(char* s);
JsonValue* json_new_array(void);
JsonValue* json_new_object(void);

Error json_decode(String sv, JsonValue **out);
JsonNumber json_get_number(JsonValue *json);
JsonString json_get_string(JsonValue *json);

JsonValue *json_object_get(JsonValue *json, const char *key);
void json_object_set(JsonValue *json, const char *key, JsonValue *val);
bool json_object_remove(JsonValue *json, const char *key);

JsonValue *json_array_get(JsonValue *json, int index);
void json_array_append(JsonValue *json, JsonValue *val);
void json_array_remove(JsonValue *json, size_t index);

void json_encode(JsonValue json, StringBuilder *sb, int pp);
void json_print(FILE *file, JsonValue json, int pp);
void json_free(JsonValue *json);

// HTTP Server
typedef struct {
  int sockfd;
  struct sockaddr_in addr;
} HttpServer;

typedef struct {
  int id;
  String method;
  String path;
  String body;
  HashTable headers;

  // This is the raw request string
  // Needs to be freed after use
  String raw_request;
} HttpRequest;

typedef struct {
  int status_code;
  HashTable headers; // String to String hashtable
  String body;
  bool free_body_after_use; // Will call MEM_FREE on body after use
  bool keep_alive; // Whether to keep the connection alive
} HttpResponse;

typedef HttpResponse (*HttpListenCallback)(HttpRequest*);

#define HTTP_DEFAULT_PORT 8000
#define HTTP_BACKLOG 1024
#define HTTP_HEADER_CAPACITY 20

HashTable http_headers_init(void);
bool http_headers_set(HashTable *headers, String key, String value);
String* http_headers_get(HashTable *headers, String key);
void http_headers_free(HashTable *headers);

typedef struct {
  int port;
  int backlog;
  int header_capacity;
} HttpServerInitOptions;

Error http_server_init(HttpServer *server);
Error http_server_init_opts(HttpServer *server, HttpServerInitOptions options);
Error http_server_listen(HttpServer *server, HttpListenCallback callback);
void http_server_free(HttpServer *server);

HttpResponse http_response_init(int status_code);

#endif // BASIC_H
