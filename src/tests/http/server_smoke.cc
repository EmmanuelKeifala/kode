#include "../../http/http_server.h"
#include <uv.h>
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    uint16_t port = 3000;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    } else if (const char* envp = std::getenv("PORT")) {
        port = static_cast<uint16_t>(std::atoi(envp));
    }

    uv_loop_t* loop = uv_default_loop();
    HttpServer server(loop);

    // Default handler echoes method and path
    server.set_handler([](const std::string& method, const std::string& path, const std::string&) -> HttpServer::Response {
        HttpServer::Response r;
        r.status = 200;
        r.content_type = "text/plain";
        r.body = method + " " + path + "\n";
        return r;
    });

    if (!server.start(port)) {
        std::cerr << "Failed to start HTTP server on port " << port << std::endl;
        return 1;
    }

    std::cout << "HTTP server listening on http://0.0.0.0:" << port << std::endl;
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
