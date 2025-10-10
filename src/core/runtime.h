#pragma once

#include <iostream>
#include <string>
#include <map>
#include <functional>
#include <fstream>
#include <uv.h>

// Include our modular components
#include "../filesystem/fs.h"
#include "../filesystem/modern_fs.h"
#include "../parser/parser.h"
#include "../concurrency/task.h"

// Forward declarations
class KodeRuntime;

// Timer callback structure for setTimeout implementation
struct TimerData {
    std::string message;
    KodeRuntime* runtime;
};

// Main Kode JavaScript Runtime
// This is the core engine that ties together all components:
// - Parser (for JavaScript syntax analysis)
// - File System (legacy and modern APIs)
// - Event Loop (libuv integration)
// - Built-in functions (console, timers, etc.)
class KodeRuntime {
private:
    uv_loop_t* loop;
    std::map<std::string, std::function<void()>> builtins;
    std::unique_ptr<ConcurrencyRuntime> concurrency_runtime;
    
public:
    KodeRuntime();
    
    // Core runtime lifecycle
    bool Initialize();
    void Shutdown();
    
    // Built-in function setup
    void setupBuiltins();
    
    // Timer functionality (setTimeout/setInterval)
    static void onTimer(uv_timer_t* timer);
    void setTimeout(const std::string& message, int delay_ms);
    
    // JavaScript execution engine
    bool ExecuteString(const std::string& source, const std::string& filename = "script");
    bool ExecuteStatement(const KodeParser::Statement& stmt);
    bool ExecuteFile(const std::string& filename);
    
    // Event loop management
    void RunEventLoop();
    
    // Concurrency support (Go-style)
    Task::TaskId spawnTask(const std::string& js_code);
    void yieldTask();
    void waitAllTasks();
    
    // Utility methods
    void PrintUsage(const char* program_name);
};
