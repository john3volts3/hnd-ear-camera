#include "Log.h"
#include <shlobj.h>
#include <strsafe.h>
#include <cstdarg>
#include <cstdio>

namespace hndcam
{
    namespace
    {
        constexpr LONGLONG MaxLogSize = 1024 * 1024;
        SRWLOCK g_lock = SRWLOCK_INIT;
        wchar_t g_dir[MAX_PATH] = {};
        wchar_t g_file[MAX_PATH] = {};
        bool g_console = false;

        void WriteFileLine(const wchar_t* line)
        {
            if (!g_file[0])
                return;

            HANDLE h = CreateFileW(g_file, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                return;

            LARGE_INTEGER size{};
            if (GetFileSizeEx(h, &size) && size.QuadPart > MaxLogSize)
            {
                // keep things simple: start over when the cap is reached
                CloseHandle(h);
                h = CreateFileW(g_file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h == INVALID_HANDLE_VALUE)
                    return;
            }

            char utf8[2048];
            int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
            if (n > 1)
            {
                DWORD written = 0;
                WriteFile(h, utf8, (DWORD)(n - 1), &written, nullptr);
            }
            CloseHandle(h);
        }
    }

    const wchar_t* LogDirectory()
    {
        if (!g_dir[0])
        {
            PWSTR programData = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData)))
            {
                StringCchPrintfW(g_dir, ARRAYSIZE(g_dir), L"%s\\HND Ear Camera\\logs", programData);
                CoTaskMemFree(programData);
            }
        }
        return g_dir;
    }

    void LogInit(const wchar_t* fileName)
    {
        AcquireSRWLockExclusive(&g_lock);
        g_file[0] = 0;
        if (fileName && wcschr(fileName, L'\\'))
        {
            StringCchCopyW(g_file, ARRAYSIZE(g_file), fileName); // full path
        }
        else if (fileName && LogDirectory()[0])
        {
            StringCchPrintfW(g_file, ARRAYSIZE(g_file), L"%s\\%s", g_dir, fileName);
        }
        ReleaseSRWLockExclusive(&g_lock);
    }

    void LogSetConsole(bool enable)
    {
        g_console = enable;
    }

    void Log(const wchar_t* format, ...)
    {
        wchar_t msg[1536];
        va_list args;
        va_start(args, format);
        StringCchVPrintfW(msg, ARRAYSIZE(msg), format, args);
        va_end(args);

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t line[1792];
        StringCchPrintfW(line, ARRAYSIZE(line), L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu:%5lu] %s\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentProcessId(), GetCurrentThreadId(), msg);

        OutputDebugStringW(line);
        AcquireSRWLockExclusive(&g_lock);
        if (g_console)
            fputws(line, stdout);
        WriteFileLine(line);
        ReleaseSRWLockExclusive(&g_lock);
    }

    void LogHr(const wchar_t* what, HRESULT hr)
    {
        wchar_t text[256] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, (DWORD)hr, 0, text, ARRAYSIZE(text), nullptr);
        // strip trailing CR/LF from the system message
        for (size_t i = wcslen(text); i > 0 && (text[i - 1] == L'\r' || text[i - 1] == L'\n'); i--)
            text[i - 1] = 0;
        Log(L"%s failed: 0x%08X %s", what, (unsigned)hr, text);
    }
}
