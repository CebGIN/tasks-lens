#include "Data/platform/linux/linux_process_source.h"

#if defined(__linux__)

#include <dirent.h>
#include <pwd.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tasks_lens {

namespace {

// Kernel jiffies per second and memory page size, both queried once: they cannot
// change while the process runs.
std::uint64_t clock_ticks_per_sec() {
  static const long hz = sysconf(_SC_CLK_TCK);
  return hz > 0 ? static_cast<std::uint64_t>(hz) : 100u;
}

std::uint64_t ticks_to_ns(std::uint64_t ticks) {
  return ticks * 1000000000ull / clock_ticks_per_sec();
}

std::uint64_t page_size_bytes() {
  static const long ps = sysconf(_SC_PAGESIZE);
  return ps > 0 ? static_cast<std::uint64_t>(ps) : 4096ull;
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

bool is_numeric_pid(const char* name) {
  if (name == nullptr || *name == '\0') return false;
  for (const char* p = name; *p != '\0'; ++p) {
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
  }
  return true;
}

ProcessState map_state(char code) {
  switch (code) {
    case 'R': return ProcessState::Running;
    case 'S': return ProcessState::Sleeping;
    case 'D': return ProcessState::DiskSleep;
    case 'T':
    case 't': return ProcessState::Stopped;
    case 'Z':
    case 'X': return ProcessState::Zombie;
    case 'I': return ProcessState::Idle;
    default: return ProcessState::Unknown;
  }
}

// The fields of /proc/<pid>/stat that the monitor needs, by their 1-based
// kernel numbering (the comment on each is the field number).
struct StatFields {
  std::string comm;      // 2
  char state = '?';      // 3
  int ppid = 0;          // 4
  std::uint64_t utime = 0;  // 14
  std::uint64_t stime = 0;  // 15
  long nice = 0;         // 19
  long num_threads = 0;  // 20
  long rss = 0;          // 24
};

// comm (field 2) may contain spaces and parentheses, so fields are read from
// after the LAST ')' and indexed relative to field 3.
bool parse_stat(const std::string& data, StatFields& f) {
  const std::size_t lparen = data.find('(');
  const std::size_t rparen = data.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos ||
      rparen < lparen) {
    return false;
  }
  f.comm = data.substr(lparen + 1, rparen - lparen - 1);

  std::istringstream ss(data.substr(rparen + 1));
  std::vector<std::string> tokens;
  std::string token;
  while (ss >> token) tokens.push_back(token);
  if (tokens.size() < 22) return false;

  f.state = tokens[0].empty() ? '?' : tokens[0][0];
  f.ppid = std::atoi(tokens[1].c_str());
  f.utime = std::strtoull(tokens[11].c_str(), nullptr, 10);
  f.stime = std::strtoull(tokens[12].c_str(), nullptr, 10);
  f.nice = std::atol(tokens[16].c_str());
  f.num_threads = std::atol(tokens[17].c_str());
  f.rss = std::atol(tokens[21].c_str());
  return true;
}

bool read_uid(const std::string& status, uid_t& uid) {
  std::size_t pos = status.find("\nUid:");
  if (pos != std::string::npos) {
    pos += 1;
  } else if (status.compare(0, 4, "Uid:") == 0) {
    pos = 0;
  } else {
    return false;
  }
  std::istringstream ss(status.substr(pos + 4));
  unsigned long value = 0;
  if (!(ss >> value)) return false;
  uid = static_cast<uid_t>(value);
  return true;
}

// `uid` resolved to a user name, memoized in `cache`.
std::string username(uid_t uid, std::unordered_map<std::uint64_t, std::string>& cache) {
  const std::uint64_t key = static_cast<std::uint64_t>(uid);
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;

  std::string name = std::to_string(static_cast<unsigned long>(uid));
  char buf[1024];
  struct passwd pwd;
  struct passwd* result = nullptr;
  if (getpwuid_r(uid, &pwd, buf, sizeof(buf), &result) == 0 && result != nullptr &&
      result->pw_name != nullptr) {
    name = result->pw_name;
  }
  cache.emplace(key, name);
  return name;
}

// cmdline is a NUL-separated argument vector; separators become spaces.
std::string normalize_cmdline(std::string cmdline) {
  for (char& c : cmdline) {
    if (c == '\0') c = ' ';
  }
  while (!cmdline.empty() && cmdline.back() == ' ') cmdline.pop_back();
  return cmdline;
}

// Fills total/idle CPU time from /proc/stat's aggregate "cpu" line.
void read_cpu_times(SystemInfo& info) {
  std::string stat;
  if (!read_file("/proc/stat", stat)) return;
  std::istringstream ss(stat);
  std::string first;
  ss >> first;
  if (first != "cpu") return;
  // Fields: user nice system idle iowait irq softirq steal. guest/guest_nice
  // are already inside user/nice, and idle+iowait are indices 3 and 4.
  std::uint64_t ticks = 0;
  std::uint64_t idle_ticks = 0;
  std::uint64_t value = 0;
  for (int i = 0; i < 8 && (ss >> value); ++i) {
    ticks += value;
    if (i == 3 || i == 4) idle_ticks += value;
  }
  info.total_cpu_time_ns = ticks_to_ns(ticks);
  info.idle_cpu_time_ns = ticks_to_ns(idle_ticks);
}

// Counts all processes plus the running/stopped/zombie ones from /proc/<pid>/stat.
void read_task_counts(SystemInfo& info) {
  DIR* dir = opendir("/proc");
  if (dir == nullptr) return;
  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (!is_numeric_pid(entry->d_name)) continue;
    std::string stat_data;
    if (!read_file(std::string("/proc/") + entry->d_name + "/stat", stat_data)) {
      continue;  // Exited between readdir and read: skip, not an error.
    }
    StatFields fields;
    if (!parse_stat(stat_data, fields)) continue;
    info.total++;
    switch (map_state(fields.state)) {
      case ProcessState::Running: info.running++; break;
      case ProcessState::Stopped: info.stopped++; break;
      case ProcessState::Zombie: info.zombie++; break;
      default: break;
    }
  }
  closedir(dir);
}

void read_load_avg(SystemInfo& info) {
  std::string load;
  if (!read_file("/proc/loadavg", load)) return;
  std::istringstream ss(load);
  ss >> info.load_avg[0] >> info.load_avg[1] >> info.load_avg[2];
}

void read_memory(SystemInfo& info) {
  std::string meminfo;
  if (!read_file("/proc/meminfo", meminfo)) return;
  std::istringstream ss(meminfo);
  std::string line;
  std::uint64_t mem_free = 0;
  std::uint64_t buffers = 0;
  std::uint64_t cached = 0;
  while (std::getline(ss, line)) {
    std::istringstream ls(line);
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    ls >> key >> value >> unit;
    if (key == "MemTotal:") {
      info.mem_total = value * 1024;
    } else if (key == "MemFree:") {
      mem_free = value * 1024;
    } else if (key == "MemAvailable:") {
      info.mem_available = value * 1024;
    } else if (key == "Buffers:") {
      buffers = value * 1024;
    } else if (key == "Cached:") {
      cached = value * 1024;
    }
  }
  if (info.mem_available == 0) info.mem_available = mem_free + buffers + cached;
  if (info.mem_total > info.mem_available) {
    info.mem_used = info.mem_total - info.mem_available;
  }
}

}  // namespace

SystemInfo LinuxProcessSource::read_system() {
  SystemInfo info;

  read_cpu_times(info);

  read_task_counts(info);

  read_load_avg(info);

  read_memory(info);

  return info;
}

std::vector<ProcessInfo> LinuxProcessSource::read_processes() {
  std::vector<ProcessInfo> out;

  DIR* dir = opendir("/proc");
  if (dir == nullptr) return out;

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (!is_numeric_pid(entry->d_name)) continue;

    const std::string pid = entry->d_name;
    const std::string base = "/proc/" + pid;

    std::string stat_data;
    if (!read_file(base + "/stat", stat_data)) continue;
    StatFields fields;
    if (!parse_stat(stat_data, fields)) continue;

    ProcessInfo proc;
    proc.pid = std::atoi(pid.c_str());
    proc.ppid = fields.ppid;
    proc.state = map_state(fields.state);
    proc.cpu_time_ns = ticks_to_ns(fields.utime + fields.stime);
    proc.thread_count = static_cast<int>(fields.num_threads);
    proc.priority = static_cast<int>(fields.nice);
    proc.rss_bytes =
        fields.rss > 0
            ? static_cast<std::uint64_t>(fields.rss) * page_size_bytes()
            : 0;

    std::string status;
    if (read_file(base + "/status", status)) {
      uid_t uid = 0;
      if (read_uid(status, uid)) proc.user = username(uid, user_cache_);
    }

    std::string cmdline;
    if (read_file(base + "/cmdline", cmdline) && !cmdline.empty()) {
      proc.command = normalize_cmdline(cmdline);
    }
    if (proc.command.empty()) proc.command = fields.comm;  // kernel threads

    out.push_back(std::move(proc));
  }

  closedir(dir);
  return out;
}

std::vector<ThreadInfo> LinuxProcessSource::read_threads(int pid) {
  std::vector<ThreadInfo> out;

  const std::string task_dir = "/proc/" + std::to_string(pid) + "/task";
  DIR* dir = opendir(task_dir.c_str());
  if (dir == nullptr) return out;

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (!is_numeric_pid(entry->d_name)) continue;

    std::string stat_data;
    if (!read_file(task_dir + "/" + entry->d_name + "/stat", stat_data)) continue;
    StatFields fields;
    if (!parse_stat(stat_data, fields)) continue;

    ThreadInfo thread;
    thread.tid = std::atoi(entry->d_name);
    thread.state = map_state(fields.state);
    thread.cpu_time_ns = ticks_to_ns(fields.utime + fields.stime);
    thread.name = fields.comm;
    out.push_back(std::move(thread));
  }

  closedir(dir);
  return out;
}

bool LinuxProcessSource::terminate(int pid, bool force) {
  return ::kill(pid, force ? SIGKILL : SIGTERM) == 0;
}

bool LinuxProcessSource::suspend(int pid) {
  return ::kill(pid, SIGSTOP) == 0;
}

bool LinuxProcessSource::resume(int pid) {
  return ::kill(pid, SIGCONT) == 0;
}

bool LinuxProcessSource::set_priority(int pid, int priority) {
  return ::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), priority) == 0;
}

}  // namespace tasks_lens

#endif  // __linux__
