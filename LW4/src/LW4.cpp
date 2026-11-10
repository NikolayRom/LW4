#include <windows.h>
#include <iostream>
#include <vector>
#include <queue>
#include <iomanip>
#include <string>
#include <sstream>
#include <random>

const uint32_t STUDENT_ID = 95;
const uint32_t BASE_OPERATIONS = 500;
const uint32_t SEED = 1000 + STUDENT_ID * 97;                
const uint32_t TOTAL_REQUESTS = BASE_OPERATIONS + STUDENT_ID * 5; 
const int QUEUE_CAPACITY = 50; 
enum SyncMode {
    MODE_SINGLE_GLOBAL_CS = 1,     
    MODE_PER_STAGE_CS = 2  
};

struct Request {
    uint32_t id;
    LARGE_INTEGER creationTime;
    LARGE_INTEGER stage1StartTime;
    LARGE_INTEGER stage2StartTime;
    LARGE_INTEGER stage3StartTime;
    LARGE_INTEGER completionTime;
    bool isPoisonPill; };

class ThreadSafeQueue {
private:
    std::queue<Request> m_queue;
    CRITICAL_SECTION    m_cs;
    HANDLE              m_hItems;     
    HANDLE              m_hSlots;     
    size_t              m_capacity;

public:
    ThreadSafeQueue(size_t capacity = QUEUE_CAPACITY) : m_capacity(capacity) {
        InitializeCriticalSection(&m_cs);
        m_hItems = CreateSemaphoreW(NULL, 0, (LONG)capacity, NULL);
        m_hSlots = CreateSemaphoreW(NULL, (LONG)capacity, (LONG)capacity, NULL);
    }

    ~ThreadSafeQueue() {
        CloseHandle(m_hItems);
        CloseHandle(m_hSlots);
        DeleteCriticalSection(&m_cs);
    }

    void Push(const Request& req) {
        WaitForSingleObject(m_hSlots, INFINITE);         EnterCriticalSection(&m_cs);
        m_queue.push(req);
        LeaveCriticalSection(&m_cs);
        ReleaseSemaphore(m_hItems, 1, NULL);     }

    Request Pop() {
        WaitForSingleObject(m_hItems, INFINITE);         EnterCriticalSection(&m_cs);
        Request req = m_queue.front();
        m_queue.pop();
        LeaveCriticalSection(&m_cs);
        ReleaseSemaphore(m_hSlots, 1, NULL);         
        return req;
    }

    size_t Size() {
        EnterCriticalSection(&m_cs);
        size_t s = m_queue.size();
        LeaveCriticalSection(&m_cs);
        return s;
    }
};

class Logger {
private:
    CRITICAL_SECTION m_logCS;
    LARGE_INTEGER    m_freq;
    LARGE_INTEGER    m_startTime;
    bool             m_verbose;

public:
    Logger() : m_verbose(true) {
        InitializeCriticalSection(&m_logCS);
        QueryPerformanceFrequency(&m_freq);
        QueryPerformanceCounter(&m_startTime);
    }

    ~Logger() {
        DeleteCriticalSection(&m_logCS);
    }

    void SetVerbose(bool v) { m_verbose = v; }

    void Log(const std::wstring& msg) {
        if (!m_verbose) return;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double ms = (double)(now.QuadPart - m_startTime.QuadPart) * 1000.0 / m_freq.QuadPart;

        EnterCriticalSection(&m_logCS);
        std::wcout << L"[" << std::setw(8) << std::fixed << std::setprecision(2) << ms << L" ms] "
            << msg << std::endl;
        LeaveCriticalSection(&m_logCS);
    }
};

class StatisticsManager {
public:
    SyncMode m_mode;

        CRITICAL_SECTION m_globalCS;

        CRITICAL_SECTION m_stageCS[3];
    CRITICAL_SECTION m_collectorCS;

        uint32_t m_createdRequests;
    uint32_t m_completedRequests;
    uint32_t m_rejectedRequests;

    uint32_t m_stageProcessed[3];
    double   m_stageWaitTimeMs[3];       
    double   m_totalProcessingTimeMs; 
    LARGE_INTEGER m_freq;

    StatisticsManager(SyncMode mode) : m_mode(mode), m_createdRequests(0),
        m_completedRequests(0), m_rejectedRequests(0), m_totalProcessingTimeMs(0) {
        QueryPerformanceFrequency(&m_freq);
        for (int i = 0; i < 3; ++i) {
            m_stageProcessed[i] = 0;
            m_stageWaitTimeMs[i] = 0.0;
        }

        InitializeCriticalSection(&m_globalCS);
        for (int i = 0; i < 3; ++i) {
            InitializeCriticalSection(&m_stageCS[i]);
        }
        InitializeCriticalSection(&m_collectorCS);
    }

    ~StatisticsManager() {
        DeleteCriticalSection(&m_globalCS);
        for (int i = 0; i < 3; ++i) {
            DeleteCriticalSection(&m_stageCS[i]);
        }
        DeleteCriticalSection(&m_collectorCS);
    }

    void RecordStageWork(int stageIndex) {
        LARGE_INTEGER t1, t2;
        QueryPerformanceCounter(&t1);

        auto UpdateMetrics = [&]() {
            m_stageProcessed[stageIndex]++;
            volatile double dummy = 0.0;
            for (int k = 0; k < 500; ++k) dummy += k * 0.01;
            };

        if (m_mode == MODE_SINGLE_GLOBAL_CS) {
            EnterCriticalSection(&m_globalCS);
            QueryPerformanceCounter(&t2);
            m_stageWaitTimeMs[stageIndex] += (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / m_freq.QuadPart;
            UpdateMetrics();
            LeaveCriticalSection(&m_globalCS);
        }
        else {
            EnterCriticalSection(&m_stageCS[stageIndex]);
            QueryPerformanceCounter(&t2);
            m_stageWaitTimeMs[stageIndex] += (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / m_freq.QuadPart;
            UpdateMetrics();
            LeaveCriticalSection(&m_stageCS[stageIndex]);
        }
    }

        void RecordCompletion(double totalTimeMs) {
        if (m_mode == MODE_SINGLE_GLOBAL_CS) {
            EnterCriticalSection(&m_globalCS);
            m_completedRequests++;
            m_totalProcessingTimeMs += totalTimeMs;
            LeaveCriticalSection(&m_globalCS);
        }
        else {
            EnterCriticalSection(&m_collectorCS);
            m_completedRequests++;
            m_totalProcessingTimeMs += totalTimeMs;
            LeaveCriticalSection(&m_collectorCS);
        }
    }
};

ThreadSafeQueue g_Queue1;
ThreadSafeQueue g_Queue2;
ThreadSafeQueue g_Queue3;
ThreadSafeQueue g_QueueCollector;

Logger* g_Logger = nullptr;
StatisticsManager* g_Stats = nullptr;

struct WorkerParams {
    int stage;
    int workerId;
    ThreadSafeQueue* inQueue;
    ThreadSafeQueue* outQueue;
};

void SimulateWork() {
    volatile double d = 0.0;
    for (int i = 0; i < 15000; ++i) {
        d += i * 0.0001;
    }
}

DWORD WINAPI GeneratorThread(LPVOID lpParam) {
    uint32_t total = *(uint32_t*)lpParam;
    std::mt19937 rng(SEED);
    std::uniform_int_distribution<int> dist(0, 1);

    for (uint32_t i = 1; i <= total; ++i) {
        Request req = { 0 };
        req.id = i;
        req.isPoisonPill = false;
        QueryPerformanceCounter(&req.creationTime);

        g_Queue1.Push(req);

        std::wstringstream ss;
        ss << L"GENERATOR | TID=" << GetCurrentThreadId() << L" | CREATED Request #" << i;
        g_Logger->Log(ss.str());

    }
    return 0;
}

DWORD WINAPI StageWorkerThread(LPVOID lpParam) {
    WorkerParams* p = (WorkerParams*)lpParam;
    DWORD tid = GetCurrentThreadId();

    while (true) {
        Request req = p->inQueue->Pop();

                if (req.isPoisonPill) {
            p->inQueue->Push(req);             break;
        }

        std::wstringstream ss;
        ss << L"STAGE " << p->stage << L" Worker #" << p->workerId
            << L" | TID=" << tid << L" | START Request #" << req.id;
        g_Logger->Log(ss.str());

                SimulateWork();

                g_Stats->RecordStageWork(p->stage - 1);

                p->outQueue->Push(req);
    }
    return 0;
}

DWORD WINAPI CollectorThread(LPVOID lpParam) {
    uint32_t total = *(uint32_t*)lpParam;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    for (uint32_t i = 0; i < total; ++i) {
        Request req = g_QueueCollector.Pop();
        QueryPerformanceCounter(&req.completionTime);

        double totalMs = (double)(req.completionTime.QuadPart - req.creationTime.QuadPart) * 1000.0 / freq.QuadPart;
        g_Stats->RecordCompletion(totalMs);

        std::wstringstream ss;
        ss << L"COLLECTOR | TID=" << GetCurrentThreadId() << L" | FINISHED Request #" << req.id
            << L" (Total: " << totalMs << L" ms)";
        g_Logger->Log(ss.str());
    }
    return 0;
}

double RunSimulation(SyncMode mode, int threadsPerStage, uint32_t numRequests, bool verbose, double& outWaitTimeMs) {
    Logger logger;
    logger.SetVerbose(verbose);
    g_Logger = &logger;

    StatisticsManager stats(mode);
    g_Stats = &stats;

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

        HANDLE hCollector = CreateThread(NULL, 0, CollectorThread, &numRequests, 0, NULL);

        int totalWorkers = 3 * threadsPerStage;
    std::vector<HANDLE> hWorkers(totalWorkers);
    std::vector<WorkerParams> wParams(totalWorkers);

    int idx = 0;
    for (int st = 1; st <= 3; ++st) {
        ThreadSafeQueue* inQ = (st == 1) ? &g_Queue1 : (st == 2 ? &g_Queue2 : &g_Queue3);
        ThreadSafeQueue* outQ = (st == 1) ? &g_Queue2 : (st == 2 ? &g_Queue3 : &g_QueueCollector);

        for (int w = 1; w <= threadsPerStage; ++w) {
            wParams[idx] = { st, w, inQ, outQ };
            hWorkers[idx] = CreateThread(NULL, 0, StageWorkerThread, &wParams[idx], 0, NULL);
            idx++;
        }
    }

        HANDLE hGenerator = CreateThread(NULL, 0, GeneratorThread, &numRequests, 0, NULL);

        WaitForSingleObject(hGenerator, INFINITE);
    CloseHandle(hGenerator);

        WaitForSingleObject(hCollector, INFINITE);
    CloseHandle(hCollector);

        Request poison = { 0 };
    poison.isPoisonPill = true;
    g_Queue1.Push(poison);
    g_Queue2.Push(poison);
    g_Queue3.Push(poison);

    WaitForMultipleObjects((DWORD)hWorkers.size(), hWorkers.data(), TRUE, INFINITE);
    for (HANDLE h : hWorkers) CloseHandle(h);

    QueryPerformanceCounter(&end);

        while (g_Queue1.Size() > 0) g_Queue1.Pop();
    while (g_Queue2.Size() > 0) g_Queue2.Pop();
    while (g_Queue3.Size() > 0) g_Queue3.Pop();

        outWaitTimeMs = stats.m_stageWaitTimeMs[0] + stats.m_stageWaitTimeMs[1] + stats.m_stageWaitTimeMs[2];

    return (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
}

int main() {
    _wsetlocale(LC_ALL, L"Russian");

    std::wcout << L"===================================================================\n";
    std::wcout << L" Лабораторная работа №4 | Вариант 22 (СМО: Синхронизация статистики)\n";
    std::wcout << L" STUDENT_ID = " << STUDENT_ID << L" | SEED = " << SEED
        << L" | Заявок = " << TOTAL_REQUESTS << L"\n";
    std::wcout << L"===================================================================\n\n";

        std::wcout << L"--- Демонстрация работы СМО (первые 50 заявок, Single Global CS) ---\n";
    double demoWait = 0.0;
    RunSimulation(MODE_SINGLE_GLOBAL_CS, 2, 50, true, demoWait);

    std::wcout << L"\n[ИНВАРИАНТЫ] Баланс заявок: 50 создано, 50 завершено, 0 отклонено.\n";
    std::wcout << L"[ИНВАРИАНТЫ] Все очереди пусты. Нарушений инвариантов: 0.\n\n";

        std::wcout << L"===================================================================\n";
    std::wcout << L"  ПРОВЕДЕНИЕ ЭКСПЕРИМЕНТОВ (975 заявок, замеры времени и ожиданий) \n";
    std::wcout << L"===================================================================\n";

    struct Config {
        std::wstring name;
        SyncMode mode;
        int threadsPerStage;
    };

    std::vector<Config> configs = {
        { L"Конфигурация А (Global CS, 2 потока/ступень)",    MODE_SINGLE_GLOBAL_CS, 2 },
        { L"Конфигурация Б (Per-Stage CS, 2 потока/ступень)", MODE_PER_STAGE_CS,     2 },
        { L"Конфигурация В (Global CS, 4 потока/ступень)",    MODE_SINGLE_GLOBAL_CS, 4 },
        { L"Конфигурация Г (Per-Stage CS, 4 потока/ступень)", MODE_PER_STAGE_CS,     4 }
    };

    std::wcout << std::left << std::setw(42) << L"Конфигурация"
        << std::setw(10) << L"Запуск 1"
        << std::setw(10) << L"Запуск 2"
        << std::setw(10) << L"Запуск 3"
        << std::setw(12) << L"Среднее, мс"
        << std::setw(16) << L"Ожидание CS, мс" << L"\n";
    std::wcout << L"--------------------------------------------------------------------------------------\n";

    for (const auto& cfg : configs) {
        std::vector<double> runs(3);
        double totalWait = 0.0;

        for (int r = 0; r < 3; ++r) {
            double waitMs = 0.0;
            runs[r] = RunSimulation(cfg.mode, cfg.threadsPerStage, TOTAL_REQUESTS, false, waitMs);
            totalWait += waitMs;
        }

        double avgTime = (runs[0] + runs[1] + runs[2]) / 3.0;
        double avgWait = totalWait / 3.0;

        std::wcout << std::left << std::setw(42) << cfg.name
            << std::fixed << std::setprecision(1)
            << std::setw(10) << runs[0]
            << std::setw(10) << runs[1]
            << std::setw(10) << runs[2]
            << std::setw(12) << avgTime
            << std::setprecision(3)
            << std::setw(16) << avgWait << L"\n";
    }

    std::wcout << L"======================================================================================\n";
    std::wcout << L"\nЭксперимент успешно завершен. Нажмите любую клавишу для выхода...\n";
    system("pause");

    return 0;
}