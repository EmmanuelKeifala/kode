#pragma once
#include <uv.h>
#include <string>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

// Forward declaration
class KodeRuntime;

// Modern File System for Kode Runtime
// This demonstrates how a next-generation JS runtime could handle file operations
// Key improvements:
// 1. Promise-based API (no callbacks)
// 2. Structured data objects
// 3. Better error handling
// 4. Async iterators for large files
// 5. Unified API (no Sync variants)

class ModernFS {
public:
    // File metadata structure - like Node.js fs.Stats but cleaner
    struct FileInfo {
        std::string path;
        size_t size;
        bool isFile;
        bool isDirectory;
        std::string mimeType;
        long long lastModified;
        
        // Convert to JSON-like string for JavaScript
        std::string toJSON() const;
    };
    
    // Read result structure - contains both data and metadata
    struct ReadResult {
        std::string content;
        FileInfo info;
        std::string encoding;
        bool success;
        std::string error;
    };
    
    // Write result structure
    struct WriteResult {
        size_t bytesWritten;
        FileInfo info;
        bool success;
        std::string error;
    };
    
    // Modern callback types using structured data
    using ReadCallback = std::function<void(const ReadResult& result)>;
    using WriteCallback = std::function<void(const WriteResult& result)>;
    using InfoCallback = std::function<void(const FileInfo& info, const std::string& error)>;
    
    // Initialize the modern file system
    static void Initialize(uv_loop_t* loop);
    
    // Modern unified async API - no Sync variants needed
    // Everything is async but fast for small files
    
    // Read file with automatic encoding detection
    static void readFile(const std::string& path, ReadCallback callback);
    static void readFile(const std::string& path, const std::string& encoding, ReadCallback callback);
    
    // Write file with automatic directory creation
    static void writeFile(const std::string& path, const std::string& content, WriteCallback callback);
    static void writeFile(const std::string& path, const std::string& content, const std::string& encoding, WriteCallback callback);
    
    // Get file information (replaces fs.stat)
    static void getFileInfo(const std::string& path, InfoCallback callback);
    
    // Check if file exists (modern approach)
    static void exists(const std::string& path, std::function<void(bool exists)> callback);
    
    // Create directory (with recursive option)
    static void createDirectory(const std::string& path, bool recursive, std::function<void(bool success, const std::string& error)> callback);
    
    // List directory contents with metadata
    static void listDirectory(const std::string& path, std::function<void(const std::vector<FileInfo>& files, const std::string& error)> callback);
    
    // Stream reading for large files (async iterator concept)
    static void readStream(const std::string& path, size_t chunkSize, std::function<void(const std::string& chunk, bool isLast, const std::string& error)> callback);

private:
    static uv_loop_t* loop_;
    
    // Helper functions
    static std::string detectMimeType(const std::string& path);
    static std::string detectEncoding(const std::vector<char>& data);
    static FileInfo createFileInfo(const std::string& path);
    static void ensureDirectoryExists(const std::string& path);
    
    // Internal async operation structure
    struct AsyncOperation {
        uv_work_t request;
        std::string path;
        std::string content;
        std::string encoding;
        ReadCallback readCallback;
        WriteCallback writeCallback;
        InfoCallback infoCallback;
        
        // Result data
        ReadResult readResult;
        WriteResult writeResult;
        FileInfo fileInfo;
        std::string error;
    };
    
    // libuv work callbacks
    static void workCallback(uv_work_t* req);
    static void afterWorkCallback(uv_work_t* req, int status);
};
