#pragma once

#include <uv.h>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <unordered_map>

// Minimal HTTP server built on libuv
// - Listens on a TCP port
// - Parses a basic HTTP request line and headers (until \r\n\r\n)
// - Replies with a simple 200 OK text/plain body
// - Closes the connection after response

class HttpServer {
public:
    explicit HttpServer(uv_loop_t* loop);
    ~HttpServer();

    bool start(uint16_t port);
    void stop();

    // Response object returned by handler
    struct Response {
        int status = 200;
        std::string content_type = "text/plain";
        std::string body;
        std::vector<std::pair<std::string, std::string>> headers; // extra headers
    };

    // Request handler (method, path, raw_request) => Response
    using Handler = std::function<Response(const std::string&, const std::string&, const std::string&)>;
    void set_handler(Handler h) { handler_ = std::move(h); }
    void add_route(const std::string& method, const std::string& path, Handler h);

private:
    struct Connection {
        uv_tcp_t client;
        std::string buffer;
        HttpServer* server;
        bool responded = false;
        bool header_parsed = false;
        size_t header_end = 0; // index of \r\n\r\n end
        size_t expected_body_len = 0;
        std::unordered_map<std::string, std::string> headers;
    };

    static void on_new_connection(uv_stream_t* server, int status);
    static void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void after_write_cb(uv_write_t* req, int status);
    static void on_client_closed(uv_handle_t* handle);

    void handle_request(Connection* conn);

    uv_loop_t* loop_;
    uv_tcp_t server_{};
    bool running_ = false;
    Handler handler_{}; // fallback handler
    std::map<std::string, std::map<std::string, Handler>> routes_; // method -> path -> handler
};
