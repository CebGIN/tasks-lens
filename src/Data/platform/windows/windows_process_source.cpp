#include "Data/platform/windows/windows_process_source.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <string>
#include <utility>
#include <vector>

namespace tasks_lens {

namespace {

// FILETIME stores 100-nanosecond intervals; converts to nanoseconds.
std::uint64_t filetime_to_ns(const FILETIME& ft) {
  ULARGE_INTEGER value;
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return static_cast<std::uint64_t>(value.QuadPart) * 100ull;
}

std::string wide_to_utf8(const std::wstring& wide) {
  if (wide.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                       static_cast<int>(wide.size()), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::string process_command(HANDLE process) {
  wchar_t buffer[MAX_PATH];
  DWORD size = MAX_PATH;
  if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
    return wide_to_utf8(std::wstring(buffer, size));
  }
  return {};
}

std::string process_user(HANDLE process) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return {};

  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::string name;
  if (size > 0) {
    std::vector<BYTE> buffer(size);
    if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
      TOKEN_USER* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
      wchar_t domain[256];
      wchar_t account[256];
      DWORD domain_len = 256;
      DWORD account_len = 256;
      SID_NAME_USE use;
      if (LookupAccountSidW(nullptr, user->User.Sid, account, &account_len,
                            domain, &domain_len, &use)) {
        name = wide_to_utf8(std::wstring(account, account_len));
      }
    }
  }

  CloseHandle(token);
  return name;
}

}  // namespace

SystemInfo WindowsProcessSource::read_system() {
  SystemInfo info;

  FILETIME idle;
  FILETIME kernel;
  FILETIME user;
  if (GetSystemTimes(&idle, &kernel, &user)) {
    // The kernel time already includes idle time, so total = kernel + user.
    info.total_cpu_time_ns = filetime_to_ns(kernel) + filetime_to_ns(user);
    info.idle_cpu_time_ns = filetime_to_ns(idle);
  }

  MEMORYSTATUSEX mem;
  mem.dwLength = sizeof(mem);
  if (GlobalMemoryStatusEx(&mem)) {
    info.mem_total = mem.ullTotalPhys;
    info.mem_available = mem.ullAvailPhys;
    info.mem_used = mem.ullTotalPhys - mem.ullAvailPhys;
  }

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
      do {
        info.total++;
      } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
  }

  // Load average and per-state counts have no Windows equivalent; they stay 0.
  return info;
}

std::vector<ProcessInfo> WindowsProcessSource::read_processes() {
  std::vector<ProcessInfo> out;

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return out;

  PROCESSENTRY32W entry;
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      ProcessInfo proc;
      proc.pid = static_cast<int>(entry.th32ProcessID);
      proc.ppid = static_cast<int>(entry.th32ParentProcessID);
      proc.thread_count = static_cast<int>(entry.cntThreads);
      proc.command = wide_to_utf8(entry.szExeFile);

      // If this fails (protected process), only the snapshot fields above remain.
      HANDLE process = OpenProcess(
          PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
          entry.th32ProcessID);
      if (process != nullptr) {
        FILETIME create;
        FILETIME exit;
        FILETIME kernel;
        FILETIME user;
        if (GetProcessTimes(process, &create, &exit, &kernel, &user)) {
          proc.cpu_time_ns = filetime_to_ns(kernel) + filetime_to_ns(user);
        }

        PROCESS_MEMORY_COUNTERS counters;
        if (GetProcessMemoryInfo(process, &counters, sizeof(counters))) {
          proc.rss_bytes = counters.WorkingSetSize;
        }

        proc.priority = GetPriorityClass(process);

        const std::string image = process_command(process);
        if (!image.empty()) proc.command = image;

        proc.user = process_user(process);
        CloseHandle(process);
      }

      out.push_back(std::move(proc));
    } while (Process32NextW(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return out;
}

std::vector<ThreadInfo> WindowsProcessSource::read_threads(int pid) {
  std::vector<ThreadInfo> out;

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return out;

  THREADENTRY32 entry;
  entry.dwSize = sizeof(entry);
  if (Thread32First(snapshot, &entry)) {
    do {
      if (static_cast<int>(entry.th32OwnerProcessID) != pid) continue;
      ThreadInfo thread;
      thread.tid = static_cast<int>(entry.th32ThreadID);
      // The snapshot exposes no thread state; listed threads report Running.
      thread.state = ProcessState::Running;
      out.push_back(std::move(thread));
    } while (Thread32Next(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return out;
}

// TODO(windows): implement via OpenProcess/TerminateProcess and
// SuspendThread/ResumeThread; all four actions currently return false.
bool WindowsProcessSource::terminate(int, bool) { return false; }
bool WindowsProcessSource::suspend(int) { return false; }
bool WindowsProcessSource::resume(int) { return false; }
bool WindowsProcessSource::set_priority(int, int) { return false; }

}  // namespace tasks_lens

#endif  // _WIN32
