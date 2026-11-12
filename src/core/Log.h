#pragma once
#include <windows.h>

// Minimal logger: OutputDebugString + optional append-only file (size-capped).
// The file lives in %ProgramData%\HND Ear Camera\logs, created by the setup with
// write access for Local Service (the Frame Server account).
namespace hndcam
{
    void LogInit(const wchar_t* fileName); // L"source.log" (log directory) or a full path; nullptr = debugger output only
    void LogSetConsole(bool enable);       // also print to stdout (console tools)
    void Log(const wchar_t* format, ...);
    void LogHr(const wchar_t* what, HRESULT hr);
    const wchar_t* LogDirectory();          // %ProgramData%\HND Ear Camera\logs
}
