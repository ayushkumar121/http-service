#include "basic.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

HttpResponse json_response(int status, HttpRequest *request, JsonValue *json) {
  HttpResponse response = http_response_init(status);

  http_headers_set(&response.headers, sv_new("Request-Id"), tprintf("%d", request->id));
  http_headers_set(&response.headers, sv_new("Content-Type"), sv_new("application/json"));

  StringBuilder sb = {0};
  json_encode(*json, &sb, 0);
  response.body = sb_to_sv(&sb);
  response.free_body_after_use = true;
  json_free(json);

  return response;
}

HttpResponse http_listen_callback(HttpRequest* request) {
  JsonValue* json = json_new_object();
  json_object_set(json, "request_id", json_new_number(request->id));
  json_object_set(json, "method", json_new_string(request->method));
  json_object_set(json, "path", json_new_string(request->path));
  json_object_set(json, "status_code", json_new_number(200));
  
  return json_response(200, request, json);
}

int main(int argc, char** argv) {
  HttpServer server = {0};
  try(http_server_init_opts(&server, (HttpServerInitOptions){
    .port = 8000,
    .backlog = 1024, 
    .header_capacity = 20
  }));
  fprintf(stderr, "INFO: Server initialized\n");
  try(http_server_listen(&server, http_listen_callback));
  http_server_free(&server);
  return 0;
}
