#pragma once

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace tgfx::internal {

struct ProcessResult {
    int exit_code = 127;
    std::string start_error;
    std::string output;
};

inline std::string shell_quote_arg(const std::string& text) {
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

#ifdef _WIN32
inline std::wstring widen_windows_arg(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    int len = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.c_str(),
        -1,
        nullptr,
        0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (len <= 0) {
        code_page = CP_ACP;
        flags = 0;
        len = MultiByteToWideChar(code_page, flags, text.c_str(), -1, nullptr, 0);
    }
    if (len <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(code_page, flags, text.c_str(), -1, out.data(), len);
    return out;
}

inline std::string narrow_windows_text(const wchar_t* text) {
    if (!text || text[0] == L'\0') {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        std::wstring wide(text);
        std::string fallback;
        fallback.reserve(wide.size());
        for (wchar_t ch : wide) {
            fallback.push_back(ch >= 0 && ch <= 0x7f ? static_cast<char>(ch) : '?');
        }
        return fallback;
    }
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
    return out;
}

inline std::string windows_error_message(DWORD error) {
    LPWSTR buffer = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    if (len == 0 || !buffer) {
        return "Windows error " + std::to_string(error);
    }
    std::string message = narrow_windows_text(buffer);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
    }
    return message;
}

inline std::wstring quote_windows_arg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    bool needs_quotes = false;
    for (wchar_t ch : arg) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\v' || ch == L'"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return arg;
    }

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(ch);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

inline bool windows_arg_ends_with_case_insensitive(const std::wstring& arg, const wchar_t* suffix) {
    const size_t arg_len = arg.size();
    const size_t suffix_len = std::wcslen(suffix);
    if (arg_len < suffix_len) {
        return false;
    }
    for (size_t i = 0; i < suffix_len; ++i) {
        wchar_t lhs = arg[arg_len - suffix_len + i];
        wchar_t rhs = suffix[i];
        if (lhs >= L'A' && lhs <= L'Z') {
            lhs = static_cast<wchar_t>(lhs - L'A' + L'a');
        }
        if (rhs >= L'A' && rhs <= L'Z') {
            rhs = static_cast<wchar_t>(rhs - L'A' + L'a');
        }
        if (lhs != rhs) {
            return false;
        }
    }
    return true;
}
#endif

inline ProcessResult run_process(const std::vector<std::string>& args, bool capture_output = false) {
    ProcessResult result;
    if (args.empty()) {
        result.start_error = "empty process argument list";
        return result;
    }

#ifdef _WIN32
    (void)capture_output;
    std::vector<std::wstring> wide_args;
    wide_args.reserve(args.size());
    for (const std::string& arg : args) {
        wide_args.push_back(widen_windows_arg(arg));
    }

    std::wstring command_line;
    for (size_t i = 0; i < wide_args.size(); ++i) {
        if (i) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_arg(wide_args[i]);
    }

    std::wstring application_name = wide_args[0];
    if (windows_arg_ends_with_case_insensitive(wide_args[0], L".py")) {
        command_line = quote_windows_arg(L"python") + L" " + command_line;
        application_name.clear();
    } else if (
        windows_arg_ends_with_case_insensitive(wide_args[0], L".cmd") ||
        windows_arg_ends_with_case_insensitive(wide_args[0], L".bat")) {
        const wchar_t* comspec = _wgetenv(L"COMSPEC");
        const std::wstring shell = (comspec && comspec[0] != L'\0') ? comspec : L"cmd.exe";
        command_line = quote_windows_arg(shell) + L" /d /c " + command_line;
        application_name.clear();
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    BOOL ok = CreateProcessW(
        application_name.empty() ? nullptr : application_name.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &startup_info,
        &process_info);
    if (!ok) {
        result.start_error = windows_error_message(GetLastError());
        return result;
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 127;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        result.start_error = windows_error_message(GetLastError());
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    result.exit_code = static_cast<int>(exit_code);
    return result;
#else
    std::string command;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) command.push_back(' ');
        command += shell_quote_arg(args[i]);
    }

    if (!capture_output) {
        const int status = std::system(command.c_str());
        if (status == -1) {
            result.exit_code = 127;
        } else if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else {
            result.exit_code = status;
        }
        return result;
    }

    std::string pop_cmd = command + " 2>&1";
    FILE* pipe = popen(pop_cmd.c_str(), "r");
    if (!pipe) {
        result.start_error = "failed to execute: " + command;
        return result;
    }
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }
    int status = pclose(pipe);
    if (status == -1) {
        result.exit_code = 127;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = status;
    }
    return result;
#endif
}

} // namespace tgfx::internal
