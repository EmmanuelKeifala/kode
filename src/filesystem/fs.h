#pragma once
#include <uv.h>
#include <string>
#include <functional>
#include <iostream>

// Forward declaration
class KodeRuntime;

// File system operations for Kode runtime
// This is similar to how Node.js implements the 'fs' module internally
class KodeFS {
public:
    // Callback types for async operations
    using ReadCallback = std::function<void(const std::string& error, const std::string& data)>;
    using WriteCallback = std::function<void(const std::string& error)>;
    
    // Structure to hold data for async file operations
    struct FileOperation {
        uv_fs_t req;           // libuv file system request
        std::string filename;   // File path
        std::string content;    // File content (for writes)
        ReadCallback readCb;    // Read callback
        WriteCallback writeCb;  // Write callback
        KodeRuntime* runtime;   // Reference to runtime
    };
    
    // Initialize the file system module
    static void Initialize(uv_loop_t* loop);
    
    // Asynchronous file read - this is how fs.readFile() works in Node.js
    static void ReadFile(const std::string& filename, ReadCallback callback, KodeRuntime* runtime);
    
    // Asynchronous file write - this is how fs.writeFile() works in Node.js
    static void WriteFile(const std::string& filename, const std::string& content, WriteCallback callback, KodeRuntime* runtime);
    
    // Synchronous file read - this is how fs.readFileSync() works
    static std::string ReadFileSync(const std::string& filename);
    
    // Check if file exists
    static bool FileExists(const std::string& filename);

private:
    static uv_loop_t* loop_;
    
    // Callback functions for libuv - these are called when operations complete
    static void OnReadComplete(uv_fs_t* req);
    static void OnWriteComplete(uv_fs_t* req);
    static void OnOpenComplete(uv_fs_t* req);
};
