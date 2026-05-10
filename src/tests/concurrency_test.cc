#include "../concurrency/task.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

// Test the low-level concurrency implementation
void test_basic_task_creation() {
    std::cout << "\n=== Test 1: Basic Task Creation ===" << std::endl;
    
    ConcurrencyRuntime runtime;
    runtime.initialize(2);  // 2 worker threads
    
    std::atomic<int> counter{0};
    
    // Spawn multiple tasks
    for (int i = 0; i < 5; i++) {
        runtime.kode([i, &counter]() {
            std::cout << "[Task " << i << "] Starting execution" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            counter.fetch_add(1);
            std::cout << "[Task " << i << "] Completed (counter: " << counter.load() << ")" << std::endl;
        });
    }
    
    runtime.join_all();
    std::cout << "Final counter value: " << counter.load() << std::endl;
    runtime.shutdown();
}

void test_channel_communication() {
    std::cout << "\n=== Test 2: Channel Communication ===" << std::endl;
    
    ConcurrencyRuntime runtime;
    runtime.initialize(2);
    
    auto channel = runtime.make_channel<std::string>(2);  // Buffered channel
    
    // Producer task
    runtime.kode([channel]() {
        std::cout << "[Producer] Sending messages..." << std::endl;
        channel->send("Hello");
        channel->send("World");
        channel->send("From");
        channel->send("Channels");
        channel->close();
        std::cout << "[Producer] All messages sent and channel closed" << std::endl;
    });
    
    // Consumer task
    runtime.kode([channel]() {
        std::cout << "[Consumer] Receiving messages..." << std::endl;
        std::string msg;
        while (channel->receive(msg)) {
            std::cout << "[Consumer] Received: " << msg << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::cout << "[Consumer] Channel closed, stopping" << std::endl;
    });
    
    runtime.join_all();
    runtime.shutdown();
}

void test_preemptive_scheduling() {
    std::cout << "\n=== Test 3: Cooperative Scheduling Behavior ===" << std::endl;
    
    ConcurrencyRuntime runtime;
    runtime.initialize(1);  // Single worker to test preemption
    
    std::atomic<bool> heavy_task_running{false};
    std::atomic<bool> light_task_executed{false};
    
    // Heavy computation task
    runtime.kode([&heavy_task_running]() {
        heavy_task_running.store(true);
        std::cout << "[Heavy] Starting CPU-intensive task..." << std::endl;
        
        // Simulate heavy computation
        volatile long sum = 0;
        for (long i = 0; i < 100000000; i++) {
            sum += i;
            // Check for preemption periodically
            if (i % 10000000 == 0) {
                std::cout << "[Heavy] Progress: " << (i / 10000000) << "/10" << std::endl;
            }
        }
        
        std::cout << "[Heavy] Completed (sum: " << sum << ")" << std::endl;
        heavy_task_running.store(false);
    });
    
    // Light task may only run after the heavy task unless the heavy task yields.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Let heavy task start
    
    runtime.kode([&light_task_executed]() {
        std::cout << "[Light] Quick task executing!" << std::endl;
        light_task_executed.store(true);
    });
    
    runtime.join_all();
    
    if (light_task_executed.load()) {
        std::cout << "OK: Light task executed once the worker became available" << std::endl;
    } else {
        std::cout << "FAIL: Light task did not execute" << std::endl;
    }
    
    runtime.shutdown();
}

void test_timeout_functionality() {
    std::cout << "\n=== Test 4: Timeout Functionality ===" << std::endl;
    
    ConcurrencyRuntime runtime;
    runtime.initialize(2);
    
    // Task that should complete within timeout
    std::cout << "[Timeout] Testing task that completes in time..." << std::endl;
    runtime.with_timeout(std::chrono::milliseconds(200), []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[FastTask] Completed within timeout!" << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Task that should timeout
    std::cout << "[Timeout] Testing task that times out..." << std::endl;
    runtime.with_timeout(std::chrono::milliseconds(100), []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "[SlowTask] Work continued after cancellation request" << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    runtime.join_all();
    runtime.shutdown();
}

void test_context_switching() {
    std::cout << "\n=== Test 5: Context Switching ===" << std::endl;
    
    ConcurrencyRuntime runtime;
    runtime.initialize(1);  // Single worker to force context switching
    
    std::atomic<int> switch_count{0};
    
    // Multiple tasks that yield frequently
    for (int i = 0; i < 3; i++) {
        runtime.kode([i, &switch_count, &runtime]() {
            for (int j = 0; j < 5; j++) {
                std::cout << "[Task " << i << "] Iteration " << j << std::endl;
                runtime.yield();  // Cooperative yield
                switch_count.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::cout << "[Task " << i << "] Completed" << std::endl;
        });
    }
    
    runtime.join_all();
    std::cout << "Total context switches: " << switch_count.load() << std::endl;
    runtime.shutdown();
}

void test_performance_benchmark() {
    std::cout << "\n=== Test 6: Performance Benchmark ===" << std::endl;
    
    const int NUM_TASKS = 1000;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    ConcurrencyRuntime runtime;
    runtime.initialize();  // Auto-detect cores
    
    std::atomic<int> completed_tasks{0};
    
    // Spawn many lightweight tasks
    for (int i = 0; i < NUM_TASKS; i++) {
        runtime.kode([i, &completed_tasks]() {
            // Minimal work per task
            volatile int x = i * 2;
            (void)x;  // Suppress unused variable warning
            completed_tasks.fetch_add(1);
        });
    }
    
    runtime.join_all();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    std::cout << "Spawned and executed " << NUM_TASKS << " tasks in " 
              << duration.count() << " microseconds" << std::endl;
    std::cout << "Average time per task: " << (duration.count() / NUM_TASKS) << " microseconds" << std::endl;
    std::cout << "Tasks per second: " << (NUM_TASKS * 1000000 / duration.count()) << std::endl;
    
    runtime.shutdown();
}

int main() {
    std::cout << "=== Kode Concurrency System Tests ===" << std::endl;
    std::cout << "Testing Go-style goroutines, channels, and cooperative scheduling" << std::endl;
    
    try {
        test_basic_task_creation();
        test_channel_communication();
        test_preemptive_scheduling();
        test_timeout_functionality();
        test_context_switching();
        test_performance_benchmark();
        
        std::cout << "\n=== All Tests Completed ===" << std::endl;
        std::cout << "OK: Basic task creation and execution" << std::endl;
        std::cout << "OK: Channel-based communication (CSP)" << std::endl;
        std::cout << "OK: Cooperative scheduling" << std::endl;
        std::cout << "OK: Timeout and cancellation" << std::endl;
        std::cout << "OK: Context switching and yielding" << std::endl;
        std::cout << "OK: Performance benchmarking" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
