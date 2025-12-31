#include <windows.h>
#include <string>
#include <iostream>
#include <cstring>

struct TaskMessage {
    uint32_t type;
    uint32_t taskId;
    uint32_t dataSize;
    char data[1];
};

struct ResultMessage {
    uint32_t taskId;
    uint32_t resultSize;
    char data[1];
};

uint32_t CountSubstring(const char* text, const char* pattern) {
    if (!text || !pattern || pattern[0] == '\0') return 0;

    uint32_t count = 0;
    const char* pos = text;
    size_t len = strlen(pattern);

    while ((pos = strstr(pos, pattern)) != nullptr) {
        count++;
        pos += len;
    }
    return count;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: Worker.exe <worker_id>" << std::endl;
        return 1;
    }

    int workerId = atoi(argv[1]);

    std::string inputPipeName = "\\\\.\\pipe\\worker_in_" + std::to_string(workerId);
    std::string outputPipeName = "\\\\.\\pipe\\worker_out_" + std::to_string(workerId);

    HANDLE hInputPipe = INVALID_HANDLE_VALUE;
    HANDLE hOutputPipe = INVALID_HANDLE_VALUE;

    while (true) {
        hInputPipe = CreateFileA(
            inputPipeName.c_str(),
            GENERIC_READ,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (hInputPipe != INVALID_HANDLE_VALUE) break;

        if (GetLastError() != ERROR_PIPE_BUSY) {
            return 1;
        }

        WaitNamedPipeA(inputPipeName.c_str(), 5000);
    }

    while (true) {
        hOutputPipe = CreateFileA(
            outputPipeName.c_str(),
            GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (hOutputPipe != INVALID_HANDLE_VALUE) break;

        if (GetLastError() != ERROR_PIPE_BUSY) {
            CloseHandle(hInputPipe);
            return 1;
        }

        WaitNamedPipeA(outputPipeName.c_str(), 5000);
    }

    while (true) {
        TaskMessage header;
        DWORD bytesRead;

        if (!ReadFile(hInputPipe, &header, sizeof(TaskMessage), &bytesRead, NULL)) {
            break;
        }

        uint32_t totalSize = sizeof(TaskMessage) + header.dataSize;
        TaskMessage* fullTask = (TaskMessage*)malloc(totalSize);
        memcpy(fullTask, &header, sizeof(TaskMessage));

        if (header.dataSize > 0) {
            if (!ReadFile(hInputPipe, fullTask->data, header.dataSize, &bytesRead, NULL)) {
                free(fullTask);
                break;
            }
        }

        ResultMessage* result = nullptr;
        uint32_t resultSize = 0;

        if (fullTask->type == 7) {
            const char* dataPtr = fullTask->data;
            std::string text(dataPtr);
            dataPtr += text.length() + 1;
            std::string pattern(dataPtr);

            uint32_t count = CountSubstring(text.c_str(), pattern.c_str());
            resultSize = sizeof(uint32_t);
            result = (ResultMessage*)malloc(sizeof(ResultMessage) + resultSize);
            result->taskId = fullTask->taskId;
            result->resultSize = resultSize;
            memcpy(result->data, &count, resultSize);
        }
        else if (fullTask->type == 999) {
            break;
        }

        free(fullTask);

        if (result) {
            DWORD bytesWritten;
            WriteFile(hOutputPipe, result, sizeof(ResultMessage) + result->resultSize, &bytesWritten, NULL);
            free(result);
        }

        FlushFileBuffers(hOutputPipe);
    }

    CloseHandle(hInputPipe);
    CloseHandle(hOutputPipe);
    return 0;
}   