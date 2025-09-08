#include "basic.h"
#include "http.h"
#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

HttpResponse http_listen_callback(HttpRequest* request) {
  JsonValue* json = json_new_object();
  json_object_set(json, sv_new("request_id"), json_new_string(request->request_id));
  json_object_set(json, sv_new("method"), json_new_string(request->method));
  json_object_set(json, sv_new("path"), json_new_string(request->path));
  json_object_set(json, sv_new("status_code"), json_new_number(200));
  
  return http_json_response(200, request, json);
}

int main(int argc, char** argv) {
  try(config_load("config.json"));
  
  HttpServer server = {0};
  HttpServerInitOptions options = http_server_init_defaults();
  options.port = config_get_int(sv_new("server.port"), 8080); 

  try(http_server_init_opts(&server, options));
  fprintf(stderr, "INFO: Server initialized\n");
  
  try(http_server_listen(&server, http_listen_callback));
  
  http_server_free(&server);
  config_free();

  return 0;
}
