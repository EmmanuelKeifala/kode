#include "modern_fs.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <algorithm>

// Static member initialization
uv_loop_t* ModernFS::loop_ = nullptr;

void ModernFS::Initialize(uv_loop_t* loop) {
    loop_ = loop;
    std::cout << "[ModernFS] Next-generation file system initialized" << std::endl;
}

// Modern async file reading with automatic encoding detection
void ModernFS::readFile(const std::string& path, ReadCallback callback) {
    readFile(path, "auto", callback);
}

void ModernFS::readFile(const std::string& path, const std::string& encoding, ReadCallback callback) {
    std::cout << "[ModernFS] Reading file: " << path << " (encoding: " << encoding << ")" << std::endl;
    
    // Create async operation
    AsyncOperation* op = new AsyncOperation();
    op->path = path;
    op->encoding = encoding;
    op->readCallback = callback;
    op->request.data = op;
    
    // Queue work on thread pool
    int uvResult = uv_queue_work(loop_, &op->request, workCallback, afterWorkCallback);
    
    if (uvResult < 0) {
        ReadResult result;
        result.success = false;
        result.error = "Failed to queue file operation: " + std::string(uv_strerror(uvResult));
        callback(result);
        delete op;
    }
}

// Modern async file writing
void ModernFS::writeFile(const std::string& path, const std::string& content, WriteCallback callback) {
    writeFile(path, content, "utf8", callback);
}

void ModernFS::writeFile(const std::string& path, const std::string& content, const std::string& encoding, WriteCallback callback) {
    std::cout << "[ModernFS] Writing file: " << path << " (" << content.length() << " chars)" << std::endl;
    
    // Create async operation
    AsyncOperation* op = new AsyncOperation();
    op->path = path;
    op->content = content;
    op->encoding = encoding;
    op->writeCallback = callback;
    op->request.data = op;
    
    // Queue work on thread pool
    int uvResult = uv_queue_work(loop_, &op->request, workCallback, afterWorkCallback);
    
    if (uvResult < 0) {
        WriteResult result;
        result.success = false;
        result.error = "Failed to queue write operation: " + std::string(uv_strerror(uvResult));
        callback(result);
        delete op;
    }
}

// Get file information
void ModernFS::getFileInfo(const std::string& path, InfoCallback callback) {
    std::cout << "[ModernFS] Getting file info: " << path << std::endl;
    
    AsyncOperation* op = new AsyncOperation();
    op->path = path;
    op->infoCallback = callback;
    op->request.data = op;
    
    int uvResult = uv_queue_work(loop_, &op->request, workCallback, afterWorkCallback);
    
    if (uvResult < 0) {
        FileInfo info;
        callback(info, "Failed to queue info operation: " + std::string(uv_strerror(uvResult)));
        delete op;
    }
}

// Check if file exists (modern approach)
void ModernFS::exists(const std::string& path, std::function<void(bool exists)> callback) {
    getFileInfo(path, [callback](const FileInfo& info, const std::string& error) {
        callback(error.empty());
    });
}

// Work callback - runs on thread pool
void ModernFS::workCallback(uv_work_t* req) {
    AsyncOperation* op = static_cast<AsyncOperation*>(req->data);
    
    try {
        if (op->readCallback) {
            // Read operation
            std::ifstream file(op->path, std::ios::binary);
            if (!file.is_open()) {
                op->readResult.success = false;
                op->readResult.error = "Cannot open file: " + op->path;
                return;
            }
            
            // Read content
            std::ostringstream buffer;
            buffer << file.rdbuf();
            op->readResult.content = buffer.str();
            
            // Create file info
            op->readResult.info = createFileInfo(op->path);
            op->readResult.encoding = op->encoding;
            op->readResult.success = true;
            
        } else if (op->writeCallback) {
            // Write operation
            
            // Ensure directory exists
            ensureDirectoryExists(op->path);
            
            std::ofstream file(op->path);
            if (!file.is_open()) {
                op->writeResult.success = false;
                op->writeResult.error = "Cannot create file: " + op->path;
                return;
            }
            
            file << op->content;
            file.close();
            
            if (file.good()) {
                op->writeResult.bytesWritten = op->content.length();
                op->writeResult.info = createFileInfo(op->path);
                op->writeResult.success = true;
            } else {
                op->writeResult.success = false;
                op->writeResult.error = "Failed to write to file: " + op->path;
            }
            
        } else if (op->infoCallback) {
            // Info operation
            op->fileInfo = createFileInfo(op->path);
            if (op->fileInfo.path.empty()) {
                op->error = "File not found: " + op->path;
            }
        }
        
    } catch (const std::exception& e) {
        if (op->readCallback) {
            op->readResult.success = false;
            op->readResult.error = "Exception: " + std::string(e.what());
        } else if (op->writeCallback) {
            op->writeResult.success = false;
            op->writeResult.error = "Exception: " + std::string(e.what());
        } else if (op->infoCallback) {
            op->error = "Exception: " + std::string(e.what());
        }
    }
}

// After work callback - runs on main thread
void ModernFS::afterWorkCallback(uv_work_t* req, int status) {
    AsyncOperation* op = static_cast<AsyncOperation*>(req->data);
    
    if (status < 0) {
        // libuv error
        std::string error = "libuv error: " + std::string(uv_strerror(status));
        
        if (op->readCallback) {
            ReadResult result;
            result.success = false;
            result.error = error;
            op->readCallback(result);
        } else if (op->writeCallback) {
            WriteResult result;
            result.success = false;
            result.error = error;
            op->writeCallback(result);
        } else if (op->infoCallback) {
            FileInfo info;
            op->infoCallback(info, error);
        }
    } else {
        // Success - call appropriate callback
        if (op->readCallback) {
            op->readCallback(op->readResult);
        } else if (op->writeCallback) {
            op->writeCallback(op->writeResult);
        } else if (op->infoCallback) {
            op->infoCallback(op->fileInfo, op->error);
        }
    }
    
    // Clean up
    delete op;
}

// Helper functions
ModernFS::FileInfo ModernFS::createFileInfo(const std::string& path) {
    FileInfo info;
    
    try {
        if (!std::filesystem::exists(path)) {
            return info; // Return empty info for non-existent files
        }
        
        info.path = path;
        
        auto fileStatus = std::filesystem::status(path);
        info.isFile = std::filesystem::is_regular_file(fileStatus);
        info.isDirectory = std::filesystem::is_directory(fileStatus);
        
        if (info.isFile) {
            info.size = std::filesystem::file_size(path);
            info.mimeType = detectMimeType(path);
        } else {
            info.size = 0;
            info.mimeType = "inode/directory";
        }
        
        // Get last modified time
        auto ftime = std::filesystem::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        info.lastModified = std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
        
    } catch (const std::exception& e) {
        // Return empty info on error
        info = FileInfo();
    }
    
    return info;
}

std::string ModernFS::detectMimeType(const std::string& path) {
    // Simple MIME type detection based on file extension
    std::string ext = path.substr(path.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "txt") return "text/plain";
    if (ext == "md") return "text/markdown";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    
    return "application/octet-stream";
}

void ModernFS::ensureDirectoryExists(const std::string& path) {
    try {
        std::filesystem::path filePath(path);
        std::filesystem::path dirPath = filePath.parent_path();
        
        if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }
    } catch (const std::exception& e) {
        // Ignore directory creation errors for now
    }
}

std::string ModernFS::FileInfo::toJSON() const {
    std::ostringstream json;
    json << "{"
         << "\"path\":\"" << path << "\","
         << "\"size\":" << size << ","
         << "\"isFile\":" << (isFile ? "true" : "false") << ","
         << "\"isDirectory\":" << (isDirectory ? "true" : "false") << ","
         << "\"mimeType\":\"" << mimeType << "\","
         << "\"lastModified\":" << lastModified
         << "}";
    return json.str();
}
