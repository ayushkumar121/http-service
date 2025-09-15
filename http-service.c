#include "basic.h"
#include "http.h"
#include "config.h"

HttpResponse http_listen_callback(HttpRequest* request) {
  JsonValue* json = json_new_object();
  json_object_set(json, sv_new("request_id"), json_new_string(request->request_id));
  json_object_set(json, sv_new("proto"), json_new_string(request->proto));
  json_object_set(json, sv_new("method"), json_new_string(request->method));
  json_object_set(json, sv_new("path"), json_new_string(request->path));
  json_object_set(json, sv_new("body"), json_new_string(request->body));

  JsonValue* headers = json_new_object();
  for (size_t i=0; i<request->headers.capacity; i++) {
    HashTableEntry entry = request->headers.entries[i];
    if (entry.key != NULL) {
      HeaderValues* values = (HeaderValues*) entry.value;
      JsonValue* header_values = json_new_array();
      for (size_t j = 0; j<values->length; j++) {
        json_array_append(header_values, json_new_string(values->items[j]));
      }
      json_object_set(headers, *(String*)entry.key, header_values);
    }
  }
  json_object_set(json, sv_new("headers"), headers);
  
  return http_json_response(200, request, json);
}

int main(int argc, char** argv) {
  try(config_load("config.json"));

  HttpServer server = {0};
  HttpServerInitOptions options = http_server_init_defaults();
  options.port = config_get_int(sv_new("server.port"), 8080); 

  try(http_server_init_opts(&server, options));
  try(http_server_listen(&server, http_listen_callback));
  return 0;
}
