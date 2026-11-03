#include "log.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mmt8 {
namespace {

constexpr size_t kSlots = 512;  // power of two
constexpr size_t kLineMax = 240;

struct Slot {
    std::atomic<size_t> seq{0};
    char text[kLineMax];
};

/* Bounded multi-producer queue (Vyukov); the worker thread is the only consumer. */
struct Queue {
    std::array<Slot, kSlots> slots;
    std::atomic<size_t> head{0};
    size_t tail = 0;

    Queue() {
        for (size_t i = 0; i < kSlots; ++i) slots[i].seq.store(i, std::memory_order_relaxed);
    }

    Slot *claim() {
        size_t pos = head.load(std::memory_order_relaxed);
        for (;;) {
            Slot &s = slots[pos & (kSlots - 1)];
            const size_t seq = s.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0) {
                if (head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) return &s;
            } else if (dif < 0) {
                return nullptr;
            } else {
                pos = head.load(std::memory_order_relaxed);
            }
        }
    }
    static void publish(Slot *s, size_t pos) { s->seq.store(pos + 1, std::memory_order_release); }

    Slot *peek() {
        Slot &s = slots[tail & (kSlots - 1)];
        return s.seq.load(std::memory_order_acquire) == tail + 1 ? &s : nullptr;
    }
    void release(Slot *s) {
        s->seq.store(tail + kSlots, std::memory_order_release);
        ++tail;
    }
};

Queue g_queue;
std::mutex g_mu;
std::condition_variable g_cv;
std::thread g_worker;
bool g_stop = false;
int g_users = 0;
std::string g_path;

void drain(FILE *f) {
    while (Slot *s = g_queue.peek()) {
        if (f) {
            std::fputs(s->text, f);
            std::fputc('\n', f);
        }
        g_queue.release(s);
    }
    if (f) std::fflush(f);
}

FILE *open_log(const std::string &path) {
    if (path.empty()) return nullptr;
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
    FILE *f = _wfopen(w.c_str(), L"ab");
#else
    FILE *f = std::fopen(path.c_str(), "ab");
#endif
    if (f && std::fseek(f, 0, SEEK_END) == 0 && std::ftell(f) > (8L << 20)) {
        std::fclose(f);
#if defined(_WIN32)
        f = _wfopen(w.c_str(), L"wb");
#else
        f = std::fopen(path.c_str(), "wb");
#endif
    }
    return f;
}

void worker() {
    FILE *f = open_log(g_path);
    std::unique_lock<std::mutex> lk(g_mu);
    while (!g_stop) {
        g_cv.wait_for(lk, std::chrono::milliseconds(200));
        lk.unlock();
        drain(f);
        lk.lock();
    }
    lk.unlock();
    drain(f);
    if (f) std::fclose(f);
}

}  // namespace

void log_start(const std::string &path) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_users++ > 0) return;
    g_path = path;
    g_stop = false;
    g_worker = std::thread(worker);
}

void log_stop() {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_users == 0 || --g_users > 0) return;
        g_stop = true;
    }
    g_cv.notify_all();
    if (g_worker.joinable()) g_worker.join();
}

void log(const char *fmt, ...) {
    Slot *s = g_queue.claim();
    if (!s) return;
    const size_t pos = s->seq.load(std::memory_order_relaxed);
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    int n = std::snprintf(s->text, kLineMax, "%02d:%02d:%02d.%03d ", tm.tm_hour, tm.tm_min, tm.tm_sec,
                          static_cast<int>(ms % 1000));
    if (n < 0) n = 0;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(s->text + n, kLineMax - static_cast<size_t>(n), fmt, ap);
    va_end(ap);
    Queue::publish(s, pos);
}

std::string module_dir() {
    std::string path;
#if defined(_WIN32)
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&module_dir), &mod)) {
        wchar_t buf[MAX_PATH * 4];
        const DWORD n = GetModuleFileNameW(mod, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
        if (n > 0) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
            path.resize(static_cast<size_t>(len));
            WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), path.data(), len, nullptr, nullptr);
        }
    }
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&module_dir), &info) && info.dli_fname) path = info.dli_fname;
#endif
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
}

}  // namespace mmt8
