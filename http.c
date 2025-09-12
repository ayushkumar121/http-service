#include "http.h"
#include "basic.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// For hashtable
bool header_key_eq(void *a, void *b) {
  String *sa = a;
  String *sb = b;
  return sv_equal_ignore_case(*sa, *sb);
}

// hash: https://theartincode.stanis.me/008-djb2/
size_t header_key_hash(size_t capacity, void *a) {
  String *s = a;
  size_t hash = 5381;
  for (size_t i = 0; i < s->length; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)(tolower(s->items[i]));
  }
  return hash % capacity;
}

HashTable http_headers_init(void) {
  return hash_table_init(HTTP_HEADER_CAPACITY, header_key_eq, header_key_hash);
}

void http_headers_set(HashTable *headers, String key, String value) {
  assert(headers != NULL);
  String *key_ptr = malloc(sizeof(String));
  *key_ptr = key;

  HeaderValues *out;
  if (hash_table_get(headers, key_ptr, (void **)&out)) {
    array_append(out, value);
  } else {
    HeaderValues *values = malloc(sizeof(HeaderValues));
    array_append(values, value);
    hash_table_set(headers, key_ptr, values);
  }
}

HeaderValues *http_headers_get(HashTable *headers, String key) {
  assert(headers != NULL);
  assert(key.length > 0);

  void *value = NULL;
  if (hash_table_get(headers, &key, &value)) {
    return (HeaderValues *)value;
  }
  return NULL;
}

void http_headers_free(HashTable *headers) {
  assert(headers != NULL);
  if (headers->entries != NULL) {
    for (size_t i = 0; i < headers->capacity; i++) {
      HashTableEntry entry = headers->entries[i];
      if (entry.key != NULL) {
        free(entry.key);
        free(entry.value);
      }
    }
  }
  hash_table_free(headers);
}

HttpServerInitOptions http_server_init_defaults(void) {
  return (HttpServerInitOptions){
      .port = HTTP_DEFAULT_PORT,
      .backlog = HTTP_BACKLOG,
      .header_capacity = HTTP_HEADER_CAPACITY,
  };
}
Error http_server_init(HttpServer *server) {
  return http_server_init_opts(server, http_server_init_defaults());
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
  default:
    return sv_new("unknown");
  }
}

HttpError http_parse_request(int client, StringBuilder *sb,
                             HttpRequest *request) {
  assert(request != NULL);

  ssize_t header_end;
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
  StringPair p3 = sv_split_delim(p2.second, ' ');  // (proto vs rest)
  if (p0.first.length == 0 || p1.first.length == 0 || p2.first.length == 0) {
    return HttpErrorParse;
  }

  request->request_id = random_id();
  request->proto = p3.first;
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
  case 204:
    return sv_new("No Content");
  case 301:
    return sv_new("Moved Permanently");
  case 400:
    return sv_new("Bad Request");
  case 404:
    return sv_new("Not Found");
  case 405:
    return sv_new("Method Not Allowed");
  case 500:
    return sv_new("Internal Server Error");
  default:
    return sv_new("Unknown");
  }
}

char *http_date(void) {
  time_t t;
  struct tm *tm;
  time(&t);
  tm = gmtime(&t);
  static char buf[40];
  strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tm);
  return buf;
}

void http_response_encode(HttpResponse *response, StringBuilder *sb) {
  sb_push_str(sb, "HTTP/1.1 ");
  sb_push_long(sb, response->status_code);
  sb_push_str(sb, " ");
  sb_push_sv(sb, http_status_code_to_string(response->status_code));
  sb_push_str(sb, CRLF);
  if (response->body.length > 0) {
    sb_push_str(sb, "Content-Type: ");
    sb_push_sv(sb, response->content_type);
    sb_push_str(sb, CRLF);
    sb_push_str(sb, "Content-Length: ");
    sb_push_long(sb, response->body.length);
    sb_push_str(sb, CRLF);
  }
  if (response->keep_alive) {
    sb_push_str(sb, "Connection: keep-alive");
    sb_push_str(sb, CRLF);
  } else {
    sb_push_str(sb, "Connection: close");
    sb_push_str(sb, CRLF);
  }
  sb_push_str(sb, "Date: ");
  sb_push_str(sb, http_date());
  sb_push_str(sb, CRLF);
  for (int i = 0; i < response->headers.capacity; i++) {
    HashTableEntry entry = response->headers.entries[i];
    if (entry.key != NULL) {
      sb_push_sv(sb, *(String *)entry.key);
      sb_push_str(sb, ": ");
      HeaderValues *values = (HeaderValues *)entry.value;
      if (values != NULL) {
        for (int j = 0; j < values->length; j++) {
          sb_push_sv(sb, values->items[j]);
          if (j < values->length-1) sb_push_char(sb, ',');
        }
      }
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
  free(arg);

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
      free(response.body.items);
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
    return errorf("listen failed: %s\n", strerror(errno));
  }

  while (true) {
    int clientfd = accept(server->sockfd, NULL, NULL);
    if (clientfd < 0) {
      fprintf(stderr, "ERROR: accept failed: %s\n", strerror(errno));
      continue;
    }

    ClientThreadArgs *arg = malloc(sizeof(ClientThreadArgs));
    arg->clientfd = clientfd;
    arg->callback = callback;

    pthread_t tid;
    if (pthread_create(&tid, NULL, handle_client, arg) < 0) {
      close(clientfd);
      fprintf(stderr, "ERROR: pthread create: %s\n", strerror(errno));
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

HttpResponse http_json_response(int status, HttpRequest *request,
                                JsonValue *json) {
  HttpResponse response = http_response_init(status);
  response.content_type = sv_new("application/json");

  StringBuilder sb = {0};
  json_encode(*json, &sb, 0);
  response.body = sb_to_sv(&sb);
  response.free_body_after_use = true;
  json_free(json);

  return response;
}

HttpResponse http_text_response(int status, HttpRequest *request, String body) {
  HttpResponse response = http_response_init(status);
  response.content_type = sv_new("text/plain");
  response.body = body;
  response.free_body_after_use = false;
  return response;
}
