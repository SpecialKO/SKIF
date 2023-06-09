// Below sections of codes are from the reset utility of Custom Resolution Utility (CRU)
// https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU

/**************************************************************************

Copyright (C) 2013-2021 ToastyX

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

 **************************************************************************/

/* Includes ***************************************************************/

#include "utility/drvreset.h"

/* Defines ****************************************************************/

#define MAX_WINDOWS 10000

/* Locals *****************************************************************/

static int window_count;
static HWND window_list[MAX_WINDOWS];
static WINDOWPLACEMENT window_placement[MAX_WINDOWS];

/* Functions **************************************************************/

static BOOL CALLBACK EnumWindowsProc(HWND window, LPARAM lParam)
{
  UNREFERENCED_PARAMETER(lParam);

	if (window_count >= MAX_WINDOWS)
		return FALSE;

	if (IsWindowVisible(window))
	{
		window_list[window_count] = window;
		window_placement[window_count].length = sizeof window_placement[window_count];
		GetWindowPlacement(window, &window_placement[window_count]);

		switch (window_placement[window_count].showCmd)
		{
			case SW_SHOWNORMAL:
				GetWindowRect(window, &window_placement[window_count].rcNormalPosition);
				break;

			case SW_SHOWMAXIMIZED:
				ShowWindow(window, SW_SHOWNORMAL);
				break;
		}

		window_placement[window_count].flags |= WPF_ASYNCWINDOWPLACEMENT;
		window_count++;
	}

	return TRUE;
}

/**************************************************************************/

void SaveWindows()
{
	window_count = 0;
	EnumWindows(EnumWindowsProc, 0);
}

/**************************************************************************/

static BOOL SetWindowRect(HWND window, RECT rect, UINT flags)
{
	return SetWindowPos(window, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, flags);
}

/**************************************************************************/

void RestoreWindows()
{
	int index;

	for (index = 0; index < window_count; index++)
	{
		if (window_placement[index].showCmd == SW_SHOWNORMAL)
			SetWindowRect(window_list[index], window_placement[index].rcNormalPosition, SWP_ASYNCWINDOWPOS);
		else
			SetWindowPlacement(window_list[index], &window_placement[index]);
	}
}

/**************************************************************************/

int KillProcess(LPCTSTR name)
{
	HANDLE processes;
	PROCESSENTRY32 process_entry;
	HANDLE process;
	int result = 0;

	processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (processes == INVALID_HANDLE_VALUE)
		return 0;

	process_entry.dwSize = sizeof process_entry;

	if (Process32First(processes, &process_entry))
	{
		do
		{
			if (_tcsicmp(process_entry.szExeFile, name) == 0)
			{
				process = OpenProcess(PROCESS_TERMINATE, FALSE, process_entry.th32ProcessID);

				if (process)
				{
					TerminateProcess(process, 0);
					CloseHandle(process);
					result++;
				}
			}
		}
		while (Process32Next(processes, &process_entry));
	}

	CloseHandle(processes);
	return result;
}

/**************************************************************************/

static BOOL SetProcessTokenPrivilege(LPCTSTR name, DWORD attributes)
{
	TOKEN_PRIVILEGES token_privileges;
	HANDLE process_token;

	token_privileges.PrivilegeCount = 1;
	token_privileges.Privileges[0].Attributes = attributes;

	if (!LookupPrivilegeValue(NULL, name, &token_privileges.Privileges[0].Luid))
		return FALSE;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &process_token))
		return FALSE;

	if (!AdjustTokenPrivileges(process_token, FALSE, &token_privileges, 0, NULL, NULL))
	{
		CloseHandle(process_token);
		return FALSE;
	}

	CloseHandle(process_token);
	return TRUE;
}

/**************************************************************************/

static HANDLE GetUserToken()
{
	HWND shell_window;
	DWORD process_id;
	HANDLE process_handle;
	HANDLE process_token;
	HANDLE user_token;

	if (!SetProcessTokenPrivilege(SE_INCREASE_QUOTA_NAME, SE_PRIVILEGE_ENABLED))
		return NULL;

	shell_window = GetShellWindow();

	if (!shell_window)
		return NULL;

	GetWindowThreadProcessId(shell_window, &process_id);

	if (!process_id)
		return NULL;

	process_handle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id);

	if (!process_handle)
		return NULL;

	if (!OpenProcessToken(process_handle, TOKEN_DUPLICATE, &process_token))
	{
		CloseHandle(process_handle);
		return NULL;
	}

	if (!DuplicateTokenEx(process_token, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID, NULL, SecurityImpersonation, TokenPrimary, &user_token))
		user_token = NULL;

	CloseHandle(process_handle);
	CloseHandle(process_token);
	return user_token;
}

/**************************************************************************/

BOOL RunAsUser(LPWSTR command)
{
	HANDLE user_token;
	STARTUPINFOW startup_info;
	PROCESS_INFORMATION process_info;

	user_token = GetUserToken();

	if (!user_token)
		return FALSE;

	GetStartupInfoW(&startup_info);

	if (!CreateProcessWithTokenW(user_token, 0, NULL, command, 0, NULL, NULL, &startup_info, &process_info))
	{
		CloseHandle(user_token);
		return FALSE;
	}

	CloseHandle(user_token);
	CloseHandle(process_info.hProcess);
	CloseHandle(process_info.hThread);
	return TRUE;
}

/**************************************************************************/

BOOL RefreshNotifyWindow(HWND window)
{
	RECT rect;
	int x, y;

	GetClientRect(window, &rect);

	for (y = 0; y < rect.bottom; y += 4)
		for (x = 0; x < rect.right; x += 4)
			PostMessage(window, WM_MOUSEMOVE, 0, (y << 16) | (x & 65535));

	return TRUE;
}

/**************************************************************************/

BOOL RefreshNotifyIcons()
{
	HWND parent;
	HWND window;

	parent = FindWindow(_T("Shell_TrayWnd"), NULL);

	if (!parent)
		return FALSE;

	parent = FindWindowEx(parent, NULL, _T("TrayNotifyWnd"), NULL);

	if (!parent)
		return FALSE;

	parent = FindWindowEx(parent, NULL, _T("SysPager"), NULL);

	if (!parent)
		return FALSE;

	window = FindWindowEx(parent, NULL, _T("ToolbarWindow32"), _T("Notification Area"));

	if (window)
		RefreshNotifyWindow(window);

	window = FindWindowEx(parent, NULL, _T("ToolbarWindow32"), _T("User Promoted Notification Area"));

	if (window)
		RefreshNotifyWindow(window);

	parent = FindWindow(_T("NotifyIconOverflowWindow"), NULL);

	if (parent)
	{
		window = FindWindowEx(parent, NULL, _T("ToolbarWindow32"), _T("Overflow Notification Area"));

		if (window)
			RefreshNotifyWindow(window);
	}

	return TRUE;
}

/**************************************************************************/

BOOL StopCCC()
{
	KillProcess(_T("MOM.exe"));
	KillProcess(_T("CCC.exe"));
	return TRUE;
}

/**************************************************************************/

BOOL StartCCC()
{
	WCHAR command[] = L"CLI.exe start";
	LPCTSTR program_files;

	program_files = _tgetenv(_T("ProgramFiles(x86)"));

	if (!program_files)
	{
		program_files = _tgetenv(_T("ProgramFiles"));

		if (!program_files)
			return FALSE;
	}

	if (_tchdir(program_files) != 0)
		return FALSE;

	if (_tchdir(_T("AMD\\ATI.ACE\\Core-Static")) != 0)
		if (_tchdir(_T("ATI Technologies\\ATI.ACE\\Core-Static")) != 0)
			return FALSE;

	if (!RunAsUser(command))
		return FALSE;

	return TRUE;
}

/**************************************************************************/

BOOL StopRadeonSettings()
{
	KillProcess(_T("RadeonSoftware.exe"));
	KillProcess(_T("RadeonSettings.exe"));
	KillProcess(_T("cnext.exe"));
	return TRUE;
}

/**************************************************************************/

BOOL StartRadeonSettings()
{
	WCHAR command[] = L"cncmd.exe restart";
	LPCTSTR program_files;

	program_files = _tgetenv(_T("ProgramFiles"));

	if (!program_files)
		return FALSE;

	if (_tchdir(program_files) != 0)
		return FALSE;

	if (_tchdir(_T("AMD\\CNext\\CNext")) != 0)
		return FALSE;

	if (!RunAsUser(command))
		return FALSE;

	return TRUE;
}

/**************************************************************************/

BOOL FixTaskbar()
{
	KillProcess(_T("ShellExperienceHost.exe"));
	KillProcess(_T("SearchUI.exe"));
	return TRUE;
}

/**************************************************************************/

int SetDriverState(DWORD state)
{
	HDEVINFO devices;
	DWORD index;
	SP_DEVINFO_DATA device;
	SP_PROPCHANGE_PARAMS params;
	int result = 0;

	devices = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT);

	if (devices == INVALID_HANDLE_VALUE)
		return 0;

	for (index = 0; device.cbSize = sizeof device, SetupDiEnumDeviceInfo(devices, index, &device); index++)
	{
		params.ClassInstallHeader.cbSize = sizeof params.ClassInstallHeader;
		params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
		params.StateChange = state;
		params.Scope = DICS_FLAG_GLOBAL;
		params.HwProfile = 0;

		if (SetupDiSetClassInstallParams(devices, &device, &params.ClassInstallHeader, sizeof params))
			if (SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devices, &device))
				result++;
	}

	SetupDiDestroyDeviceInfoList(devices);
	return result;
}

/**************************************************************************/

BOOL StopDriver()
{
	SaveWindows();

	if (!SetDriverState(DICS_DISABLE))
		return FALSE;

	StopCCC();
	StopRadeonSettings();
	Sleep(100);
	RefreshNotifyIcons();
	return TRUE;
}

/**************************************************************************/

BOOL StartDriver()
{
	if (!SetDriverState(DICS_ENABLE))
		return FALSE;

	FixTaskbar();
	Sleep(3500);
	RestoreWindows();
	StartRadeonSettings();
	Sleep(100);
	return TRUE;
}

/**************************************************************************/

BOOL RestartDriver()
{
	if (!StopDriver())
		return FALSE;

	if (!StartDriver())
		return FALSE;

	return TRUE;
}

/**************************************************************************/