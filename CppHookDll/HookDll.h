/****************************** Module Header ******************************\
* Module Name:	HookDll.h
* Project:		CppHookDll
* Copyright (c) Microsoft Corporation.
* 
* This sample exports hook procedure that is used by CppWindowsHook.

* More information exporting data, methods and classes, please see the 
* CppDllExport.
* 
* This source is subject to the Microsoft Public License.
* See http://www.microsoft.com/opensource/licenses.mspx#Ms-PL.
* All other rights reserved.
* 
* History:
* * 5/01/2009 11:04 PM RongChun Zhang Created
\***************************************************************************/

#include <string>
#ifdef HOOKDLL_EXPORTS
#define HOOKDLL_API __declspec(dllexport)
#else
#define HOOKDLL_API __declspec(dllimport)
#endif

// Define a private messages. This message is sent when key is down/up
#define WM_KEYSTROKE (WM_USER + 101)
#define WM_KEYINPUT  (WM_USER + 102)
#define WM_MOVESTART  (WM_USER + 103)
#define WM_MOVEEND  (WM_USER + 104)
#define WM_MOVEUPDATE  (WM_USER + 105)
#define WM_TEST  (WM_USER + 106)
#define WM_KEYINPUTUP  (WM_USER + 107)
//exported functions.
BOOL HOOKDLL_API WINAPI SetKeyboardHook(BOOL bInstall, 
										DWORD dwThreadId = 0, 
										HWND hWndCaller = NULL);

BOOL HOOKDLL_API WINAPI SetLowKeyboardHook(BOOL bInstall, 
										DWORD dwThreadId = 0, 
										HWND hWndCaller = NULL);
BOOL HOOKDLL_API WINAPI SetMouseHook(BOOL bInstall,
	DWORD dwThreadId = 0,
	HWND hWndCaller = NULL);
BOOL HOOKDLL_API WINAPI SetLLMouseHook(BOOL bInstall,
	DWORD dwThreadId = 0,
	HWND hWndCaller = NULL);

void HOOKDLL_API WINAPI MoveCurrentWindow(std::string uuid);

BOOL HOOKDLL_API WINAPI SetMovingStatus(bool m);
