#include "core_log.h"

#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>
#include <exception>
#include <cstring>

namespace
{
	std::mutex LogMutex;
	HANDLE LogFile = INVALID_HANDLE_VALUE;
	std::wstring LogPath;

	void WriteLineUnlocked(cstr text)
	{
		if (LogFile == INVALID_HANDLE_VALUE)
			return;
		DWORD bytesWritten = 0;
		::WriteFile(LogFile, text, static_cast<DWORD>(std::strlen(text)), &bytesWritten, nullptr);
		::WriteFile(LogFile, "\r\n", 2, &bytesWritten, nullptr);
	}

	void WriteLine(cstr text)
	{
		std::lock_guard lock(LogMutex);
		WriteLineUnlocked(text);
	}

	LONG WINAPI PeepoUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
	{
		const EXCEPTION_RECORD* record = exceptionInfo != nullptr ? exceptionInfo->ExceptionRecord : nullptr;
		const DWORD code = record != nullptr ? record->ExceptionCode : 0;
		const void* address = record != nullptr ? record->ExceptionAddress : nullptr;
		const auto moduleBase = reinterpret_cast<uintptr_t>(::GetModuleHandleW(nullptr));
		const auto exceptionAddress = reinterpret_cast<uintptr_t>(address);
		const auto relativeAddress = exceptionAddress >= moduleBase ? exceptionAddress - moduleBase : 0;
		const ULONG_PTR accessType = record != nullptr && record->NumberParameters >= 1 ? record->ExceptionInformation[0] : 0;
		const ULONG_PTR accessAddress = record != nullptr && record->NumberParameters >= 2 ? record->ExceptionInformation[1] : 0;
		char message[512] = {};
		sprintf_s(message, "UNHANDLED EXCEPTION: code=0x%08lX address=%p module=%p rva=0x%llX access=%llu target=%p thread=%lu",
			code, address, reinterpret_cast<void*>(moduleBase), static_cast<unsigned long long>(relativeAddress),
			static_cast<unsigned long long>(accessType), reinterpret_cast<void*>(accessAddress), ::GetCurrentThreadId());
		WriteLine(message);
#if defined(_M_X64)
		if (exceptionInfo != nullptr && exceptionInfo->ContextRecord != nullptr)
		{
			const CONTEXT& context = *exceptionInfo->ContextRecord;
			sprintf_s(message, "REGISTERS: rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX r14=%016llX rip=%016llX",
				context.Rax, context.Rbx, context.Rcx, context.Rdx, context.R14, context.Rip);
			WriteLine(message);
		}
#endif
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void TerminateHandler()
	{
		WriteLine("std::terminate() called");
		std::abort();
	}
}

namespace Log
{
	void Initialize()
	{
		std::lock_guard lock(LogMutex);
		if (LogFile != INVALID_HANDLE_VALUE)
			return;

		wchar_t executablePath[MAX_PATH] = {};
		const DWORD length = ::GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
		if (length == 0 || length >= std::size(executablePath))
			return;
		LogPath.assign(executablePath, length);
		const size_t separator = LogPath.find_last_of(L"\\/");
		LogPath.resize(separator == std::wstring::npos ? 0 : separator + 1);
		LogPath += L"peepo_drum_kit.log";
		LogFile = ::CreateFileW(LogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (LogFile != INVALID_HANDLE_VALUE)
			WriteLineUnlocked("--- application start ---");
	}

	void Shutdown()
	{
		std::lock_guard lock(LogMutex);
		if (LogFile != INVALID_HANDLE_VALUE)
		{
			WriteLineUnlocked("--- application shutdown ---");
			::CloseHandle(LogFile);
			LogFile = INVALID_HANDLE_VALUE;
		}
	}

	void Write(cstr format, ...)
	{
		char message[2048] = {};
		va_list args;
		va_start(args, format);
		vsprintf_s(message, format, args);
		va_end(args);
		WriteLine(message);
	}

	void WriteHRESULT(cstr operation, long result)
	{
		Write("%s: HRESULT=0x%08lX", operation, static_cast<unsigned long>(result));
	}

	void InstallCrashHandler()
	{
		::SetUnhandledExceptionFilter(PeepoUnhandledExceptionFilter);
		std::set_terminate(TerminateHandler);
	}
}
