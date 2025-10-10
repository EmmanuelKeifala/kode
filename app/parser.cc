#include "parser.h"
#include "fs.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>

// Forward declaration to avoid circular dependency
class KodeRuntime;

std::vector<KodeParser::Statement> KodeParser::Parse(const std::string& code) {
    std::vector<Statement> statements;
    
    // Clean the code first
    std::string cleanCode = RemoveComments(code);
    
    // Split into lines
    std::vector<std::string> lines = SplitIntoLines(cleanCode);
    
    // Track if we're inside a callback function
    bool insideCallback = false;
    int braceDepth = 0;
    
    for (const std::string& line : lines) {
        std::string trimmed = TrimWhitespace(line);
        
        // Skip empty lines
        if (trimmed.empty()) {
            continue;
        }
        
        // Count braces to track callback scope
        for (char c : trimmed) {
            if (c == '{') braceDepth++;
            if (c == '}') braceDepth--;
        }
        
        // Handle callback detection
        if (trimmed.find("=>") != std::string::npos || trimmed.find("function") != std::string::npos) {
            insideCallback = true;
        }
        
        // If we're inside a callback, skip parsing individual lines
        if (insideCallback && braceDepth > 0) {
            continue;
        }
        
        // End of callback
        if (insideCallback && braceDepth <= 0) {
            insideCallback = false;
            continue;
        }
        
        // Skip callback-related syntax
        if (trimmed == "{" || trimmed == "}" || trimmed == "});" || 
            trimmed.find("if (err)") != std::string::npos ||
            trimmed.find("} else {") != std::string::npos ||
            trimmed == "err" || trimmed == "data.toString()") {
            continue;
        }
        
        // Parse the line
        Statement stmt = ParseLine(trimmed);
        if (stmt.type != Statement::UNKNOWN) {
            statements.push_back(stmt);
        }
    }
    
    return statements;
}

KodeParser::Statement KodeParser::ParseLine(const std::string& line) {
    Statement stmt;
    stmt.type = Statement::UNKNOWN;
    
    std::string trimmed = TrimWhitespace(RemoveSemicolons(line));
    
    // Parse different JavaScript patterns
    if (trimmed.find("console.log") != std::string::npos) {
        stmt.type = Statement::CONSOLE_LOG;
        
        // Extract arguments from console.log(...)
        size_t start = trimmed.find("(");
        size_t end = trimmed.rfind(")");
        
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string args = trimmed.substr(start + 1, end - start - 1);
            stmt.content = ExtractStringLiteral(args);
            if (stmt.content.empty()) {
                stmt.content = args; // Fallback to raw content
            }
        }
    }
    else if (trimmed.find("setTimeout") != std::string::npos) {
        stmt.type = Statement::SET_TIMEOUT;
        stmt.content = "Timer callback executed!";
    }
    else if (trimmed.find("fs.readFile") != std::string::npos && trimmed.find("Sync") == std::string::npos) {
        stmt.type = Statement::FS_READ_FILE;
        
        // Extract filename from fs.readFile('filename', ...)
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) {
            stmt.content = ExtractStringLiteral(args[0]);
        }
        if (stmt.content.empty()) {
            stmt.content = "index.js"; // Default
        }
        
        std::cout << "[Parser] Detected fs.readFile for: " << stmt.content << std::endl;
    }
    else if (trimmed.find("fs.readFileSync") != std::string::npos) {
        stmt.type = Statement::FS_READ_FILE_SYNC;
        
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) {
            stmt.content = ExtractStringLiteral(args[0]);
        }
        if (stmt.content.empty()) {
            stmt.content = "index.js"; // Default
        }
    }
    else if (trimmed.find("fs.writeFile") != std::string::npos) {
        stmt.type = Statement::FS_WRITE_FILE;
        
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        if (args.size() >= 2) {
            stmt.content = ExtractStringLiteral(args[0]); // filename
            stmt.options["content"] = ExtractStringLiteral(args[1]); // content
        }
    }
    else if (trimmed.find("require") != std::string::npos) {
        stmt.type = Statement::REQUIRE;
        
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) {
            stmt.content = ExtractStringLiteral(args[0]);
        }
    }
    else if (trimmed.find("const ") == 0 || trimmed.find("let ") == 0 || trimmed.find("var ") == 0) {
        stmt.type = Statement::VARIABLE_DECLARATION;
        stmt.content = trimmed;
    }
    
    return stmt;
}

bool KodeParser::ExecuteStatement(const Statement& stmt, KodeRuntime* runtime) {
    switch (stmt.type) {
        case Statement::CONSOLE_LOG:
            std::cout << stmt.content << std::endl;
            return true;
            
        case Statement::SET_TIMEOUT:
            // This would need access to KodeRuntime's setTimeout method
            std::cout << "[Parser] setTimeout detected - would set timer" << std::endl;
            return true;
            
        case Statement::FS_READ_FILE:
            std::cout << "[Parser] Reading file asynchronously: " << stmt.content << std::endl;
            KodeFS::ReadFile(stmt.content, [](const std::string& error, const std::string& data) {
                if (!error.empty()) {
                    std::cout << "Error: " << error << std::endl;
                } else {
                    std::cout << data << std::endl;
                }
            }, runtime);
            return true;
            
        case Statement::FS_READ_FILE_SYNC:
            {
                std::cout << "[Parser] Reading file synchronously: " << stmt.content << std::endl;
                std::string content = KodeFS::ReadFileSync(stmt.content);
                if (!content.empty()) {
                    std::cout << content << std::endl;
                } else {
                    std::cout << "Error: Could not read file " << stmt.content << std::endl;
                }
            }
            return true;
            
        case Statement::FS_WRITE_FILE:
            {
                std::string content = "Hello from Kode Runtime!";
                if (stmt.options.find("content") != stmt.options.end()) {
                    content = stmt.options.at("content");
                }
                
                std::cout << "[Parser] Writing file asynchronously: " << stmt.content << std::endl;
                KodeFS::WriteFile(stmt.content, content, [](const std::string& error) {
                    if (!error.empty()) {
                        std::cout << "Error: " << error << std::endl;
                    } else {
                        std::cout << "File written successfully" << std::endl;
                    }
                }, runtime);
            }
            return true;
            
        case Statement::REQUIRE:
            std::cout << "[Parser] Requiring module: " << stmt.content << std::endl;
            return true;
            
        case Statement::VARIABLE_DECLARATION:
            std::cout << "[Parser] Variable declaration: " << stmt.content << std::endl;
            return true;
            
        default:
            std::cout << "[Parser] Unknown statement type" << std::endl;
            return false;
    }
}

// Helper functions
std::vector<std::string> KodeParser::SplitIntoLines(const std::string& code) {
    std::vector<std::string> lines;
    std::stringstream ss(code);
    std::string line;
    
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    
    return lines;
}

std::string KodeParser::TrimWhitespace(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string KodeParser::RemoveComments(const std::string& code) {
    std::string result = code;
    
    // Remove single-line comments
    size_t pos = 0;
    while ((pos = result.find("//", pos)) != std::string::npos) {
        size_t endLine = result.find('\n', pos);
        if (endLine == std::string::npos) {
            result = result.substr(0, pos);
            break;
        } else {
            result.erase(pos, endLine - pos);
        }
    }
    
    return result;
}

std::string KodeParser::RemoveSemicolons(const std::string& str) {
    std::string result = str;
    if (!result.empty() && result.back() == ';') {
        result.pop_back();
    }
    return result;
}

std::vector<std::string> KodeParser::ExtractFunctionArguments(const std::string& line) {
    std::vector<std::string> args;
    
    size_t start = line.find("(");
    size_t end = line.rfind(")");
    
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return args;
    }
    
    std::string argsStr = line.substr(start + 1, end - start - 1);
    
    // Simple comma splitting (doesn't handle nested parentheses)
    std::stringstream ss(argsStr);
    std::string arg;
    
    while (std::getline(ss, arg, ',')) {
        args.push_back(TrimWhitespace(arg));
    }
    
    return args;
}

std::string KodeParser::ExtractStringLiteral(const std::string& str) {
    std::string trimmed = TrimWhitespace(str);
    
    // Handle single quotes
    if (trimmed.length() >= 2 && trimmed.front() == '\'' && trimmed.back() == '\'') {
        return trimmed.substr(1, trimmed.length() - 2);
    }
    
    // Handle double quotes
    if (trimmed.length() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.length() - 2);
    }
    
    return "";
}
