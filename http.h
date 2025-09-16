#ifndef HTTP_H
#define HTTP_H

#include "basic.h"

#include <netinet/in.h>

// HTTP Server
typedef struct {
  int sock_fd;
  struct sockaddr_in addr;
} HttpServer;

typedef struct {
  String request_id; // Temp allocated
  String proto;
  String method;
  String path;
  String body;
  
  // Keys and Values are heap allocated strings which freed after use
  // headers are multi key map e.g. foo: [bar, buz]
  HashTable headers;
  String raw_request;
} HttpRequest;

typedef struct {
  int status_code;
  HashTable headers;
  String content_type;
  String body;
  bool free_body_after_use; // Will call MEM_FREE on body after use
  bool keep_alive;          // Whether to keep the connection alive
} HttpResponse;

typedef HttpResponse (*HttpListenCallback)(const HttpRequest *);

#define HTTP_DEFAULT_PORT 8000
#define HTTP_BACKLOG 1024
#define HTTP_HEADER_CAPACITY 20
#define HTTP_READ_BUFFER_SIZE 512

typedef ARRAY(String) HeaderValues;

HashTable http_headers_init(void);
void http_headers_set(HashTable *headers, String key, String value);
HeaderValues *http_headers_get(const HashTable *headers, String key);
void http_headers_free(HashTable *headers);

typedef struct {
  int port;
  int backlog;
  int header_capacity;
} HttpServerInitOptions;

Error http_server_init(HttpServer *server);
HttpServerInitOptions http_server_init_defaults(void);
Error http_server_init_opts(HttpServer *server, HttpServerInitOptions options);
Error http_server_listen(const HttpServer *server, HttpListenCallback callback);
void http_server_free(const HttpServer *server);

HttpResponse http_response_init(int status_code);
HttpResponse http_json_response(int status, JsonValue *json);
HttpResponse http_text_response(int status, String body);
HttpResponse http_status_response(int status);
#endif
