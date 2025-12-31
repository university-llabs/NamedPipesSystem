#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <algorithm>

constexpr int MAX_WORKERS = 10;
constexpr int MAX_DATA_SIZE = 1024 * 1024; // 1MB

enum class MessageType : uint32_t {
    TASK_SUBSTRING = 7,   // Наш вариант
    TERMINATE = 999
};

// Структура для передачи задачи
#pragma pack(push, 1)
struct TaskMessage {
    MessageType type;
    uint32_t taskId;
    uint32_t dataSize;
    char data[1]; // гибкий массив
};
#pragma pack(pop)

// Структура для результата
#pragma pack(push, 1)
struct ResultMessage {
    uint32_t taskId;
    uint32_t resultSize;
    char data[1]; // гибкий массив
};
#pragma pack(pop)

// Вспомогательные функции
inline TaskMessage* CreateTaskMessage(MessageType type, uint32_t taskId,
    const void* data, uint32_t dataSize) {
    uint32_t totalSize = sizeof(TaskMessage) + dataSize;
    TaskMessage* msg = (TaskMessage*)malloc(totalSize);
    if (msg) {
        msg->type = type;
        msg->taskId = taskId;
        msg->dataSize = dataSize;
        if (data && dataSize > 0) {
            memcpy(msg->data, data, dataSize);
        }
    }
    return msg;
}

inline void FreeTaskMessage(TaskMessage* msg) {
    free(msg);
}

inline ResultMessage* CreateResultMessage(uint32_t taskId,
    const void* data, uint32_t dataSize) {
    uint32_t totalSize = sizeof(ResultMessage) + dataSize;
    ResultMessage* msg = (ResultMessage*)malloc(totalSize);
    if (msg) {
        msg->taskId = taskId;
        msg->resultSize = dataSize;
        if (data && dataSize > 0) {
            memcpy(msg->data, data, dataSize);
        }
    }
    return msg;
}

inline void FreeResultMessage(ResultMessage* msg) {
    free(msg);
}

inline std::string GetInputPipeName(int workerId) {
    return std::string("\\\\.\\pipe\\worker_in_") + std::to_string(workerId);
}

inline std::string GetOutputPipeName(int workerId) {
    return std::string("\\\\.\\pipe\\worker_out_") + std::to_string(workerId);
}

inline std::vector<std::string> GenerateTestStrings() {
    return {
        "hello world hello there hello everyone",
        "test test test test test",
        "aaa bbb aaa ccc aaa ddd",
        "quick brown fox jumps over lazy dog",
        "the cat in the hat is back",
        "programming is fun programming is cool",
        "data structures and algorithms",
        "operating system concepts",
        "computer networks and security",
        "artificial intelligence machine learning"
    };
}

// Класс Browser
class Browser {
private:
    int numWorkers;
    int numTasks;

    struct WorkerInfo {
        int id;
        HANDLE hProcess;
        HANDLE hInputPipe;  // Для отправки задач
        HANDLE hOutputPipe; // Для получения результатов
        bool isBusy;
    };

    std::vector<WorkerInfo> workers;

    bool CreatePipes();
    bool LaunchWorkerProcesses();
    void CreateWorkerProcess(int workerId);
    bool SendTaskToWorker(int workerId, const TaskMessage* task);
    bool ReceiveResultFromWorker(int workerId);
    void WaitForAllWorkers();

public:
    Browser();
    ~Browser();

    void GetUserInput();
    bool Initialize();
    void Run();
    void Cleanup();
};