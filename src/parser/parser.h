#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_set>
#include <regex>

// Forward declaration
class KodeRuntime;

// Production-ready JavaScript parser for Kode runtime
// Features: AST-like structure, error recovery, modern JS syntax support
class KodeParser {
public:
    // Advanced statement representation with AST-like structure
    struct Statement {
        enum Type {
            // Core language constructs
            CONSOLE_LOG, CONSOLE_ERROR, CONSOLE_WARN, CONSOLE_INFO,
            
            // Async operations
            SET_TIMEOUT, SET_INTERVAL, CLEAR_TIMEOUT, CLEAR_INTERVAL,
            
            // File system operations
            FS_READ_FILE, FS_WRITE_FILE, FS_READ_FILE_SYNC, FS_WRITE_FILE_SYNC,
            FS_EXISTS, FS_MKDIR, FS_READDIR, FS_STAT,
            
            // Module system
            REQUIRE, IMPORT, EXPORT,
            
            // Variable declarations
            CONST_DECLARATION, LET_DECLARATION, VAR_DECLARATION,
            
            // Functions and classes
            FUNCTION_DECLARATION, ARROW_FUNCTION, CLASS_DECLARATION,
            METHOD_CALL, PROPERTY_ACCESS,
            
            // Control flow
            IF_STATEMENT, FOR_LOOP, WHILE_LOOP, TRY_CATCH,
            
            // Expressions
            ASSIGNMENT, BINARY_OPERATION, UNARY_OPERATION,
            
            // Literals
            STRING_LITERAL, NUMBER_LITERAL, BOOLEAN_LITERAL, NULL_LITERAL,
            
            // Advanced constructs
            ASYNC_FUNCTION, AWAIT_EXPRESSION, PROMISE_THEN, PROMISE_CATCH,
            TEMPLATE_LITERAL, DESTRUCTURING, SPREAD_OPERATOR,
            
            UNKNOWN, ERROR
        };
        
        Type type;
        std::string content;
        std::vector<std::string> arguments;
        std::map<std::string, std::string> options;
        
        // AST-like properties
        std::string identifier;           // Variable/function name
        std::string operator_;           // For operations
        std::vector<Statement> children; // Nested statements
        int lineNumber;                  // Source location
        int columnNumber;
        std::string sourceFile;
        
        // Error information
        bool hasError;
        std::string errorMessage;
        
        // Metadata
        bool isAsync;
        bool isExported;
        std::string scope;              // global, function, block
        std::vector<std::string> dependencies; // Required modules
    };
    
    // Parse result with comprehensive information
    struct ParseResult {
        std::vector<Statement> statements;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        std::unordered_set<std::string> dependencies;
        std::map<std::string, std::string> variables;
        bool hasAsyncOperations;
        bool hasModuleImports;
        int totalLines;
        double parseTimeMs;
    };
    
    // Production-ready parsing interface
    static ParseResult ParseAdvanced(const std::string& code, const std::string& filename = "");
    static std::vector<Statement> Parse(const std::string& code); // Legacy interface
    
    // Execution engine
    static bool ExecuteStatement(const Statement& stmt, KodeRuntime* runtime);
    static bool ExecuteStatements(const std::vector<Statement>& statements, KodeRuntime* runtime);
    
    // Validation and analysis
    static bool ValidateStatement(const Statement& stmt);
    static std::vector<std::string> AnalyzeDependencies(const std::vector<Statement>& statements);
    static bool HasAsyncOperations(const std::vector<Statement>& statements);
    
private:
    // Core parsing engine
    struct ParserState {
        std::string source;
        std::string filename;
        int currentLine;
        int currentColumn;
        std::vector<std::string> lines;
        std::unordered_set<std::string> variables;
        std::unordered_set<std::string> functions;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        bool inFunction;
        bool inClass;
        bool inCallback;
        int braceDepth;
        int parenDepth;
        std::string currentScope;
    };
    
    // Advanced parsing methods
    static ParseResult ParseWithState(const std::string& code, const std::string& filename);
    static Statement ParseStatement(const std::string& line, ParserState& state);
    static Statement ParseVariableDeclaration(const std::string& line, ParserState& state);
    static Statement ParseFunctionCall(const std::string& line, ParserState& state);
    static Statement ParseAsyncOperation(const std::string& line, ParserState& state);
    static Statement ParseModuleOperation(const std::string& line, ParserState& state);
    static Statement ParseControlFlow(const std::string& line, ParserState& state);
    
    // Specialized parsing functions
    static Statement ParseConsoleOperation(const std::string& line, ParserState& state);
    static Statement ParseFileSystemOperation(const std::string& line, ParserState& state);
    static Statement ParseFunctionDeclaration(const std::string& line, ParserState& state);
    static Statement ParseClassDeclaration(const std::string& line, ParserState& state);
    
    // Helper functions for advanced parsing
    static void UpdateParserState(const std::string& line, ParserState& state);
    static bool IsImportantCallbackStatement(const std::string& line);
    static bool IsAsyncStatement(const Statement& stmt);
    static bool IsModuleStatement(const Statement& stmt);
    static bool IsVariableDeclaration(const Statement& stmt);
    
    // Expression parsing
    static std::vector<std::string> ParseArguments(const std::string& args);
    static std::string ParseStringLiteral(const std::string& str);
    static bool IsValidIdentifier(const std::string& identifier);
    static bool IsReservedKeyword(const std::string& word);
    
    // Pattern recognition
    static bool IsCallbackPattern(const std::string& line);
    static bool IsAsyncPattern(const std::string& line);
    static bool IsModulePattern(const std::string& line);
    static bool IsControlFlowPattern(const std::string& line);
    
    // Syntax analysis
    static std::vector<std::string> TokenizeLine(const std::string& line);
    static std::string DetectStatementType(const std::string& line);
    static bool ValidateSyntax(const std::string& line);
    
    // Error handling and recovery
    static void AddError(ParserState& state, const std::string& message);
    static void AddWarning(ParserState& state, const std::string& message);
    static Statement CreateErrorStatement(const std::string& error, int line, int column);
    
    // Utility functions
    static std::vector<std::string> SplitIntoLines(const std::string& code);
    static std::string TrimWhitespace(const std::string& str);
    static std::string RemoveComments(const std::string& code);
    static std::string RemoveSemicolons(const std::string& str);
    static std::vector<std::string> ExtractFunctionArguments(const std::string& line);
    static std::string ExtractStringLiteral(const std::string& str);
    
    // Legacy support
    static Statement ParseLine(const std::string& line);
};
