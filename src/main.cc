// Main entry point for Kode JavaScript Runtime
// Organized modular architecture with separate components

#include "core/runtime.h"
#include <iostream>

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
                std::cout << "Modular architecture: Core + Parser + FileSystem + Examples" << std::endl;
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
        runtime.RunEventLoop();
    }
    
    // Clean shutdown
    runtime.Shutdown();
    return success ? 0 : 1;
}
