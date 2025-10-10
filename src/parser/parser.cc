#include "parser.h"
#include "../filesystem/fs.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <chrono>
#include <unordered_set>

// Forward declaration to avoid circular dependency
class KodeRuntime;

KodeParser::ParseResult KodeParser::ParseAdvanced(const std::string& code, const std::string& filename) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    ParseResult result;
    result.totalLines = 0;
    result.hasAsyncOperations = false;
    result.hasModuleImports = false;
    
    try {
        result = ParseWithState(code, filename);
    } catch (const std::exception& e) {
        result.errors.push_back("Parser exception: " + std::string(e.what()));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    result.parseTimeMs = duration.count() / 1000.0;
    
    return result;
}

KodeParser::ParseResult KodeParser::ParseWithState(const std::string& code, const std::string& filename) {
    ParseResult result;
    ParserState state;
    
    // Initialize parser state
    state.source = code;
    state.filename = filename;
    state.currentLine = 1;
    state.currentColumn = 1;
    state.lines = SplitIntoLines(RemoveComments(code));
    state.inFunction = false;
    state.inClass = false;
    state.inCallback = false;
    state.braceDepth = 0;
    state.parenDepth = 0;
    state.currentScope = "global";
    
    result.totalLines = state.lines.size();
    
    // Reserved keywords for validation
    static const std::unordered_set<std::string> reservedKeywords = {
        "break", "case", "catch", "class", "const", "continue", "debugger", "default",
        "delete", "do", "else", "export", "extends", "finally", "for", "function",
        "if", "import", "in", "instanceof", "let", "new", "return", "super",
        "switch", "this", "throw", "try", "typeof", "var", "void", "while",
        "with", "yield", "async", "await", "enum", "implements", "interface",
        "package", "private", "protected", "public", "static"
    };
    
    // Parse each line with advanced error recovery
    for (size_t i = 0; i < state.lines.size(); i++) {
        state.currentLine = i + 1;
        state.currentColumn = 1;
        
        std::string line = TrimWhitespace(state.lines[i]);
        if (line.empty()) continue;
        
        try {
            // Update parser state based on line content
            UpdateParserState(line, state);
            
            // Skip callback internals but capture important statements
            if (state.inCallback && state.braceDepth > 0) {
                if (IsImportantCallbackStatement(line)) {
                    Statement stmt = ParseStatement(line, state);
                    if (stmt.type != Statement::UNKNOWN && stmt.type != Statement::ERROR) {
                        result.statements.push_back(stmt);
                    }
                }
                continue;
            }
            
            // Parse the statement
            Statement stmt = ParseStatement(line, state);
            
            if (stmt.type == Statement::ERROR) {
                result.errors.push_back(stmt.errorMessage);
            } else if (stmt.type != Statement::UNKNOWN) {
                result.statements.push_back(stmt);
                
                // Track dependencies and async operations
                if (IsAsyncStatement(stmt)) {
                    result.hasAsyncOperations = true;
                }
                
                if (IsModuleStatement(stmt)) {
                    result.hasModuleImports = true;
                    if (!stmt.content.empty()) {
                        result.dependencies.insert(stmt.content);
                    }
                }
                
                // Track variables
                if (IsVariableDeclaration(stmt)) {
                    result.variables[stmt.identifier] = stmt.content;
                    state.variables.insert(stmt.identifier);
                }
            }
            
        } catch (const std::exception& e) {
            AddError(state, "Line " + std::to_string(state.currentLine) + ": " + e.what());
        }
    }
    
    // Copy errors and warnings from state
    result.errors = state.errors;
    result.warnings = state.warnings;
    
    return result;
}

// Helper functions for advanced parsing
void KodeParser::UpdateParserState(const std::string& line, ParserState& state) {
    // Track braces and parentheses
    for (char c : line) {
        if (c == '{') state.braceDepth++;
        if (c == '}') state.braceDepth--;
        if (c == '(') state.parenDepth++;
        if (c == ')') state.parenDepth--;
    }
    
    // Detect function/callback/class contexts
    if (line.find("=>") != std::string::npos || line.find("function") != std::string::npos) {
        state.inCallback = true;
    }
    
    if (line.find("class ") != std::string::npos) {
        state.inClass = true;
    }
    
    // End of callback/function
    if (state.inCallback && state.braceDepth <= 0) {
        state.inCallback = false;
    }
}

bool KodeParser::IsImportantCallbackStatement(const std::string& line) {
    return line.find("console.") != std::string::npos ||
           line.find("fs.") != std::string::npos ||
           line.find("return") != std::string::npos ||
           line.find("throw") != std::string::npos;
}

bool KodeParser::IsAsyncStatement(const Statement& stmt) {
    return stmt.type == Statement::SET_TIMEOUT ||
           stmt.type == Statement::SET_INTERVAL ||
           stmt.type == Statement::FS_READ_FILE ||
           stmt.type == Statement::FS_WRITE_FILE ||
           stmt.isAsync;
}

bool KodeParser::IsModuleStatement(const Statement& stmt) {
    return stmt.type == Statement::REQUIRE ||
           stmt.type == Statement::IMPORT ||
           stmt.type == Statement::EXPORT;
}

bool KodeParser::IsVariableDeclaration(const Statement& stmt) {
    return stmt.type == Statement::CONST_DECLARATION ||
           stmt.type == Statement::LET_DECLARATION ||
           stmt.type == Statement::VAR_DECLARATION;
}

KodeParser::Statement KodeParser::ParseStatement(const std::string& line, ParserState& state) {
    Statement stmt;
    stmt.lineNumber = state.currentLine;
    stmt.columnNumber = state.currentColumn;
    stmt.sourceFile = state.filename;
    stmt.hasError = false;
    stmt.isAsync = false;
    stmt.isExported = false;
    stmt.scope = state.currentScope;
    
    std::string trimmed = TrimWhitespace(RemoveSemicolons(line));
    
    // Advanced pattern matching with error recovery
    try {
        // Console operations
        if (trimmed.find("console.") != std::string::npos) {
            return ParseConsoleOperation(trimmed, state);
        }
        
        // File system operations
        if (trimmed.find("fs.") != std::string::npos) {
            return ParseFileSystemOperation(trimmed, state);
        }
        
        // Async operations
        if (trimmed.find("setTimeout") != std::string::npos || 
            trimmed.find("setInterval") != std::string::npos) {
            return ParseAsyncOperation(trimmed, state);
        }
        
        // Module operations
        if (trimmed.find("require") != std::string::npos || 
            trimmed.find("import") != std::string::npos) {
            return ParseModuleOperation(trimmed, state);
        }
        
        // Variable declarations
        if (trimmed.find("const ") == 0 || trimmed.find("let ") == 0 || 
            trimmed.find("var ") == 0) {
            return ParseVariableDeclaration(trimmed, state);
        }
        
        // Function declarations
        if (trimmed.find("function ") == 0 || trimmed.find("async function") == 0) {
            return ParseFunctionDeclaration(trimmed, state);
        }
        
        // Class declarations
        if (trimmed.find("class ") == 0) {
            return ParseClassDeclaration(trimmed, state);
        }
        
        // If we get here, it's an unknown statement
        stmt.type = Statement::UNKNOWN;
        stmt.content = trimmed;
        
    } catch (const std::exception& e) {
        return CreateErrorStatement("Parse error: " + std::string(e.what()), 
                                   state.currentLine, state.currentColumn);
    }
    
    return stmt;
}

void KodeParser::AddError(ParserState& state, const std::string& message) {
    state.errors.push_back(message);
}

void KodeParser::AddWarning(ParserState& state, const std::string& message) {
    state.warnings.push_back(message);
}

KodeParser::Statement KodeParser::CreateErrorStatement(const std::string& error, int line, int column) {
    Statement stmt;
    stmt.type = Statement::ERROR;
    stmt.hasError = true;
    stmt.errorMessage = error;
    stmt.lineNumber = line;
    stmt.columnNumber = column;
    return stmt;
}

// Specialized parsing functions
KodeParser::Statement KodeParser::ParseConsoleOperation(const std::string& line, ParserState& state) {
    Statement stmt;
    stmt.type = Statement::CONSOLE_LOG;
    
    // Determine console method type
    if (line.find("console.error") != std::string::npos) stmt.type = Statement::CONSOLE_ERROR;
    else if (line.find("console.warn") != std::string::npos) stmt.type = Statement::CONSOLE_WARN;
    else if (line.find("console.info") != std::string::npos) stmt.type = Statement::CONSOLE_INFO;
    
    // Extract arguments
    size_t start = line.find("(");
    size_t end = line.rfind(")");
    
    if (start != std::string::npos && end != std::string::npos && end > start) {
        std::string args = line.substr(start + 1, end - start - 1);
        stmt.content = ExtractStringLiteral(args);
        if (stmt.content.empty()) {
            stmt.content = args; // Fallback for expressions
        }
        stmt.arguments = ParseArguments(args);
    }
    
    return stmt;
}

KodeParser::Statement KodeParser::ParseFileSystemOperation(const std::string& line, ParserState& state) {
    Statement stmt;
    stmt.isAsync = true; // Most FS operations are async
    
    if (line.find("fs.readFile") != std::string::npos) {
        stmt.type = line.find("Sync") != std::string::npos ? 
                   Statement::FS_READ_FILE_SYNC : Statement::FS_READ_FILE;
        if (stmt.type == Statement::FS_READ_FILE_SYNC) stmt.isAsync = false;
    }
    else if (line.find("fs.writeFile") != std::string::npos) {
        stmt.type = line.find("Sync") != std::string::npos ? 
                   Statement::FS_WRITE_FILE_SYNC : Statement::FS_WRITE_FILE;
        if (stmt.type == Statement::FS_WRITE_FILE_SYNC) stmt.isAsync = false;
    }
    else if (line.find("fs.exists") != std::string::npos) {
        stmt.type = Statement::FS_EXISTS;
    }
    else if (line.find("fs.mkdir") != std::string::npos) {
        stmt.type = Statement::FS_MKDIR;
    }
    else if (line.find("fs.readdir") != std::string::npos) {
        stmt.type = Statement::FS_READDIR;
    }
    else if (line.find("fs.stat") != std::string::npos) {
        stmt.type = Statement::FS_STAT;
    }
    
    // Extract filename argument
    std::vector<std::string> args = ExtractFunctionArguments(line);
    if (!args.empty()) {
        stmt.content = ExtractStringLiteral(args[0]);
        stmt.arguments = args;
        
        // For write operations, extract content
        if (args.size() >= 2 && (stmt.type == Statement::FS_WRITE_FILE || 
                                stmt.type == Statement::FS_WRITE_FILE_SYNC)) {
            stmt.options["content"] = ExtractStringLiteral(args[1]);
        }
    }
    
    return stmt;
}

KodeParser::Statement KodeParser::ParseAsyncOperation(const std::string& line, ParserState& state) {
    Statement stmt;
    stmt.isAsync = true;
    
    if (line.find("setTimeout") != std::string::npos) {
        stmt.type = Statement::SET_TIMEOUT;
    } else if (line.find("setInterval") != std::string::npos) {
        stmt.type = Statement::SET_INTERVAL;
    }
    
    // Extract delay parameter
    std::vector<std::string> args = ExtractFunctionArguments(line);
    int delay = 1000; // default
    
    if (args.size() > 1) {
        try {
            delay = std::stoi(args[1]);
        } catch (...) {
            AddWarning(state, "Invalid delay value, using default 1000ms");
        }
    }
    
    stmt.content = "Timer callback executed!";
    stmt.options["delay"] = std::to_string(delay);
    stmt.arguments = args;
    
    return stmt;
}

KodeParser::Statement KodeParser::ParseModuleOperation(const std::string& line, ParserState& state) {
    Statement stmt;
    
    if (line.find("require(") != std::string::npos) {
        stmt.type = Statement::REQUIRE;
        std::vector<std::string> args = ExtractFunctionArguments(line);
        if (!args.empty()) {
            stmt.content = ExtractStringLiteral(args[0]);
        }
    }
    else if (line.find("import ") != std::string::npos) {
        stmt.type = Statement::IMPORT;
        // Parse ES6 import syntax
        size_t fromPos = line.find("from ");
        if (fromPos != std::string::npos) {
            std::string modulePart = line.substr(fromPos + 5);
            stmt.content = ExtractStringLiteral(TrimWhitespace(modulePart));
        }
    }
    else if (line.find("export ") != std::string::npos) {
        stmt.type = Statement::EXPORT;
        stmt.isExported = true;
    }
    
    return stmt;
}

KodeParser::Statement KodeParser::ParseVariableDeclaration(const std::string& line, ParserState& state) {
    Statement stmt;
    
    if (line.find("const ") == 0) stmt.type = Statement::CONST_DECLARATION;
    else if (line.find("let ") == 0) stmt.type = Statement::LET_DECLARATION;
    else if (line.find("var ") == 0) stmt.type = Statement::VAR_DECLARATION;
    
    stmt.content = line;
    
    // Extract variable name
    size_t spacePos = line.find(" ");
    size_t equalPos = line.find("=");
    
    if (spacePos != std::string::npos && equalPos != std::string::npos) {
        std::string varName = line.substr(spacePos + 1, equalPos - spacePos - 1);
        stmt.identifier = TrimWhitespace(varName);
        
        // Extract value if present
        if (equalPos + 1 < line.length()) {
            std::string value = TrimWhitespace(line.substr(equalPos + 1));
            stmt.options["value"] = value;
        }
    }
    
    return stmt;
}

KodeParser::Statement KodeParser::ParseFunctionDeclaration(const std::string& line, ParserState& state) {
    Statement stmt;
    stmt.type = Statement::FUNCTION_DECLARATION;
    
    if (line.find("async ") != std::string::npos) {
        stmt.type = Statement::ASYNC_FUNCTION;
        stmt.isAsync = true;
    }
    
    stmt.content = line;
    state.inFunction = true;
    
    return stmt;
}

KodeParser::Statement KodeParser::ParseClassDeclaration(const std::string& line, ParserState& state) {
    Statement stmt;
    stmt.type = Statement::CLASS_DECLARATION;
    stmt.content = line;
    state.inClass = true;
    
    return stmt;
}

// Legacy interface for backward compatibility
std::vector<KodeParser::Statement> KodeParser::Parse(const std::string& code) {
    std::vector<Statement> statements;
    
    // Next-generation parsing approach
    std::string cleanCode = RemoveComments(code);
    std::vector<std::string> lines = SplitIntoLines(cleanCode);
    
    // Advanced parsing state
    struct ParseState {
        bool insideCallback = false;
        bool insideFunction = false;
        bool insideClass = false;
        int braceDepth = 0;
        int parenDepth = 0;
        std::string currentFunction;
        std::vector<std::string> callbackBuffer;
    } state;
    
    for (const std::string& line : lines) {
        std::string trimmed = TrimWhitespace(line);
        
        if (trimmed.empty()) continue;
        
        // Advanced brace and parentheses tracking
        for (char c : trimmed) {
            if (c == '{') state.braceDepth++;
            if (c == '}') state.braceDepth--;
            if (c == '(') state.parenDepth++;
            if (c == ')') state.parenDepth--;
        }
        
        // Detect modern JavaScript constructs
        bool isArrowFunction = trimmed.find("=>") != std::string::npos;
        bool isFunction = trimmed.find("function") != std::string::npos;
        bool isClass = trimmed.find("class ") != std::string::npos;
        bool isAsync = trimmed.find("async ") != std::string::npos;
        bool isAwait = trimmed.find("await ") != std::string::npos;
        
        // Handle different JavaScript constructs
        if (isClass) {
            state.insideClass = true;
            continue;
        }
        
        if (isArrowFunction || isFunction) {
            state.insideCallback = true;
            state.currentFunction = trimmed;
            continue;
        }
        
        // Skip callback internals but track important calls
        if (state.insideCallback && state.braceDepth > 0) {
            // Look for important calls inside callbacks
            if (trimmed.find("console.log") != std::string::npos ||
                trimmed.find("fs.") != std::string::npos) {
                Statement stmt = ParseLine(trimmed);
                if (stmt.type != Statement::UNKNOWN) {
                    statements.push_back(stmt);
                }
            }
            continue;
        }
        
        // End of callback/function
        if (state.insideCallback && state.braceDepth <= 0) {
            state.insideCallback = false;
            state.currentFunction.clear();
            continue;
        }
        
        // Skip common callback patterns
        if (IsCallbackPattern(trimmed)) {
            continue;
        }
        
        // Parse executable statements
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
    
    // Next-generation JavaScript parsing with better pattern recognition
    
    // Console operations (enhanced)
    if (trimmed.find("console.") != std::string::npos) {
        stmt.type = Statement::CONSOLE_LOG;
        
        // Handle multiple console methods: log, error, warn, info
        std::string method = "log";
        if (trimmed.find("console.error") != std::string::npos) method = "error";
        if (trimmed.find("console.warn") != std::string::npos) method = "warn";
        if (trimmed.find("console.info") != std::string::npos) method = "info";
        
        // Extract arguments with better parsing
        size_t start = trimmed.find("(");
        size_t end = trimmed.rfind(")");
        
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string args = trimmed.substr(start + 1, end - start - 1);
            stmt.content = ExtractStringLiteral(args);
            if (stmt.content.empty()) {
                // Handle template literals and expressions
                stmt.content = args;
            }
        }
    }
    
    // Async operations (enhanced)
    else if (trimmed.find("setTimeout") != std::string::npos || trimmed.find("setInterval") != std::string::npos) {
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
    else if (trimmed.find("fs.readFile") != std::string::npos && trimmed.find("Sync") == std::string::npos) {
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
    
    else if (trimmed.find("fs.readFileSync") != std::string::npos) {
        stmt.type = Statement::FS_READ_FILE_SYNC;
        
        std::vector<std::string> args = ExtractFunctionArguments(trimmed);
        if (!args.empty()) {
            stmt.content = ExtractStringLiteral(args[0]);
        }
        if (stmt.content.empty()) {
            stmt.content = "index.js";
        }
    }
    
    else if (trimmed.find("fs.writeFile") != std::string::npos) {
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
    else if (trimmed.find("require(") != std::string::npos || trimmed.find("import ") != std::string::npos) {
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
    else if (trimmed.find("const ") == 0 || trimmed.find("let ") == 0 || 
             trimmed.find("var ") == 0 || trimmed.find("class ") == 0) {
        stmt.type = Statement::VARIABLE_DECLARATION;
        stmt.content = trimmed;
        
        // Extract variable name for better tracking
        size_t spacePos = trimmed.find(" ");
        size_t equalPos = trimmed.find("=");
        if (spacePos != std::string::npos && equalPos != std::string::npos) {
            std::string varName = trimmed.substr(spacePos + 1, equalPos - spacePos - 1);
            stmt.options["name"] = TrimWhitespace(varName);
        }
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
