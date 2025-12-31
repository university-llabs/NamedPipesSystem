#include "Browser.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>

Browser::Browser() : numWorkers(0), numTasks(0) {}

Browser::~Browser() {
    Cleanup();
}

void Browser::GetUserInput() {
    std::cout << " Browser Process: " << std::endl;

    do {
        std::cout << "Enter number of workers (1-" << MAX_WORKERS << "): ";
        std::cin >> numWorkers;
    } while (numWorkers < 1 || numWorkers > MAX_WORKERS);

    do {
        std::cout << "Enter number of tasks (1-100): ";
        std::cin >> numTasks;
    } while (numTasks < 1 || numTasks > 100);

    workers.resize(numWorkers);
    for (int i = 0; i < numWorkers; i++) {
        workers[i].id = i;
        workers[i].isBusy = false;
        workers[i].hProcess = INVALID_HANDLE_VALUE;
        workers[i].hInputPipe = INVALID_HANDLE_VALUE;
        workers[i].hOutputPipe = INVALID_HANDLE_VALUE;
    }
}

bool Browser::CreatePipes() {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    for (int i = 0; i < numWorkers; i++) {
        std::string inputPipeName = GetInputPipeName(i);
        std::string outputPipeName = GetOutputPipeName(i);

        workers[i].hInputPipe = CreateNamedPipeA(
            inputPipeName.c_str(),
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            sizeof(TaskMessage) + 1024,
            sizeof(TaskMessage) + 1024,
            0,
            &sa
        );

        if (workers[i].hInputPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create input pipe for worker " << i
                << ". Error: " << GetLastError() << std::endl;
            return false;
        }

        workers[i].hOutputPipe = CreateNamedPipeA(
            outputPipeName.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            sizeof(ResultMessage) + 1024,
            sizeof(ResultMessage) + 1024,
            0,
            &sa
        );

        if (workers[i].hOutputPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create output pipe for worker " << i
                << ". Error: " << GetLastError() << std::endl;
            CloseHandle(workers[i].hInputPipe);
            return false;
        }

        std::cout << "Created pipes for worker " << i << std::endl;
    }

    return true;
}

void Browser::CreateWorkerProcess(int workerId) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::string cmdLine = "Worker.exe " + std::to_string(workerId);

    char* cmdLineCopy = new char[cmdLine.length() + 1];
    strcpy_s(cmdLineCopy, cmdLine.length() + 1, cmdLine.c_str());

    if (!CreateProcessA(
        NULL,
        cmdLineCopy,
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    )) {
        std::cerr << "Failed to create worker process " << workerId
            << ". Error: " << GetLastError() << std::endl;
        delete[] cmdLineCopy;
        return;
    }

    delete[] cmdLineCopy;
    workers[workerId].hProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    std::cout << "Worker process " << workerId << " started (PID: " << pi.dwProcessId << ")" << std::endl;
}

bool Browser::SendTaskToWorker(int workerId, const TaskMessage* task) {
    WorkerInfo& worker = workers[workerId];

    if (!ConnectNamedPipe(worker.hInputPipe, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
            std::cerr << "Failed to connect to worker " << workerId
                << " input pipe. Error: " << err << std::endl;
            return false;
        }
    }

    uint32_t totalSize = sizeof(TaskMessage) + task->dataSize;
    DWORD bytesWritten;

    if (!WriteFile(worker.hInputPipe, task, totalSize, &bytesWritten, NULL)) {
        std::cerr << "Failed to send task to worker " << workerId
            << ". Error: " << GetLastError() << std::endl;
        return false;
    }

    if (bytesWritten != totalSize) {
        std::cerr << "Partial write to worker " << workerId << std::endl;
        return false;
    }

    FlushFileBuffers(worker.hInputPipe);
    worker.isBusy = true;

    return true;
}

bool Browser::ReceiveResultFromWorker(int workerId) {
    WorkerInfo& worker = workers[workerId];

    if (!ConnectNamedPipe(worker.hOutputPipe, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
            std::cerr << "Failed to connect to worker " << workerId
                << " output pipe. Error: " << err << std::endl;
            worker.isBusy = false;
            return false;
        }
    }

    ResultMessage header;
    DWORD bytesRead;

    if (!ReadFile(worker.hOutputPipe, &header, sizeof(ResultMessage), &bytesRead, NULL)) {
        DWORD err = GetLastError();
        std::cerr << "Failed to read result header from worker " << workerId
            << ". Error: " << err << std::endl;
        worker.isBusy = false;
        return false;
    }

    if (bytesRead != sizeof(ResultMessage)) {
        std::cerr << "Incomplete result header from worker " << workerId << std::endl;
        worker.isBusy = false;
        return false;
    }

    uint32_t totalSize = sizeof(ResultMessage) + header.resultSize;
    ResultMessage* fullResult = (ResultMessage*)malloc(totalSize);
    if (!fullResult) {
        std::cerr << "Memory allocation failed!" << std::endl;
        worker.isBusy = false;
        return false;
    }

    memcpy(fullResult, &header, sizeof(ResultMessage));

    if (header.resultSize > 0) {
        if (!ReadFile(worker.hOutputPipe, fullResult->data, header.resultSize, &bytesRead, NULL)) {
            std::cerr << "Failed to read result data from worker " << workerId << std::endl;
            free(fullResult);
            worker.isBusy = false;
            return false;
        }

        if (bytesRead != header.resultSize) {
            std::cerr << "Incomplete result data from worker " << workerId << std::endl;
            free(fullResult);
            worker.isBusy = false;
            return false;
        }
    }

    if (header.resultSize == sizeof(uint32_t)) {
        uint32_t count = *(uint32_t*)fullResult->data;
        std::cout << "Browser: Received result for task " << fullResult->taskId
            << " from worker " << workerId
            << ": count = " << count << std::endl;
    }

    free(fullResult);
    worker.isBusy = false;

    DisconnectNamedPipe(worker.hInputPipe);
    DisconnectNamedPipe(worker.hOutputPipe);

    return true;
}

bool Browser::Initialize() {
    std::cout << "Initializing Browser..." << std::endl;

    if (!CreatePipes()) {
        std::cerr << "Failed to create pipes." << std::endl;
        return false;
    }

    for (int i = 0; i < numWorkers; i++) {
        CreateWorkerProcess(i);
        Sleep(1000);
    }

    return true;
}

void Browser::Run() {
    std::cout << "\n=== Starting task distribution ===" << std::endl;
    std::cout << "Workers: " << numWorkers << ", Tasks: " << numTasks << std::endl;

    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<std::string> testStrings = {
        "hello world hello there hello everyone",
        "test test test test test",
        "aaa bbb aaa ccc aaa ddd",
        "quick brown fox jumps over lazy dog",
        "the cat in the hat is back"
    };

    std::vector<std::string> patterns = { "hello", "test", "aaa", "fox", "cat" };

    for (int taskId = 0; taskId < numTasks; taskId++) {
        int workerId = -1;
        for (int i = 0; i < numWorkers; i++) {
            if (!workers[i].isBusy) {
                workerId = i;
                break;
            }
        }

        if (workerId == -1) {
            for (int i = 0; i < numWorkers; i++) {
                if (workers[i].isBusy) {
                    ReceiveResultFromWorker(i);
                }
            }
            workerId = 0;
        }

        int stringIndex = taskId % testStrings.size();
        int patternIndex = taskId % patterns.size();

        std::string text = testStrings[stringIndex];
        std::string pattern = patterns[patternIndex];

        std::cout << "\n--- Task " << taskId << " ---" << std::endl;
        std::cout << "Text: \"" << text << "\"" << std::endl;
        std::cout << "Pattern: \"" << pattern << "\"" << std::endl;
        std::cout << "Worker: " << workerId << std::endl;

        std::vector<char> buffer;
        buffer.insert(buffer.end(), text.begin(), text.end());
        buffer.push_back('\0');
        buffer.insert(buffer.end(), pattern.begin(), pattern.end());
        buffer.push_back('\0');

        TaskMessage* task = CreateTaskMessage(
            MessageType::TASK_SUBSTRING,
            taskId,
            buffer.data(),
            static_cast<uint32_t>(buffer.size())
        );

        if (!task) {
            std::cerr << "Failed to create task " << taskId << std::endl;
            continue;
        }

        std::cout << "Sending task to worker " << workerId << std::endl;
        if (!SendTaskToWorker(workerId, task)) {
            std::cerr << "Failed to send task " << taskId << std::endl;
            FreeTaskMessage(task);
            continue;
        }

        FreeTaskMessage(task);

        if (!ReceiveResultFromWorker(workerId)) {
            std::cerr << "Failed to get result for task " << taskId << std::endl;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "\n=== All tasks completed ===" << std::endl;
    std::cout << "Total time: " << duration.count() << " ms" << std::endl;
    std::cout << "Average time per task: " << duration.count() / (double)numTasks << " ms" << std::endl;

    std::cout << "\n=== Sending termination commands ===" << std::endl;
    for (int i = 0; i < numWorkers; i++) {
        TaskMessage* termTask = CreateTaskMessage(MessageType::TERMINATE, 0, nullptr, 0);
        if (termTask) {
            if (SendTaskToWorker(i, termTask)) {
                ReceiveResultFromWorker(i);
            }
            FreeTaskMessage(termTask);
        }

        if (workers[i].hInputPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(workers[i].hInputPipe);
            workers[i].hInputPipe = INVALID_HANDLE_VALUE;
        }

        if (workers[i].hOutputPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(workers[i].hOutputPipe);
            workers[i].hOutputPipe = INVALID_HANDLE_VALUE;
        }
    }
    WaitForAllWorkers();
}

void Browser::WaitForAllWorkers() {
    for (auto& worker : workers) {
        if (worker.hProcess != INVALID_HANDLE_VALUE) {
            WaitForSingleObject(worker.hProcess, INFINITE);
            CloseHandle(worker.hProcess);
            worker.hProcess = INVALID_HANDLE_VALUE;
        }
    }
}

void Browser::Cleanup() {
    std::cout << "Cleaning up..." << std::endl;

    for (auto& worker : workers) {
        if (worker.hInputPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(worker.hInputPipe);
        }
        if (worker.hOutputPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(worker.hOutputPipe);
        }
        if (worker.hProcess != INVALID_HANDLE_VALUE) {
            CloseHandle(worker.hProcess);
        }
    }
    workers.clear();
}

int main() {
    Browser browser;

    browser.GetUserInput();

    if (!browser.Initialize()) {
        std::cerr << "Failed to initialize browser." << std::endl;
        std::cout << "Press Enter to exit...";
        std::cin.ignore();
        std::cin.get();
        return 1;
    }

    browser.Run();
    browser.Cleanup();

    std::cout << "\n=== Browser finished successfully ===" << std::endl;
    std::cout << "Press Enter to exit...";
    std::cin.ignore();
    std::cin.get();

    return 0;
}