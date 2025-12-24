#include "Worker.h"

Worker::Worker(int id) : workerId(id), hInputPipe(INVALID_HANDLE_VALUE),
hOutputPipe(INVALID_HANDLE_VALUE), isRunning(false) {}

Worker::~Worker() {
    Stop();
}

bool Worker::ConnectToPipes() {
    // Подключаемся к входному пайпу (читаем задачи)
    std::string inputPipeName = GetInputPipeName(workerId);

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

        if (hInputPipe != INVALID_HANDLE_VALUE) {
            break;
        }

        if (GetLastError() != ERROR_PIPE_BUSY) {
            std::cerr << "Worker " << workerId << ": Failed to connect to input pipe. Error: " << GetLastError() << std::endl;
            return false;
        }

        // Ждем, если пайп занят
        if (!WaitNamedPipeA(inputPipeName.c_str(), 5000)) {
            std::cerr << "Worker " << workerId << ": Could not open pipe: 5 second wait timed out." << std::endl;
            return false;
        }
    }

    // Подключаемся к выходному пайпу (пишем результаты)
    std::string outputPipeName = GetOutputPipeName(workerId);

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

        if (hOutputPipe != INVALID_HANDLE_VALUE) {
            break;
        }

        if (GetLastError() != ERROR_PIPE_BUSY) {
            std::cerr << "Worker " << workerId << ": Failed to connect to output pipe. Error: " << GetLastError() << std::endl;
            CloseHandle(hInputPipe);
            return false;
        }

        if (!WaitNamedPipeA(outputPipeName.c_str(), 5000)) {
            std::cerr << "Worker " << workerId << ": Could not open output pipe: 5 second wait timed out." << std::endl;
            CloseHandle(hInputPipe);
            return false;
        }
    }

    std::cout << "Worker " << workerId << " connected to pipes." << std::endl;
    return true;
}

uint32_t Worker::CountSubstringOccurrences(const char* text, const char* pattern) {
    if (!text || !pattern || pattern[0] == '\0') {
        return 0;
    }

    uint32_t count = 0;
    const char* pos = text;

    while ((pos = strstr(pos, pattern)) != nullptr) {
        count++;
        pos++; // Перемещаемся на один символ вперед
    }

    return count;
}

void Worker::ProcessTask(const TaskMessage* task) {
    std::cout << "Worker " << workerId << " processing task " << task->taskId
        << " of type " << static_cast<int>(task->type) << std::endl;

    ResultMessage* result = nullptr;

    switch (task->type) {
    case MessageType::TASK_SUBSTRING: {
        // Извлекаем текст и подстроку из данных
        // Формат: текст\0подстрока\0
        const char* dataPtr = task->data;

        std::string text(dataPtr);
        dataPtr += text.length() + 1; // Пропускаем текст и нулевой символ

        std::string pattern(dataPtr);

        // Выполняем поиск подстроки
        uint32_t count = CountSubstringOccurrences(text.c_str(), pattern.c_str());

        // Создаем результат
        result = CreateResultMessage(task->taskId, &count, sizeof(uint32_t));

        std::cout << "Worker " << workerId << ": Found " << count
            << " occurrences of \"" << pattern << "\" in \"" << text << "\"" << std::endl;
        break;
    }
    case MessageType::TERMINATE: {
        std::cout << "Worker " << workerId << " received termination command." << std::endl;
        isRunning = false;
        result = CreateResultMessage(task->taskId, nullptr, 0);
        break;
    }
    default: {
        std::cerr << "Worker " << workerId << ": Unknown task type: " << static_cast<int>(task->type) << std::endl;
        uint32_t error = 0;
        result = CreateResultMessage(task->taskId, &error, sizeof(uint32_t));
        break;
    }
    }

    if (result) {
        // Отправляем результат
        DWORD bytesWritten;
        uint32_t totalSize = sizeof(ResultMessage) + result->resultSize;

        if (!WriteFile(hOutputPipe, result, totalSize, &bytesWritten, NULL)) {
            std::cerr << "Worker " << workerId << ": Failed to write result. Error: " << GetLastError() << std::endl;
        }

        FreeResultMessage(result);
    }
}

void Worker::ProcessLoop() {
    while (isRunning) {
        // Читаем заголовок задачи
        TaskMessage header;
        if (!ReadFromPipe(hInputPipe, &header, sizeof(TaskMessage))) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                std::cout << "Worker " << workerId << ": Pipe disconnected." << std::endl;
            }
            else {
                std::cerr << "Worker " << workerId << ": Read failed. Error: " << GetLastError() << std::endl;
            }
            break;
        }

        // Выделяем память для полного сообщения
        uint32_t totalSize = sizeof(TaskMessage) + header.dataSize;
        TaskMessage* fullMessage = (TaskMessage*)malloc(totalSize);
        if (!fullMessage) {
            std::cerr << "Worker " << workerId << ": Memory allocation failed!" << std::endl;
            break;
        }

        // Копируем заголовок
        memcpy(fullMessage, &header, sizeof(TaskMessage));

        // Читаем данные, если они есть
        if (header.dataSize > 0) {
            if (!ReadFromPipe(hInputPipe, fullMessage->data, header.dataSize)) {
                std::cerr << "Worker " << workerId << ": Failed to read task data. Error: " << GetLastError() << std::endl;
                free(fullMessage);
                break;
            }
        }

        // Обрабатываем задачу
        ProcessTask(fullMessage);

        free(fullMessage);

        // Если получили команду завершения, выходим
        if (header.type == MessageType::TERMINATE) {
            break;
        }
    }
}

bool Worker::Initialize() {
    std::cout << "Initializing Worker " << workerId << "..." << std::endl;
    return ConnectToPipes();
}

void Worker::Run() {
    isRunning = true;
    std::cout << "Worker " << workerId << " started." << std::endl;
    ProcessLoop();
}

void Worker::Stop() {
    isRunning = false;

    if (hInputPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hInputPipe);
        hInputPipe = INVALID_HANDLE_VALUE;
    }

    if (hOutputPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hOutputPipe);
        hOutputPipe = INVALID_HANDLE_VALUE;
    }

    std::cout << "Worker " << workerId << " stopped." << std::endl;
}

// Точка входа для Worker
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: Worker.exe <worker_id>" << std::endl;
        return 1;
    }

    int workerId = std::stoi(argv[1]);

    Worker worker(workerId);

    if (!worker.Initialize()) {
        std::cerr << "Failed to initialize worker " << workerId << std::endl;
        return 1;
    }

    worker.Run();

    return 0;
}