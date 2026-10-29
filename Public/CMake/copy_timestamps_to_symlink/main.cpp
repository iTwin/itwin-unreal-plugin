/*--------------------------------------------------------------------------------------+
|
|     $Source: main.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "windows.h"
#include <optional>
#include <stdio.h>
#include <string>

// From https://stackoverflow.com/questions/1387064:
DWORD PrintLastErrorFollowing(std::string const& func, std::optional<DWORD> errorMessageID = {})
{
	if (!errorMessageID)
		errorMessageID.emplace(GetLastError());
	if (*errorMessageID == NO_ERROR)
	{
		printf("%s failed with undefined error", func.c_str());
		return -1;
	}
	LPSTR messageBuffer = nullptr;
	//Ask Win32 to give us the string version of that message ID.
	//The parameters we pass in, tell Win32 to create the buffer that holds the message for us
	//(because we don't yet know how long the message string will be).
	size_t size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, *errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
	printf("%s failed with: %s", func.c_str(), messageBuffer);
	LocalFree(messageBuffer);
	return *errorMessageID;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	int nArgs;
	LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	if (NULL == szArglist)
		return PrintLastErrorFollowing("CommandLineToArgvW");
	if (nArgs <= 2)
	{
		printf("Expects exactly two arguments: reference file for the timestamp, then target (link) file to modify.");
		return 1;
	}
	FILETIME ftc, fta, ftw;
	HANDLE hRef = INVALID_HANDLE_VALUE;
	hRef = CreateFileW(szArglist[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (hRef == INVALID_HANDLE_VALUE)
		return PrintLastErrorFollowing("CreateFileW");
	if (hRef != INVALID_HANDLE_VALUE && !GetFileTime(hRef, &ftc, &fta, &ftw))
		return PrintLastErrorFollowing("GetFileTime");
	DWORD ret = NO_ERROR;
	HANDLE hDest = INVALID_HANDLE_VALUE;
	hDest = CreateFileW(szArglist[2], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
						NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (hDest != INVALID_HANDLE_VALUE)
	{
		if (SetFileTime(hDest, &ftc, &fta, &ftw) == TRUE)
		{
			CloseHandle(hDest);
			ret = NO_ERROR;
		}
		else
		{
			ret = GetLastError();
			CloseHandle(hDest);
			return PrintLastErrorFollowing("SetFileTime", ret);
		}
	}
	else
		return PrintLastErrorFollowing("CreateFileW(2)");
	return ret;
}
