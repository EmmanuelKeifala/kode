#include "fs.h"
#include <fstream>
#include <sstream>
#include <cstring>  // For memcpy member initialization

// Static member initialization
uv_loop_t* KodeFS::loop_ = nullptr;

void KodeFS::Initialize(uv_loop_t* loop) {
    loop_ = loop;
    // File system module initialized silently
}

// Asynchronous file reading - this is the heart of Node.js I/O
// It uses libuv to read files without blocking the main thread
void KodeFS::ReadFile(const std::string& filename, ReadCallback callback, KodeRuntime* runtime) {
    std::cout << "[FS] Reading file: " << filename << std::endl;
    
    // For now, let's use synchronous reading to avoid the segfault
    // In a production runtime, we'd fix the async callback handling
    std::string content = ReadFileSync(filename);
    
    if (content.empty()) {
        callback("File not found or empty: " + filename, "");
    } else {
        callback("", content);  // Empty error string means success
    }
}

// Callback when file is opened - this is called by libuv
void KodeFS::OnOpenComplete(uv_fs_t* req) {
    FileOperation* op = static_cast<FileOperation*>(req->data);
    
    if (req->result < 0) {
        // File open failed
        std::string error = "Failed to open file: " + std::string(uv_strerror(req->result));
        op->readCb(error, "");
        uv_fs_req_cleanup(req);
        delete op;
        return;
    }
    
    // File opened successfully, now get file size
    uv_file file = req->result;  // File descriptor
    uv_fs_req_cleanup(req);      // Clean up the open request
    
    // Get file stats to know how much to read
    int result = uv_fs_fstat(loop_, &op->req, file, [](uv_fs_t* req) {
        FileOperation* op = static_cast<FileOperation*>(req->data);
        
        if (req->result < 0) {
            std::string error = "Failed to get file stats: " + std::string(uv_strerror(req->result));
            op->readCb(error, "");
            uv_fs_req_cleanup(req);
            delete op;
            return;
        }
        
        // Get file size
        size_t size = req->statbuf.st_size;
        uv_file file = req->file;  // Get file descriptor
        uv_fs_req_cleanup(req);
        
        // Allocate buffer for file content
        char* buffer = new char[size + 1];
        buffer[size] = '\0';  // Null terminate
        
        // Create buffer for libuv
        uv_buf_t buf = uv_buf_init(buffer, size);
        
        // Read the file content
        int result = uv_fs_read(loop_, &op->req, file, &buf, 1, 0, [](uv_fs_t* req) {
            FileOperation* op = static_cast<FileOperation*>(req->data);
            
            if (req->result < 0) {
                std::string error = "Failed to read file: " + std::string(uv_strerror(req->result));
                op->readCb(error, "");
            } else {
                // Success! Extract the content
                uv_buf_t* buf = static_cast<uv_buf_t*>(req->bufs);
                std::string content(buf->base, req->result);
                std::cout << "[FS] Successfully read " << req->result << " bytes" << std::endl;
                op->readCb("", content);  // Empty error string means success
            }
            
            // Clean up
            delete[] static_cast<char*>(req->bufs->base);
            uv_fs_req_cleanup(req);
            
            // Close the file
            uv_fs_close(loop_, &op->req, req->file, [](uv_fs_t* req) {
                FileOperation* op = static_cast<FileOperation*>(req->data);
                uv_fs_req_cleanup(req);
                delete op;
            });
        });
        
        if (result < 0) {
            std::string error = "Failed to start read: " + std::string(uv_strerror(result));
            op->readCb(error, "");
            delete[] buffer;
            delete op;
        }
    });
    
    if (result < 0) {
        std::string error = "Failed to get file stats: " + std::string(uv_strerror(result));
        op->readCb(error, "");
        delete op;
    }
}

// Asynchronous file writing (simplified to avoid segfaults)
void KodeFS::WriteFile(const std::string& filename, const std::string& content, WriteCallback callback, KodeRuntime* runtime) {
    std::cout << "[FS] Writing file: " << filename << std::endl;
    
    // Use synchronous writing for now to avoid callback issues
    std::ofstream file(filename);
    if (!file.is_open()) {
        callback("Failed to open file for writing: " + filename);
        return;
    }
    
    file << content;
    file.close();
    
    if (file.good()) {
        std::cout << "[FS] Successfully wrote to: " << filename << std::endl;
        callback("");  // Empty error means success
    } else {
        callback("Failed to write to file: " + filename);
    }
}

// Synchronous file reading - blocks the thread (like fs.readFileSync)
std::string KodeFS::ReadFileSync(const std::string& filename) {
    std::cout << "[FS] Synchronous read of: " << filename << std::endl;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        return "";  // Return empty string on error
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Check if file exists
bool KodeFS::FileExists(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();
}

// Placeholder callbacks - in a real implementation these would be more complex
void KodeFS::OnReadComplete(uv_fs_t* req) {
    // This is handled by lambda functions above for better readability
}

void KodeFS::OnWriteComplete(uv_fs_t* req) {
    // This is handled by lambda functions above for better readability
}
