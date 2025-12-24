#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <cstring>

// Константы
constexpr int MAX_DATA_SIZE = 1024 * 1024; // 1MB

// Типы сообщений (должны совпадать с Browser)
enum class MessageType : uint32_t {
    TASK_SEPIA = 1,
    TASK_PRIMES = 2,
    TASK_SORT = 3,
    TASK_CRC32 = 4,
    TASK_STATS = 5,
    TASK_XOR = 6,
    TASK_SUBSTRING = 7,   // Наш вариант
    TASK_MATRIX_MULT = 8,
    TASK_FACTORIAL = 9,
    TASK_HISTOGRAM = 10,
    TASK_FOURIER = 11,
    TASK_RLE = 12,
    TASK_GRAPH_PATH = 13,
    TASK_INVERT = 14,
    TERMINATE = 999
};

// Структура для передачи задачи
#pragma pack(push, 1)
struct TaskMessage {
    MessageType type;
    uint32_t taskId;
    uint32_t dataSize;
    uint32_t extraParam;
    char data[1];
};
#pragma pack(pop)

// Структура для результата
#pragma pack(push, 1)
struct ResultMessage {
    uint32_t taskId;
    uint32_t resultSize;
    char data[1];
};
#pragma pack(pop)

// Вспомогательные функции
inline std::string GetInputPipeName(int workerId) {
    return std::string("\\\\.\\pipe\\worker_in_") + std::to_string(workerId);
}

inline std::string GetOutputPipeName(int workerId) {
    return std::string("\\\\.\\pipe\\worker_out_") + std::to_string(workerId);
}

inline bool WriteToPipe(HANDLE hPipe, const void* data, DWORD size) {
    DWORD bytesWritten;
    return WriteFile(hPipe, data, size, &bytesWritten, NULL) &&
        bytesWritten == size;
}

inline bool ReadFromPipe(HANDLE hPipe, void* buffer, DWORD size) {
    DWORD bytesRead;
    return ReadFile(hPipe, buffer, size, &bytesRead, NULL) &&
        bytesRead == size;
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

// Класс Worker
class Worker {
private:
    int workerId;
    HANDLE hInputPipe;
    HANDLE hOutputPipe;
    bool isRunning;

    bool ConnectToPipes();
    void ProcessLoop();
    void ProcessTask(const TaskMessage* task);
    uint32_t CountSubstringOccurrences(const char* text, const char* pattern);

public:
    Worker(int id);
    ~Worker();

    bool Initialize();
    void Run();
    void Stop();
};