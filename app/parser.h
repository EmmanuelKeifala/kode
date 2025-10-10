#pragma once
#include <string>
#include <vector>
#include <map>

// Forward declaration
class KodeRuntime;

// JavaScript parser for Kode runtime
// This handles parsing JavaScript-like syntax into executable commands
class KodeParser {
public:
    // Represents a parsed JavaScript statement
    struct Statement {
        enum Type {
            CONSOLE_LOG,
            SET_TIMEOUT,
            FS_READ_FILE,
            FS_WRITE_FILE,
            FS_READ_FILE_SYNC,
            VARIABLE_DECLARATION,
            FUNCTION_CALL,
            REQUIRE,
            UNKNOWN
        };
        
        Type type;
        std::string content;
        std::vector<std::string> arguments;
        std::map<std::string, std::string> options;
    };
    
    // Parse JavaScript code into statements
    static std::vector<Statement> Parse(const std::string& code);
    
    // Execute a single statement
    static bool ExecuteStatement(const Statement& stmt, KodeRuntime* runtime);
    
private:
    // Helper functions for parsing
    static std::vector<std::string> SplitIntoLines(const std::string& code);
    static std::string TrimWhitespace(const std::string& str);
    static std::vector<std::string> ExtractFunctionArguments(const std::string& line);
    static std::string ExtractStringLiteral(const std::string& str);
    static Statement ParseLine(const std::string& line);
    
    // Clean up JavaScript syntax
    static std::string RemoveComments(const std::string& code);
    static std::string RemoveSemicolons(const std::string& str);
};
