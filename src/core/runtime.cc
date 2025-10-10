#include "runtime.h"
#include "../http/http_server.h"
#include <chrono>

// Implementation of KodeRuntime class

KodeRuntime::KodeRuntime() {
    // Get the default libuv event loop
    // This is the heart of Node.js - it handles timers, I/O, etc.
    loop = uv_default_loop();
    setupBuiltins();
    
    // Initialize concurrency runtime
    concurrency_runtime = std::make_unique<ConcurrencyRuntime>();
}

void KodeRuntime::setupBuiltins() {
    // In a real runtime, these would be more complex
    // For now, we're just learning the concepts
    builtins["console.log"] = []() {
        std::cout << "Console.log called!" << std::endl;
};
}

bool KodeRuntime::Initialize() {
        std::cout << "=== Kode JavaScript Runtime ===" << std::endl;
        std::cout << "Next-generation Node.js with Go-style concurrency" << std::endl;
        std::cout << std::endl;
        
        // Initialize file system modules
        KodeFS::Initialize(loop);      // Legacy FS
        ModernFS::Initialize(loop);    // Modern FS
        
        // Initialize concurrency runtime
        if (!concurrency_runtime->initialize()) {
            std::cerr << "Failed to initialize concurrency runtime" << std::endl;
            return false;
        }
        
        return true;
}

void KodeRuntime::Shutdown() {
        // Shutdown concurrency runtime first
        if (concurrency_runtime) {
            concurrency_runtime->shutdown();
        }
        if (loop) {
            uv_loop_close(loop);
        }
        std::cout << "Kode Runtime shutdown complete." << std::endl;
}

// Timer callback function - called when setTimeout timer expires
// This is a static function because libuv is a C library and needs C-style callbacks
void KodeRuntime::onTimer(uv_timer_t* timer) {
        // Extract our data from the timer
        TimerData* data = static_cast<TimerData*>(timer->data);
        std::cout << "Timer fired: " << data->message << std::endl;
        
        // Clean up the timer (important to prevent memory leaks)
        uv_timer_stop(timer);
        uv_close(reinterpret_cast<uv_handle_t*>(timer), [](uv_handle_t* handle) {
            delete reinterpret_cast<uv_timer_t*>(handle);
        });
        delete data;
}

// setTimeout implementation - this is how Node.js implements setTimeout internally
void KodeRuntime::setTimeout(const std::string& message, int delay_ms) {
        // Create a new libuv timer
        uv_timer_t* timer = new uv_timer_t;
        TimerData* data = new TimerData{message, this};
        
        // Initialize the timer with our event loop
        uv_timer_init(loop, timer);
        timer->data = data;  // Attach our data to the timer
        
        // Start the timer - it will call onTimer after delay_ms milliseconds
        uv_timer_start(timer, onTimer, delay_ms, 0);  // 0 = don't repeat
        
        std::cout << "Timer set for " << delay_ms << "ms: " << message << std::endl;
}
    
    // JavaScript executor using our parser
    // This parses JavaScript-like syntax and executes it
bool KodeRuntime::ExecuteString(const std::string& source, const std::string& filename) {
        // Parse the JavaScript code into statements
        std::vector<KodeParser::Statement> statements = KodeParser::Parse(source);
        
        if (statements.empty()) {
            std::cout << "[Runtime] No executable statements found" << std::endl;
            return true;
        }
        
        std::cout << "[Runtime] Executing " << statements.size() << " statement(s) from " << filename << std::endl;
        
        // Execute each statement
        bool success = true;
        for (const auto& stmt : statements) {
            if (!ExecuteStatement(stmt)) {
                success = false;
            }
        }
        
        return success;
}
    
    // Execute a single parsed statement
bool KodeRuntime::ExecuteStatement(const KodeParser::Statement& stmt) {
        switch (stmt.type) {
            case KodeParser::Statement::CONSOLE_LOG:
                std::cout << stmt.content << std::endl;
                return true;
                
            case KodeParser::Statement::SET_TIMEOUT:
                setTimeout("Timeout callback executed!", 1000);
                return true;
                
            case KodeParser::Statement::FS_READ_FILE:
                // Use modern FS with structured results
                ModernFS::readFile(stmt.content, [](const ModernFS::ReadResult& result) {
                    if (!result.success) {
                        std::cout << "Error: " << result.error << std::endl;
                    } else {
                        std::cout << result.content << std::endl;
                        std::cout << "[FileInfo] " << result.info.toJSON() << std::endl;
                    }
                });
                return true;
                
            case KodeParser::Statement::FS_READ_FILE_SYNC:
                {
                    std::string content = KodeFS::ReadFileSync(stmt.content);
                    if (!content.empty()) {
                        std::cout << content << std::endl;
                    } else {
                        std::cout << "Error: Could not read file " << stmt.content << std::endl;
                    }
                }
                return true;
                
            case KodeParser::Statement::FS_WRITE_FILE:
                {
                    std::string content = "Hello from Kode Runtime with Modern FS!";
                    if (stmt.options.find("content") != stmt.options.end()) {
                        content = stmt.options.at("content");
                    }
                    
                    // Use modern FS with structured results
                    ModernFS::writeFile(stmt.content, content, [](const ModernFS::WriteResult& result) {
                        if (!result.success) {
                            std::cout << "Error: " << result.error << std::endl;
                        } else {
                            std::cout << "Successfully wrote " << result.bytesWritten << " bytes" << std::endl;
                            std::cout << "[FileInfo] " << result.info.toJSON() << std::endl;
                        }
                    });
                }
                return true;

            // Kode concurrency API routing
            case KodeParser::Statement::KODE_SPAWN:
                if (!stmt.content.empty()) {
                    spawnTask(stmt.content);
                }
                return true;
            case KodeParser::Statement::CH_CREATE:
                {
                    std::string name;
                    size_t capacity = 0;
                    auto itn = stmt.options.find("name");
                    if (itn != stmt.options.end()) name = itn->second;
                    auto itc = stmt.options.find("capacity");
                    if (itc != stmt.options.end()) {
                        try { capacity = static_cast<size_t>(std::stoll(itc->second)); } catch (...) { capacity = 0; }
                    }
                    if (!name.empty()) createChannel(name, capacity);
                }
                return true;
            case KodeParser::Statement::CH_SEND:
                {
                    std::string name, value;
                    auto itn = stmt.options.find("name");
                    auto itv = stmt.options.find("value");
                    if (itn != stmt.options.end()) name = itn->second;
                    if (itv != stmt.options.end()) value = itv->second;
                    if (!name.empty()) sendToChannel(name, value);
                }
                return true;
            case KodeParser::Statement::CH_RECV:
                {
                    std::string name;
                    auto itn = stmt.options.find("name");
                    if (itn != stmt.options.end()) name = itn->second;
                    if (!name.empty()) {
                        std::string val = receiveFromChannel(name);
                        if (!val.empty()) std::cout << "Received: " << val << std::endl;
                    }
                }
                return true;
            case KodeParser::Statement::YIELD_OP:
                yieldTask();
                return true;
            case KodeParser::Statement::WITH_TIMEOUT:
                {
                    int ms = 0;
                    auto it = stmt.options.find("timeout");
                    if (it != stmt.options.end()) {
                        try { ms = std::stoi(it->second); } catch (...) { ms = 0; }
                    }
                    spawnTaskWithTimeout(std::chrono::milliseconds(ms), stmt.content);
                }
                return true;

            // HTTP server controls
            case KodeParser::Statement::HTTP_START:
                {
                    int port = 0;
                    auto it = stmt.options.find("port");
                    if (it != stmt.options.end()) {
                        try { port = std::stoi(it->second); } catch (...) { port = 0; }
                    }
                    if (port <= 0) port = 3000; // default port
                    bool ok = httpStart(static_cast<uint16_t>(port));
                    if (!ok) {
                        std::cout << "Error: Failed to start HTTP server on port " << port << std::endl;
                    } else {
                        std::cout << "[HTTP] Listening on http://0.0.0.0:" << port << std::endl;
                    }
                }
                return true;
            case KodeParser::Statement::HTTP_STOP:
                httpStop();
                std::cout << "[HTTP] Server stopped" << std::endl;
                return true;
            case KodeParser::Statement::HTTP_ROUTE:
                {
                    std::string method, path, body, ctype = "text/plain";
                    auto itM = stmt.options.find("method");
                    auto itP = stmt.options.find("path");
                    auto itC = stmt.options.find("contentType");
                    if (itM != stmt.options.end()) method = itM->second;
                    if (itP != stmt.options.end()) path = itP->second;
                    if (itC != stmt.options.end()) ctype = itC->second;
                    body = stmt.content;
                    httpRoute(method, path, body, ctype);
                }
                return true;
                
            case KodeParser::Statement::REQUIRE:
                std::cout << "[Runtime] Loading module: " << stmt.content << std::endl;
                return true;
                
            case KodeParser::Statement::CONST_DECLARATION:
            case KodeParser::Statement::LET_DECLARATION:
            case KodeParser::Statement::VAR_DECLARATION:
                std::cout << "[Runtime] Variable: " << stmt.content << std::endl;
                return true;
                
            default:
                std::cout << "[Runtime] Unknown statement - available commands:" << std::endl;
                std::cout << "  console.log('message')" << std::endl;
                std::cout << "  setTimeout()" << std::endl;
                std::cout << "  fs.readFile('filename', callback)" << std::endl;
                std::cout << "  fs.writeFile('filename', 'content', callback)" << std::endl;
                std::cout << "  fs.readFileSync('filename')" << std::endl;
                return false;
        }
}
    
    // File execution - reads a file and executes it
bool KodeRuntime::ExecuteFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Cannot open file: " << filename << std::endl;
            return false;
        }
        
        // Read entire file into string
        std::string source((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();
        
        return ExecuteString(source, filename);
}

// Run the event loop - this is what keeps Node.js alive
void KodeRuntime::RunEventLoop() {
    std::cout << "Starting event loop..." << std::endl;
    // UV_RUN_DEFAULT means run until there are no more active handles
        uv_run(loop, UV_RUN_DEFAULT);
    std::cout << "Event loop finished." << std::endl;
}

// Go-style concurrency methods
Task::TaskId KodeRuntime::spawnTask(const std::string& js_code) {
    if (!concurrency_runtime) {
        throw std::runtime_error("Concurrency runtime not initialized");
    }
    
    return concurrency_runtime->kode([js_code, this]() {
        std::cout << "[ConcurrentTask] Executing: " << js_code << std::endl;
        // Execute the JavaScript code using our parser
        ExecuteString(js_code, "concurrent-task");
        std::cout << "[ConcurrentTask] Completed: " << js_code << std::endl;
    });
}

void KodeRuntime::yieldTask() {
    if (concurrency_runtime) {
        concurrency_runtime->yield();
    }
}

void KodeRuntime::waitAllTasks() {
    if (concurrency_runtime) {
        concurrency_runtime->join_all();
    }
}

// Kode concurrency helpers for parser
void KodeRuntime::createChannel(const std::string& name, size_t capacity) {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    channels_[name] = std::make_shared<Channel<std::string>>(capacity);
}

void KodeRuntime::sendToChannel(const std::string& name, const std::string& value) {
    std::shared_ptr<Channel<std::string>> ch;
    {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        auto it = channels_.find(name);
        if (it != channels_.end()) ch = it->second;
    }
    if (!ch) {
        std::cout << "Error: Channel '" << name << "' not found" << std::endl;
        return;
    }
    ch->send(value);
}

std::string KodeRuntime::receiveFromChannel(const std::string& name) {
    std::shared_ptr<Channel<std::string>> ch;
    {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        auto it = channels_.find(name);
        if (it != channels_.end()) ch = it->second;
    }
    if (!ch) {
        std::cout << "Error: Channel '" << name << "' not found" << std::endl;
        return "";
    }
    std::string value;
    ch->receive(value);
    return value;
}

void KodeRuntime::spawnTaskWithTimeout(std::chrono::milliseconds timeout, const std::string& js_code) {
    if (!concurrency_runtime) {
        throw std::runtime_error("Concurrency runtime not initialized");
    }
    concurrency_runtime->with_timeout(timeout, [this, js_code]() {
        ExecuteString(js_code, "withTimeout-task");
    });
}

bool KodeRuntime::httpStart(uint16_t port) {
    if (!http_server_) {
        http_server_ = std::make_unique<HttpServer>(loop);
    }
    return http_server_->start(port);
}

void KodeRuntime::httpStop() {
    if (http_server_) {
        http_server_->stop();
        http_server_.reset();
    }
}

void KodeRuntime::httpRoute(const std::string& method, const std::string& path,
                            const std::string& body, const std::string& contentType) {
    if (!http_server_) {
        http_server_ = std::make_unique<HttpServer>(loop);
        http_server_->start(3000);
    }
    http_server_->add_route(method, path, [body, contentType](const std::string& m,
                                                              const std::string& p,
                                                              const std::string& raw) -> HttpServer::Response {
        HttpServer::Response r;
        r.status = 200;
        r.content_type = contentType;
        r.body = body;
        (void)m; (void)p; (void)raw;
        return r;
    });
}
