#include "parser.h"
#include "../filesystem/fs.h"
#include <iostream>
#include <sstream>
#include <algorithm>

// No runtime includes here to avoid circular deps; runtime handles execution routing

// Legacy interface for backward compatibility - simplified working version
std::vector<KodeParser::Statement> KodeParser::Parse(const std::string& code) {
    std::vector<Statement> statements;
    
    // Clean the code first
    std::string cleanCode = RemoveComments(code);
    std::vector<std::string> lines = SplitIntoLines(cleanCode);
    
    // Advanced parsing state
    bool insideCallback = false;
    int braceDepth = 0;
    
    for (const std::string& line : lines) {
        std::string trimmed = TrimWhitespace(line);
        
        if (trimmed.empty()) continue;
        
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
        if (IsCallbackPattern(trimmed)) {
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

// Next-generation JavaScript pattern recognition
bool KodeParser::IsCallbackPattern(const std::string& line) {
    // Modern callback patterns to skip
    std::vector<std::string> patterns = {
        "if (err)", "if (error)", "} else {", "} catch", "} finally",
        "return;", "throw", "break;", "continue;",
        "err", "error", "data.toString()", ".then(", ".catch("
    };
    
    for (const auto& pattern : patterns) {
        if (line.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

KodeParser::Statement KodeParser::ParseLine(const std::string& line) {
    Statement stmt;
    stmt.type = Statement::UNKNOWN;
    
    std::string trimmed = TrimWhitespace(RemoveSemicolons(line));
    
    // Kode concurrency API (must be detected BEFORE generic console/fs matching)
    if (trimmed.rfind("kode(", 0) == 0) { // starts with
        stmt.type = Statement::KODE_SPAWN;
        auto args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) {
            stmt.content = ExtractStringLiteral(args[0]);
        }
        return stmt;
    }
    if (trimmed.rfind("createChannel(", 0) == 0) {
        stmt.type = Statement::CH_CREATE;
        auto args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) stmt.options["name"] = ExtractStringLiteral(args[0]);
        if (args.size() > 1) {
            try { stmt.options["capacity"] = std::to_string(std::stoi(args[1])); }
            catch (...) { stmt.options["capacity"] = "0"; }
        } else {
            stmt.options["capacity"] = "0";
        }
        return stmt;
    }
    if (trimmed.rfind("sendToChannel(", 0) == 0) {
        stmt.type = Statement::CH_SEND;
        auto args = ExtractFunctionArguments(trimmed);
        if (args.size() >= 2) {
            stmt.options["name"] = ExtractStringLiteral(args[0]);
            stmt.options["value"] = ExtractStringLiteral(args[1]);
        }
        return stmt;
    }
    if (trimmed.rfind("receiveFromChannel(", 0) == 0) {
        stmt.type = Statement::CH_RECV;
        auto args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) stmt.options["name"] = ExtractStringLiteral(args[0]);
        return stmt;
    }
    if (trimmed == "yield()" || trimmed.rfind("yield(", 0) == 0) {
        stmt.type = Statement::YIELD_OP;
        return stmt;
    }
    if (trimmed.rfind("withTimeout(", 0) == 0) {
        stmt.type = Statement::WITH_TIMEOUT;
        auto args = ExtractFunctionArguments(trimmed);
        int ms = 0;
        if (!args.empty()) {
            try { ms = std::stoi(args[0]); } catch (...) { ms = 0; }
        }
        stmt.options["timeout"] = std::to_string(ms);
        if (args.size() > 1) stmt.content = ExtractStringLiteral(args[1]);
        return stmt;
    }
    
    // Console operations (enhanced)
    if (trimmed.rfind("console.", 0) == 0) {
        stmt.type = Statement::CONSOLE_LOG;
        
        // Handle multiple console methods: log, error, warn, info
        if (trimmed.find("console.error") != std::string::npos) stmt.type = Statement::CONSOLE_ERROR;
        else if (trimmed.find("console.warn") != std::string::npos) stmt.type = Statement::CONSOLE_WARN;
        else if (trimmed.find("console.info") != std::string::npos) stmt.type = Statement::CONSOLE_INFO;
        
        // Extract arguments with better parsing
        size_t start = trimmed.find("(");
        size_t end = trimmed.rfind(")");
        
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string args = trimmed.substr(start + 1, end - start - 1);
            stmt.content = ExtractStringLiteral(args);
            // ExtractStringLiteral now returns the original string if no quotes found
            // so we don't need the empty check anymore
        }
    }
    
    // Async operations (enhanced)
    else if (trimmed.rfind("setTimeout", 0) == 0 || trimmed.rfind("setInterval", 0) == 0) {
        stmt.type = Statement::SET_TIMEOUT;
        
        // Extract delay if specified
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        int delay = 1000; // default
        if (args.size() > 1) {
            try {
                delay = std::stoi(args[1]);
            } catch (...) {
                delay = 1000;
            }
        }
        
        stmt.content = "Timer callback executed!";
        stmt.options["delay"] = std::to_string(delay);
    }
    
    // File system operations (enhanced)
    else if (trimmed.rfind("fs.readFile", 0) == 0 && trimmed.find("Sync") == std::string::npos) {
        stmt.type = Statement::FS_READ_FILE;
        
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) {
            stmt.content = ExtractStringLiteral(args[0]);
        }
        if (stmt.content.empty()) {
            stmt.content = "index.js";
        }
        
        // Extract encoding if specified
        if (args.size() > 1) {
            std::string encoding = ExtractStringLiteral(args[1]);
            if (!encoding.empty()) {
                stmt.options["encoding"] = encoding;
            }
        }
    }
    
    else if (trimmed.rfind("fs.readFileSync", 0) == 0) {
        stmt.type = Statement::FS_READ_FILE_SYNC;
        
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) {
            stmt.content = ExtractStringLiteral(args[0]);
        }
        if (stmt.content.empty()) {
            stmt.content = "index.js";
        }
    }
    
    else if (trimmed.rfind("fs.writeFile", 0) == 0) {
        stmt.type = Statement::FS_WRITE_FILE;
        
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        if (args.size() >= 2) {
            stmt.content = ExtractStringLiteral(args[0]); // filename
            stmt.options["content"] = ExtractStringLiteral(args[1]); // content
        }
        
        // Handle encoding parameter
        if (args.size() > 2) {
            std::string encoding = ExtractStringLiteral(args[2]);
            if (!encoding.empty()) {
                stmt.options["encoding"] = encoding;
            }
        }
    }
    
    // Module system (enhanced)
    else if (trimmed.rfind("require(", 0) == 0 || trimmed.rfind("import ", 0) == 0) {
        stmt.type = Statement::REQUIRE;
        
        if (trimmed.find("require(") != std::string::npos) {
            // CommonJS require
            std::vector<std::string> args = ExtractFunctionArguments(trimmed);
            if (!args.empty()) {
                stmt.content = ExtractStringLiteral(args[0]);
            }
        } else {
            // ES6 import - extract module name
            size_t fromPos = trimmed.find("from ");
            if (fromPos != std::string::npos) {
                std::string modulePart = trimmed.substr(fromPos + 5);
                stmt.content = ExtractStringLiteral(TrimWhitespace(modulePart));
            }
        }
    }
    
    // Variable declarations (enhanced)
    else if (trimmed.find("const ") == 0) {
        stmt.type = Statement::CONST_DECLARATION;
        stmt.content = trimmed;
    }
    else if (trimmed.find("let ") == 0) {
        stmt.type = Statement::LET_DECLARATION;
        stmt.content = trimmed;
    }
    else if (trimmed.find("var ") == 0) {
        stmt.type = Statement::VAR_DECLARATION;
        stmt.content = trimmed;
    }
    
    return stmt;
}

bool KodeParser::ExecuteStatement(const Statement& stmt, KodeRuntime* runtime) {
    switch (stmt.type) {
        case Statement::CONSOLE_LOG:
        case Statement::CONSOLE_ERROR:
        case Statement::CONSOLE_WARN:
        case Statement::CONSOLE_INFO:
            std::cout << stmt.content << std::endl;
            return true;
            
        case Statement::SET_TIMEOUT:
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
        
        // Kode concurrency API execution is handled by KodeRuntime::ExecuteStatement
            
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
            
        case Statement::CONST_DECLARATION:
        case Statement::LET_DECLARATION:
        case Statement::VAR_DECLARATION:
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
    
    size_t start = line.find('(');
    size_t end = line.rfind(')');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return args;
    }
    
    std::string s = line.substr(start + 1, end - start - 1);
    std::string cur;
    bool inSingle = false, inDouble = false, inBacktick = false, escape = false;
    int depth = 0;
    
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (escape) {
            cur.push_back(c);
            escape = false;
            continue;
        }
        if (c == '\\') {
            cur.push_back(c);
            escape = true;
            continue;
        }
        if (inSingle) {
            if (c == '\'') inSingle = false;
            cur.push_back(c);
            continue;
        }
        if (inDouble) {
            if (c == '"') inDouble = false;
            cur.push_back(c);
            continue;
        }
        if (inBacktick) {
            if (c == '`') inBacktick = false;
            cur.push_back(c);
            continue;
        }
        if (c == '\'') { inSingle = true; cur.push_back(c); continue; }
        if (c == '"') { inDouble = true; cur.push_back(c); continue; }
        if (c == '`') { inBacktick = true; cur.push_back(c); continue; }
        if (c == '(') { depth++; cur.push_back(c); continue; }
        if (c == ')') { if (depth > 0) depth--; cur.push_back(c); continue; }
        if (c == ',' && depth == 0) {
            args.push_back(TrimWhitespace(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    
    if (!cur.empty() || !s.empty()) {
        args.push_back(TrimWhitespace(cur));
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
    
    // Handle template literals (backticks)
    if (trimmed.length() >= 2 && trimmed.front() == '`' && trimmed.back() == '`') {
        return trimmed.substr(1, trimmed.length() - 2);
    }
    
    // If no quotes found, return the original string (for expressions)
    return trimmed;
}
