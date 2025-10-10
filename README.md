# kode

# Kode Runtime - Project Structure

## Overview
Kode is a learning JavaScript runtime built with modern C++ and libuv, designed to demonstrate how Node.js works internally. The project is organized into modular components for better maintainability and understanding.

## Directory Structure

```
kode/
├── src/                          # Source code (organized by feature)
│   ├── main.cc                   # Main entry point
│   ├── core/                     # Core runtime engine
│   │   ├── runtime.h             # Runtime class definition
│   │   └── runtime.cc            # Runtime implementation
│   ├── parser/                   # JavaScript parser
│   │   ├── parser.h              # Advanced parser interface
│   │   └── parser.cc             # Production-ready parser
│   ├── filesystem/               # File system operations
│   │   ├── fs.h                  # Legacy FS API
│   │   ├── fs.cc                 # Legacy FS implementation
│   │   ├── modern_fs.h           # Modern FS API
│   │   └── modern_fs.cc          # Modern FS implementation
│   ├── examples/                 # Example programs
│   │   └── simple-test.cpp       # Basic V8 test
│   └── tests/                    # Test files and demos
├── bin/                          # Compiled binaries
├── libuv/                        # libuv library
├── v8/                           # V8 JavaScript engine
├── Makefile                      # Build configuration
└── README.md                     # Project documentation
```

## Component Architecture

### 1. Core Runtime (`src/core/`)
**Purpose**: Main JavaScript runtime engine
**Key Features**:
- Event loop management (libuv integration)
- Built-in functions (console, timers)
- Module loading and execution
- Memory management

**Files**:
- `runtime.h` - Class definitions and interfaces
- `runtime.cc` - Implementation of runtime logic

### 2. Parser (`src/parser/`)
**Purpose**: Advanced JavaScript syntax analysis
**Key Features**:
- AST-like statement representation
- Modern JS syntax support (ES6+, async/await)
- Error recovery and reporting
- Performance monitoring
- 25+ statement types

**Files**:
- `parser.h` - Parser interface and data structures
- `parser.cc` - Production-ready parsing engine

### 3. File System (`src/filesystem/`)
**Purpose**: File I/O operations with modern and legacy APIs
**Key Features**:
- Legacy callback-style API (Node.js compatible)
- Modern structured result API (next-generation)
- Async operations with libuv thread pool
- Rich metadata (MIME types, file stats)
- Automatic directory creation

**Files**:
- `fs.h/fs.cc` - Legacy file system API
- `modern_fs.h/modern_fs.cc` - Next-generation file system API

### 4. Main Entry Point (`src/main.cc`)
**Purpose**: Application entry point and CLI handling
**Key Features**:
- Command-line argument processing
- Demo mode for learning
- Version information
- Usage help

## Key Design Principles

### 1. **Modular Architecture**
Each component is self-contained with clear interfaces:
```cpp
#include "core/runtime.h"      // Core engine
#include "parser/parser.h"     // JavaScript parsing
#include "filesystem/fs.h"     // File operations
```

### 2. **Educational Focus**
- Extensive comments explaining Node.js concepts
- Demo mode showing different features
- Clear separation of concerns
- Production-ready patterns

### 3. **Modern C++ Practices**
- RAII for resource management
- Smart pointers where appropriate
- Exception safety
- STL containers and algorithms

### 4. **Performance Oriented**
- libuv event loop for async operations
- Thread pool for I/O operations
- Parse time measurement
- Memory-efficient data structures

## Build System

### Makefile Structure
```makefile
# Source files organized by component
APP = src/main.cc src/core/runtime.cc src/filesystem/fs.cc \
      src/filesystem/modern_fs.cc src/parser/parser.cc

# Dependencies
- libuv (event loop, async I/O)
- V8 (future JavaScript engine integration)
```

### Build Targets
- `make build` - Build the main runtime
- `make clean` - Clean build artifacts
- `make examples` - Build example programs

## Usage Examples

### Basic Execution
```bash
# Run a JavaScript file
./bin/kode script.js

# Execute code directly
./bin/kode -e "console.log('Hello World')"

# Demo mode (shows all features)
./bin/kode
```

### Feature Demonstrations
```javascript
// Modern file system API
fs.readFile('data.txt');        // Returns rich metadata
fs.writeFile('out.txt', 'data'); // Auto-creates directories

// Async operations
setTimeout();                    // Event loop integration

// Module system
const fs = require('fs');        // CommonJS support
```

## Development Workflow

### Adding New Features
1. Create appropriate directory under `src/`
2. Define interface in `.h` file
3. Implement in `.cc` file
4. Update `Makefile` APP definition
5. Add tests/examples
6. Update documentation

### Code Organization Guidelines
- **Headers**: Interface definitions, forward declarations
- **Implementation**: Logic, error handling, performance code
- **Separation**: Each component handles one responsibility
- **Documentation**: Explain the "why" not just the "what"

## Future Enhancements

### Planned Components
- `src/network/` - HTTP server implementation
- `src/crypto/` - Cryptographic functions
- `src/streams/` - Stream processing
- `src/cluster/` - Multi-process support

### Integration Points
- V8 JavaScript engine (when architecture issues resolved)
- Native module loading
- Debugger protocol
- Performance profiling

This modular architecture makes Kode both educational and extensible, demonstrating professional software organization while teaching Node.js internals.
