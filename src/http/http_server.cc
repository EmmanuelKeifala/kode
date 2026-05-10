#include "http_server.h"
#include <cstring>
#include <string>

namespace {
struct WriteReq {
    uv_write_t req;
    uv_buf_t buf;
    uv_tcp_t* client;
};
}

HttpServer::HttpServer(uv_loop_t* loop) : loop_(loop) {
    uv_tcp_init(loop_, &server_);
    server_.data = this;
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start(uint16_t port) {
    if (running_) return true;

    struct sockaddr_in addr{};
    if (uv_ip4_addr("0.0.0.0", port, &addr) != 0) return false;

    if (uv_tcp_bind(&server_, reinterpret_cast<const struct sockaddr*>(&addr), 0) != 0) return false;

    int r = uv_listen(reinterpret_cast<uv_stream_t*>(&server_), 128, HttpServer::on_new_connection);
    if (r != 0) return false;

    running_ = true;
    return true;
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;
    uv_close(reinterpret_cast<uv_handle_t*>(&server_), nullptr);
}

void HttpServer::on_new_connection(uv_stream_t* server, int status) {
    if (status < 0) {
        return;
    }

    auto* self = reinterpret_cast<HttpServer*>(server->data);
    auto* conn = new Connection();
    conn->server = self;
    uv_tcp_init(self->loop_, &conn->client);
    conn->client.data = conn;

    if (uv_accept(server, reinterpret_cast<uv_stream_t*>(&conn->client)) == 0) {
        uv_read_start(reinterpret_cast<uv_stream_t*>(&conn->client), HttpServer::alloc_cb, HttpServer::read_cb);
    } else {
        uv_close(reinterpret_cast<uv_handle_t*>(&conn->client), HttpServer::on_client_closed);
    }
}

void HttpServer::alloc_cb(uv_handle_t* /*handle*/, size_t suggested_size, uv_buf_t* buf) {
    char* base = new char[suggested_size];
    *buf = uv_buf_init(base, static_cast<unsigned int>(suggested_size));
}

void HttpServer::read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    auto* conn = reinterpret_cast<Connection*>(stream->data);
    if (nread > 0) {
        conn->buffer.append(buf->base, buf->base + nread);
        // Detect end of headers
        if (!conn->responded && conn->buffer.find("\r\n\r\n") != std::string::npos) {
            conn->server->handle_request(conn);
        }
    }

    if (buf && buf->base) {
        delete[] buf->base;
    }

    if (nread == UV_EOF) {
        uv_close(reinterpret_cast<uv_handle_t*>(stream), HttpServer::on_client_closed);
    } else if (nread < 0) {
        uv_close(reinterpret_cast<uv_handle_t*>(stream), HttpServer::on_client_closed);
    }
}

void HttpServer::handle_request(Connection* conn) {
    conn->responded = true;
    // Very basic parsing: METHOD PATH HTTP/1.1\r\n...
    std::string method = "GET";
    std::string path = "/";

    size_t sp1 = conn->buffer.find(' ');
    if (sp1 != std::string::npos) {
        method = conn->buffer.substr(0, sp1);
        size_t sp2 = conn->buffer.find(' ', sp1 + 1);
        if (sp2 != std::string::npos) {
            path = conn->buffer.substr(sp1 + 1, sp2 - sp1 - 1);
        }
    }

    Response resp;
    // Route lookup first
    auto mit = routes_.find(method);
    if (mit != routes_.end()) {
        auto pit = mit->second.find(path);
        if (pit != mit->second.end()) {
            resp = pit->second(method, path, conn->buffer);
        }
    }
    // Fallback handler or default body
    if (resp.body.empty()) {
        if (handler_) {
            resp = handler_(method, path, conn->buffer);
        } else {
            resp.status = 200;
            resp.content_type = "text/plain";
            resp.body = "Hello from Kode HTTP";
        }
    }

    std::string status_text = "OK";
    if (resp.status == 404) status_text = "Not Found";
    else if (resp.status == 500) status_text = "Internal Server Error";

    std::string response;
    response.reserve(256 + resp.body.size());
    response.append("HTTP/1.1 ");
    response.append(std::to_string(resp.status));
    response.append(" ");
    response.append(status_text);
    response.append("\r\n");
    response.append("Connection: close\r\n");
    response.append("Content-Type: ");
    response.append(resp.content_type);
    response.append("\r\n");
    for (const auto& kv : resp.headers) {
        response.append(kv.first);
        response.append(": ");
        response.append(kv.second);
        response.append("\r\n");
    }
    response.append("Content-Length: ");
    response.append(std::to_string(resp.body.size()));
    response.append("\r\n\r\n");
    response.append(resp.body);

    // Prepare write request
    auto* wr = new WriteReq();
    wr->client = &conn->client;
    // Keep response storage alive until write completes by copying to heap
    char* heapBuf = new char[response.size()];
    std::memcpy(heapBuf, response.data(), response.size());
    wr->buf = uv_buf_init(heapBuf, static_cast<unsigned int>(response.size()));

    uv_write(&wr->req, reinterpret_cast<uv_stream_t*>(&conn->client), &wr->buf, 1, HttpServer::after_write_cb);
}

void HttpServer::after_write_cb(uv_write_t* req, int /*status*/) {
    auto* wr = reinterpret_cast<WriteReq*>(req);
    // Free write buffer
    delete[] wr->buf.base;
    // Close client
    uv_close(reinterpret_cast<uv_handle_t*>(wr->client), HttpServer::on_client_closed);
    delete wr;
}

void HttpServer::on_client_closed(uv_handle_t* handle) {
    auto* conn = reinterpret_cast<Connection*>(handle->data);
    delete conn;
}

void HttpServer::add_route(const std::string& method, const std::string& path, Handler h) {
    routes_[method][path] = std::move(h);
}
