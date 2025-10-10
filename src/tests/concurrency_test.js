// Comprehensive Concurrency Tests for Kode Runtime
// Testing Go-style goroutines, channels, and preemptive scheduling

console.log("=== Kode Concurrency Tests ===");

// Test 1: Basic Task Spawning
console.log("\n1. Testing basic task spawning:");
kode("console.log('Task 1 executed')");
kode("console.log('Task 2 executed')");
kode("console.log('Task 3 executed')");

// Test 2: Channel Communication
console.log("\n2. Testing channel communication:");
createChannel("test-channel", 2);  // Buffered channel with capacity 2

// Producer tasks
kode("sendToChannel('test-channel', 'Hello from producer')");
kode("sendToChannel('test-channel', 'Second message')");

// Consumer tasks (receive prints the value)
kode("receiveFromChannel('test-channel')");
kode("receiveFromChannel('test-channel')");

// Test 3: Cooperative Yielding
console.log("\n3. Testing cooperative yielding:");
kode("console.log('Before yield')");
kode("yield()\n");
kode("console.log('After yield')");

// Test 4: Timeout and Cancellation
console.log("\n4. Testing timeout functionality:");
withTimeout(100, "console.log('This should complete within 100ms')");

// Test 5: Heavy Computation (for preemption testing)
console.log("\n5. Testing preemptive scheduling (simplified demonstration):");
kode("console.log('Heavy task 1 simulated')");
kode("console.log('Heavy task 2 simulated')");
kode("console.log('Light task executed between heavy tasks')");

// Test 6: Multiple Channels
console.log("\n6. Testing multiple channels:");
createChannel("numbers", 0);   // Unbuffered channel
createChannel("strings", 3);   // Buffered channel

kode("sendToChannel('numbers', '42')");
kode("sendToChannel('strings', 'hello')");
kode("sendToChannel('strings', 'world')");

kode("receiveFromChannel('numbers')");
kode("receiveFromChannel('strings')");
kode("receiveFromChannel('strings')");

// Test 7: File System + Concurrency
console.log("\n7. Testing concurrent file operations:");
kode("fs.writeFile('concurrent-test-1.txt', 'Content from task 1')");
kode("fs.writeFile('concurrent-test-2.txt', 'Content from task 2')");
kode("fs.readFile('concurrent-test-1.txt')");

// Test 8: Mixed Sync/Async Operations
console.log("\n8. Testing mixed sync/async operations:");
kode("console.log('Sync operation')");
kode("fs.readFileSync('index.js')");
kode("console.log('Sync done')");
kode("console.log('Async start')");
kode("fs.readFile('index.js')");
kode("console.log('Async started')");

console.log("\nAll concurrency tests queued!");
console.log("Results will appear as tasks execute concurrently...");
console.log("Watch for preemption, context switching, and channel operations!");
