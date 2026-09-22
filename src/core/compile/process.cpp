#include "core/compile/process.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>

#include <climits>
#endif

namespace placi {

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Quotes one argument following the MSVCRT command-line parsing rules.
void append_quoted(std::wstring& cmd, const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
        if (i == arg.size()) {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
        } else {
            cmd.append(backslashes, L'\\');
        }
        cmd += arg[i];
    }
    cmd += L'"';
}

}  // namespace

ProcessResult run_process(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) throw PlaciError("cannot create pipe");
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd;
    append_quoted(cmd, exe.wstring());
    for (auto& a : args) {
        cmd += L' ';
        append_quoted(cmd, widen(a));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring wcwd = cwd.empty() ? std::wstring() : cwd.wstring();
    BOOL ok = CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(write_end);
    if (!ok) {
        CloseHandle(read_end);
        throw PlaciError("cannot start " + path_str(exe));
    }

    ProcessResult r;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(read_end, buf, sizeof buf, &got, nullptr) && got > 0) r.output.append(buf, got);
    CloseHandle(read_end);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    r.exit_code = static_cast<int>(code);
    return r;
}

fs::path executable_dir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return fs::path(buf).parent_path();
}

#else

ProcessResult run_process(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd) {
    int fds[2];
    if (pipe(fds) != 0) throw PlaciError("cannot create pipe");
    pid_t pid = fork();
    if (pid < 0) throw PlaciError("cannot fork");
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
        std::vector<char*> argv;
        std::string exe_s = exe.string();
        argv.push_back(exe_s.data());
        std::vector<std::string> copy = args;
        for (auto& a : copy) argv.push_back(a.data());
        argv.push_back(nullptr);
        execv(exe_s.c_str(), argv.data());
        _exit(127);
    }
    close(fds[1]);
    ProcessResult r;
    char buf[4096];
    ssize_t got;
    while ((got = read(fds[0], buf, sizeof buf)) > 0) r.output.append(buf, static_cast<size_t>(got));
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

fs::path executable_dir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return fs::current_path();
    buf[n] = 0;
    return fs::path(buf).parent_path();
}

#endif

}  // namespace placi
