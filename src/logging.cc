#include "logging.h"

#include <cstdio>

#if defined(GAME_WITH_ASSERTS) && !defined(_WIN32)
#include "libraries/backward.h"
#endif

namespace G {
namespace {

const char* LevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::kFatal:
      return "F";
    case LogLevel::kError:
      return "E";
    case LogLevel::kWarn:
      return "W";
    case LogLevel::kInfo:
      return "I";
    case LogLevel::kDebug:
      return "D";
    case LogLevel::kTrace:
      return "T";
  }
  return "?";
}

void DefaultLog(LogLevel level, const char* message) {
  fprintf(stdout, "%s %s\n", LevelToString(level), message);
}

[[noreturn]] void DefaultCrashHandler(const char* /*message*/) { std::abort(); }

LogSink g_LogSink = DefaultLog;

CrashHandler g_CrashHandler = DefaultCrashHandler;

}  // namespace

std::string_view TrimPath(std::string_view f) {
  if (f.empty()) return f;
  int p = f.size() - 1;
  while (p >= 0 && f[p] != '\\' && f[p] != '/') p--;
  return f.substr(p < 0 ? 0 : (p + 1));
}

LogSink GetLogSink() { return g_LogSink; }

void SetLogSink(LogSink sink) { g_LogSink = sink; }

void SetCrashHandler(CrashHandler handler) { g_CrashHandler = handler; }

#if defined(GAME_WITH_ASSERTS) && !defined(_WIN32)
void PrintStackTrace(backward::StackTrace& st) {
  backward::TraceResolver resolver;
  resolver.load_stacktrace(st);
  fprintf(stderr, "Stack trace (most recent call first):\n");
  for (size_t i = 0; i < st.size(); ++i) {
    backward::ResolvedTrace trace = resolver.resolve(st[i]);
    if (trace.source.filename.empty()) continue;
    std::string_view file = TrimPath(trace.source.filename);
    if (file == "backward.h" || file == "logging.cc" || file == "logging.h")
      continue;
    fprintf(stderr, "  [%.*s:%u] %s\n", static_cast<int>(file.size()),
            file.data(), trace.source.line, trace.object_function.c_str());
  }
}
#endif

[[noreturn]] void Crash(const char* message) {
#if defined(GAME_WITH_ASSERTS) && !defined(_WIN32)
  backward::StackTrace st;
  st.load_here(32);
  PrintStackTrace(st);
#endif
  g_CrashHandler(message);
  std::abort();
}

void InstallSignalHandlers() {
#if defined(GAME_WITH_ASSERTS) && !defined(_WIN32)
  const int signals[] = {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
  for (int sig : signals) {
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_flags = static_cast<int>(SA_SIGINFO | SA_NODEFER | SA_RESETHAND);
    sigfillset(&action.sa_mask);
    sigdelset(&action.sa_mask, sig);
    action.sa_sigaction = [](int signo, siginfo_t* /*info*/, void* ctx) {
      ucontext_t* uctx = static_cast<ucontext_t*>(ctx);
      backward::StackTrace st;
      void* error_addr = nullptr;
#ifdef REG_RIP
      error_addr = reinterpret_cast<void*>(uctx->uc_mcontext.gregs[REG_RIP]);
#elif defined(REG_EIP)
      error_addr = reinterpret_cast<void*>(uctx->uc_mcontext.gregs[REG_EIP]);
#elif defined(__aarch64__)
      error_addr = reinterpret_cast<void*>(uctx->uc_mcontext.pc);
#endif
      if (error_addr) {
        st.load_from(error_addr, /*depth=*/32, reinterpret_cast<void*>(uctx));
      } else {
        st.load_here(/*depth=*/32, reinterpret_cast<void*>(uctx));
      }
      fprintf(stderr, "Signal: %s (%d)\n", strsignal(signo), signo);
      PrintStackTrace(st);
      raise(signo);
    };
    sigaction(sig, &action, nullptr);
  }
#endif
}

#if defined(GAME_WITH_ASSERTS) && !defined(_WIN32)
void SetChannelLevel(LogChannel channel, LogLevel level) {
  g_channel_levels[static_cast<size_t>(channel)] = level;
}

LogLevel GetChannelLevel(LogChannel channel) {
  return g_channel_levels[static_cast<size_t>(channel)];
}
#endif

}  // namespace G
