#include <iostream>
#include <string>
#include <map>
#include <functional>
#include <fstream>
#include <uv.h>
#include "fs.h"     // Our file system module
#include "parser.h" // Our JavaScript parser

// Forward declaration - this tells the compiler that KodeRuntime exists
// We need this because TimerData references KodeRuntime before it's defined
class KodeRuntime;

// Timer callback structure - this holds data for our setTimeout implementation
struct TimerData {
    std::string message;    // What to print when timer fires
    KodeRuntime* runtime;   // Pointer back to our runtime (for future use)
};

// Our main JavaScript-like runtime class
// This is similar to how Node.js works internally - it combines:
// 1. An event loop (libuv) for async operations
// 2. Built-in functions (like console.log, setTimeout)
// 3. A simple command parser (instead of V8's full JS parser)
class KodeRuntime {
private:
    uv_loop_t* loop;    // libuv event loop - handles all async operations
    std::map<std::string, std::function<void()>> builtins;  // Built-in functions
    
public:
    KodeRuntime() {
        // Get the default libuv event loop
        // This is the heart of Node.js - it handles timers, I/O, etc.
        loop = uv_default_loop();
        setupBuiltins();
    }
    
    void setupBuiltins() {
        // In a real runtime, these would be more complex
        // For now, we're just learning the concepts
        builtins["console.log"] = []() {
            std::cout << "Console.log called!" << std::endl;
        };
    }
    
    bool Initialize() {
        std::cout << "=== Kode JavaScript Runtime ===" << std::endl;
        std::cout << "Learning Node.js runtime with libuv event loop" << std::endl;
        std::cout << std::endl;
        
        // Initialize the file system module (silently)
        KodeFS::Initialize(loop);
        
        return true;
    }
    
    void Shutdown() {
        if (loop) {
            uv_loop_close(loop);
        }
        std::cout << "Kode Runtime shutdown complete." << std::endl;
    }
    
    // Timer callback function - called when setTimeout timer expires
    // This is a static function because libuv is a C library and needs C-style callbacks
    static void onTimer(uv_timer_t* timer) {
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
    void setTimeout(const std::string& message, int delay_ms) {
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
    bool ExecuteString(const std::string& source, const std::string& filename = "script") {
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
    bool ExecuteStatement(const KodeParser::Statement& stmt) {
        switch (stmt.type) {
            case KodeParser::Statement::CONSOLE_LOG:
                std::cout << stmt.content << std::endl;
                return true;
                
            case KodeParser::Statement::SET_TIMEOUT:
                setTimeout("Timeout callback executed!", 1000);
                return true;
                
            case KodeParser::Statement::FS_READ_FILE:
                KodeFS::ReadFile(stmt.content, [](const std::string& error, const std::string& data) {
                    if (!error.empty()) {
                        std::cout << "Error: " << error << std::endl;
                    } else {
                        std::cout << data << std::endl;
                    }
                }, this);
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
                    std::string content = "Hello from Kode Runtime!";
                    if (stmt.options.find("content") != stmt.options.end()) {
                        content = stmt.options.at("content");
                    }
                    
                    KodeFS::WriteFile(stmt.content, content, [](const std::string& error) {
                        if (!error.empty()) {
                            std::cout << "Error: " << error << std::endl;
                        } else {
                            std::cout << "File written successfully" << std::endl;
                        }
                    }, this);
                }
                return true;
                
            case KodeParser::Statement::REQUIRE:
                std::cout << "[Runtime] Loading module: " << stmt.content << std::endl;
                return true;
                
            case KodeParser::Statement::VARIABLE_DECLARATION:
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
    bool ExecuteFile(const std::string& filename) {
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
    // It processes timers, I/O operations, etc.
    void RunEventLoop() {
        std::cout << "Starting event loop..." << std::endl;
        // UV_RUN_DEFAULT means run until there are no more active handles
        uv_run(loop, UV_RUN_DEFAULT);
    }
};

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options] [script.js]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help     Show this help message" << std::endl;
    std::cout << "  -v, --version  Show version information" << std::endl;
    std::cout << "  -e <code>      Execute JavaScript code directly" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " script.js" << std::endl;
    std::cout << "  " << program_name << " -e \"console.log('Hello World')\"" << std::endl;
}

// Main function - entry point of our runtime
// This is similar to how node.js starts up
int main(int argc, char* argv[]) {
    // Create our runtime instance
    KodeRuntime runtime;
    
    // Initialize the runtime (setup event loop, built-ins, etc.)
    if (!runtime.Initialize()) {
        std::cerr << "Failed to initialize runtime" << std::endl;
        return 1;
    }
    
    bool success = true;
    bool hasAsyncOperations = false;  // Track if we need to run event loop
    
    if (argc == 1) {
        // No arguments - show demo mode
        PrintUsage(argv[0]);
        std::cout << std::endl;
        std::cout << "=== Demo Mode - Learning How Node.js Works ===" << std::endl;
        
        // Demonstrate different features
        std::cout << "\n1. Synchronous operations:" << std::endl;
        runtime.ExecuteString("console.log('Hello from Kode!')");
        runtime.ExecuteString("1 + 1");
        
        std::cout << "\n2. Asynchronous operations (setTimeout):" << std::endl;
        runtime.ExecuteString("setTimeout()");
        hasAsyncOperations = true;
        
        std::cout << "\n3. File system operations:" << std::endl;
        std::cout << "   a) Synchronous file read (blocks):" << std::endl;
        runtime.ExecuteString("fs.readFileSync()");
        
        std::cout << "\n   b) Asynchronous file read (non-blocking):" << std::endl;
        runtime.ExecuteString("fs.readFile()");
        hasAsyncOperations = true;
        
        std::cout << "\n   c) Asynchronous file write:" << std::endl;
        runtime.ExecuteString("fs.writeFile()");
        hasAsyncOperations = true;
        
    } else {
        // Process command line arguments
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            
            if (arg == "-h" || arg == "--help") {
                PrintUsage(argv[0]);
                break;
            } 
            else if (arg == "-v" || arg == "--version") {
                std::cout << "Kode Runtime v0.1.0" << std::endl;
                std::cout << "Learning JavaScript runtime built with libuv" << std::endl;
                break;
            } 
            else if (arg == "-e" && i + 1 < argc) {
                // Execute code directly from command line
                std::string code = argv[++i];
                success = runtime.ExecuteString(code, "command-line");
                
                // Check if we need to run event loop for async operations
                if (code.find("setTimeout") != std::string::npos || 
                    code.find("fs.readFile") != std::string::npos || 
                    code.find("fs.writeFile") != std::string::npos) {
                    hasAsyncOperations = true;
                }
            } 
            else if (arg[0] != '-') {
                // Execute a JavaScript file
                success = runtime.ExecuteFile(arg);
                // Files might contain async operations, so run event loop
                hasAsyncOperations = true;
            } 
            else {
                std::cerr << "Unknown option: " << arg << std::endl;
                PrintUsage(argv[0]);
                success = false;
                break;
            }
        }
    }
    
    // Run event loop if we have async operations
    // This is crucial - without this, setTimeout and other async operations won't work
    if (hasAsyncOperations) {
        std::cout << "\n=== Running Event Loop (this is what keeps Node.js alive) ===" << std::endl;
        runtime.RunEventLoop();
    }
    
    // Clean shutdown
    runtime.Shutdown();
    return success ? 0 : 1;
}
