#include "runtime.h"

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
