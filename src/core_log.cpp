#include "core_log.h"

#include <Windows.h>
#include <DbgHelp.h>
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
		const DWORD code = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
			? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
		const void* address = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
			? exceptionInfo->ExceptionRecord->ExceptionAddress : nullptr;
		const auto moduleBase = reinterpret_cast<uintptr_t>(::GetModuleHandleW(nullptr));
		const auto exceptionAddress = reinterpret_cast<uintptr_t>(address);
		char message[256] = {};
		sprintf_s(message, "UNHANDLED EXCEPTION: code=0x%08lX address=%p rva=0x%llX base=%p thread=%lu", code, address,
			static_cast<unsigned long long>(exceptionAddress - moduleBase), reinterpret_cast<const void*>(moduleBase), ::GetCurrentThreadId());
		WriteLine(message);

		void* stack[32] = {};
		const USHORT stackCount = ::CaptureStackBackTrace(0, static_cast<DWORD>(std::size(stack)), stack, nullptr);
		HMODULE dbgHelp = ::LoadLibraryW(L"DbgHelp.dll");
		using SymInitializeWFunc = BOOL(WINAPI*)(HANDLE, PCWSTR, BOOL);
		using SymFromAddrFunc = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
		using SymCleanupFunc = BOOL(WINAPI*)(HANDLE);
		using SymLoadModuleExWFunc = DWORD64(WINAPI*)(HANDLE, HANDLE, PCWSTR, PCWSTR, DWORD64, DWORD, PMODLOAD_DATA, DWORD);
		auto symInitialize = dbgHelp != nullptr ? reinterpret_cast<SymInitializeWFunc>(::GetProcAddress(dbgHelp, "SymInitializeW")) : nullptr;
		auto symFromAddr = dbgHelp != nullptr ? reinterpret_cast<SymFromAddrFunc>(::GetProcAddress(dbgHelp, "SymFromAddr")) : nullptr;
		auto symCleanup = dbgHelp != nullptr ? reinterpret_cast<SymCleanupFunc>(::GetProcAddress(dbgHelp, "SymCleanup")) : nullptr;
		auto symLoadModuleEx = dbgHelp != nullptr ? reinterpret_cast<SymLoadModuleExWFunc>(::GetProcAddress(dbgHelp, "SymLoadModuleExW")) : nullptr;
		const HANDLE process = ::GetCurrentProcess();
		b8 symbolsAvailable = symInitialize != nullptr && symFromAddr != nullptr && symCleanup != nullptr && symInitialize(process, nullptr, FALSE);
		if (symbolsAvailable && symLoadModuleEx != nullptr)
		{
			wchar_t executablePath[MAX_PATH] = {};
			const DWORD pathLength = ::GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
			if (pathLength > 0 && pathLength < std::size(executablePath))
			{
				const DWORD64 loadedModuleBase = symLoadModuleEx(process, nullptr, executablePath, nullptr, static_cast<DWORD64>(moduleBase), 0, nullptr, 0);
				const DWORD error = loadedModuleBase != 0 ? 0 : ::GetLastError();
				sprintf_s(message, "DBGHELP: module=%p error=%lu", reinterpret_cast<const void*>(loadedModuleBase), error);
				WriteLine(message);
				symbolsAvailable = loadedModuleBase != 0;
			}
			else symbolsAvailable = false;
		}
		else symbolsAvailable = false;
		for (USHORT i = 0; i < stackCount; i++)
		{
			const auto stackAddress = reinterpret_cast<uintptr_t>(stack[i]);
			char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
			auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = MAX_SYM_NAME;
			DWORD64 displacement = 0;
			const b8 resolved = symbolsAvailable && symFromAddr(process, static_cast<DWORD64>(stackAddress), &displacement, symbol);
			sprintf_s(message, "  stack[%u]=%p rva=0x%llX%s%s", i, stack[i], static_cast<unsigned long long>(stackAddress - moduleBase),
				resolved ? " symbol=" : "", resolved ? symbol->Name : "");
			WriteLine(message);
		}
		if (symbolsAvailable) symCleanup(process);
		if (dbgHelp != nullptr) ::FreeLibrary(dbgHelp);
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
