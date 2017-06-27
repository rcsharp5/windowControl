/****************************** Module Header ******************************\
* Module Name:	HookDll.cpp
* Project:		CppHookDll
* Copyright (c) Microsoft Corporation.
* 
* Defines the exported hook procedure.
* 
* This source is subject to the Microsoft Public License.
* See http://www.microsoft.com/opensource/licenses.mspx#Ms-PL.
* All other rights reserved.
* 
* History:
* * 5/01/2009 11:04 PM RongChun Zhang Created
\***************************************************************************/

#pragma comment(lib, "rpcrt4.lib")
#include <windows.h>
#include <atlstr.h>
#define HOOKDLL_EXPORTS
#include "HookDll.h"
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_condition.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <map>
#include <boost/interprocess/containers/string.hpp>
#include <iostream>
#include <thread>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <string>
#include <psapi.h>
#pragma comment( lib, "psapi.lib" )
using namespace boost::interprocess;
using namespace std;
bool hooksActive = false;
static  std::map<HWND, std::string> windowHandles;
static  std::map< std::string, HWND> windowHandlesByString;
static  std::map< std::string, bool> shouldHook;
struct shared_memory_buffer
{
	enum { NumItems = 10 };

	shared_memory_buffer()
		: mutex(1), nempty(NumItems), nstored(0)
	{}

	//Semaphores to protect and synchronize access
	boost::interprocess::interprocess_semaphore
		mutex, nempty, nstored;

	//Items to fill
	int items[NumItems];
};


// Shared data among all instances.
#pragma data_seg(".HOOKDATA")
//Keyboard Hooks
HWND g_hWnd = NULL;	        // Window handle
HHOOK g_hHook = NULL;		// Hook handle

//MouseHooks
HWND g_MhWnd = NULL;	        // Window handle
HHOOK g_MhHook = NULL;		// Hook handle


//MouseHooks LL
HWND g_MLLhWnd = NULL;	        // Window handle
HHOOK g_MLLhHook = NULL;		// Hook handle


bool moving = false;
std::string myUUID;

bool mouseDown = false;

HANDLE gMutex;


#ifndef UNICODE  
typedef std::string String;
#else
typedef std::wstring String;
#endif
// Get module from address
HMODULE WINAPI ModuleFromAddress(PVOID pv) 
{
	MEMORY_BASIC_INFORMATION mbi;
	if (::VirtualQuery(pv, &mbi, sizeof(mbi)) != 0)
	{
		return (HMODULE)mbi.AllocationBase;
	}
	else
	{
		return NULL;
	}
}
HWND mWindow;


rapidjson::Document get_nested(rapidjson::Document &d, std::string key) {
	rapidjson::StringBuffer buffer;
	const char *key_ctr = key.c_str();

	assert(d[key_ctr].IsObject());

	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	d[key_ctr].Accept(writer);

	rapidjson::Document result;
	rapidjson::StringStream s(buffer.GetString());
	result.ParseStream(s);

	return result;
}





std::string getUUID() {
	UUID newId;
	UuidCreate(&newId);
	RPC_CSTR szUuid = NULL;
	if (::UuidToStringA(&newId, &szUuid) == RPC_S_OK)
	{
		return (char*)szUuid;
	}


}




std::string buildFullExport(LPARAM lParam,std::string uuid) {

	LPMOUSEHOOKSTRUCT mhs = (LPMOUSEHOOKSTRUCT)lParam;

	TCHAR className[MAX_PATH];
	GetClassName(mhs->hwnd, className, _countof(className));
	std::wstring arr_w(className);
	std::string cName(arr_w.begin(), arr_w.end());//classNames
	
	//std::string windowName; // The data the user has typed will be stored here
	int length = GetWindowTextLength(mhs->hwnd) + 1;

	char wnd_title[256];
	GetWindowTextA(mhs->hwnd, wnd_title, 256);
	std::string  wtitle = wnd_title;
	
	DWORD pid = 0;
	GetWindowThreadProcessId(mhs->hwnd, &pid);
	std::string pidString = std::to_string(pid);
	
	
	char handleBuff[99] = "";
	sprintf(handleBuff, "%p", mhs->hwnd);
	std::string handleStirng = handleBuff;

	HWND hParent;

	hParent = GetAncestor(mhs->hwnd, GA_PARENT);

	char parentBuff[99] = "";
	sprintf(parentBuff, "%p", hParent);
	std::string parentStirng = parentBuff;


	DWORD dwStyle = (DWORD)GetWindowLong(mhs->hwnd, GWL_STYLE);
	DWORD dwExStyle = (DWORD)GetWindowLong(mhs->hwnd, GWL_EXSTYLE); 
	bool canResize = false;
	if (dwStyle & WS_THICKFRAME || WS_SIZEBOX)
		canResize = true;

	HCURSOR NSCursor = LoadCursor(NULL, IDC_SIZENS);
	HCURSOR NESWCursor = LoadCursor(NULL, IDC_SIZENESW);
	HCURSOR NWSECursor = LoadCursor(NULL, IDC_SIZENWSE);
	HCURSOR WECursor = LoadCursor(NULL, IDC_SIZEWE);
	CURSORINFO ci;
	ci.cbSize = sizeof(ci);
	std::string cursorType = "None";
	if (GetCursorInfo(&ci))
	{
		if (ci.hCursor == NSCursor)
			cursorType = "NS";
		//cout << "Context help cursor active!" << endl; //top bottom
		if (ci.hCursor == NESWCursor)
			cursorType = "NESW";
		//cout << "Context help cursor active!" << endl; //bottom left top right
		if (ci.hCursor == NWSECursor)
			cursorType = "NWSE";
		if (ci.hCursor == WECursor)
			cursorType = "WE";
		//cout << "Context help cursor active!" << endl;//top left bottom right
	}

	RECT rect;
	::GetWindowRect(mhs->hwnd, &rect);
	char NewName[128];
	GetWindowTextA(mhs->hwnd, NewName, 128);
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();               // Between StartObject()/EndObject(), 
	writer.Key("topic");                // output a key,
	writer.String("startMove");
	writer.Key("top");
	writer.String(std::to_string(rect.top).c_str());
	writer.Key("left");
	writer.String(std::to_string(rect.left).c_str());
	writer.Key("bottom");
	writer.String(std::to_string(rect.bottom).c_str());
	writer.Key("right");
	writer.String(std::to_string(rect.right).c_str());
	writer.Key("x");
	writer.String(std::to_string(mhs->pt.x).c_str());
	writer.Key("y");
	writer.String(std::to_string(mhs->pt.y).c_str());
	writer.Key("windowName");
	writer.String(NewName);
	writer.Key("usedHandle");
	writer.String(handleStirng.c_str());
	writer.Key("parentHandle");
	writer.String(parentStirng.c_str());
	writer.Key("uuid");
	writer.String(uuid.c_str());
	writer.Key("className");
	writer.String(handleStirng.c_str());
	writer.String("canResize");
	writer.Bool(canResize);
	writer.String("resizeType");
	writer.String(cursorType.c_str());

	//writer.Key("windowName");
	//writer.String(wtitle.c_str());
	writer.EndObject();
	return s.GetString();
}
std::string buildUpdateExport(LPARAM lParam, std::string uuid) {

	MSLLHOOKSTRUCT *mhs = (MSLLHOOKSTRUCT*)lParam;



	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();               // Between StartObject()/EndObject(), 
	writer.Key("topic");                // output a key,
	writer.String("moveUpdate");
	writer.Key("uuid");
	writer.String(uuid.c_str());
	writer.EndObject();
	return s.GetString();
}
std::string buildEndMoveExport(LPARAM lParam, std::string uuid) {

	MSLLHOOKSTRUCT *mhs = (MSLLHOOKSTRUCT*)lParam;



	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();               // Between StartObject()/EndObject(), 
	writer.Key("topic");                // output a key,
	writer.String("endMovement");
	//writer.Key("x");
	//writer.String(std::to_string(mhs->pt.x).c_str());
	//writer.Key("y");
	//writer.String(std::to_string(mhs->pt.y).c_str());
	writer.Key("uuid");
	writer.String(uuid.c_str());
	//writer.Key("moveUUID");
	//writer.String(getUUID().c_str());
	writer.EndObject();
	return s.GetString();
}

bool lastMove = false;
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (wParam == WM_NCLBUTTONUP || wParam == WM_LBUTTONUP) {
		return ::CallNextHookEx(g_MhHook, nCode, wParam, lParam);
	}
	if (moving ) {
		lastMove = true;
		return true;
	}
	if (lastMove) {
		lastMove = false;
		//OutputDebugString(L"lastmove\n");
		
		//OutputDebugStringA(std::to_string(wParam).c_str());
		return true;
	}
	 
	if (nCode < 0 || nCode == HC_NOREMOVE)
		return ::CallNextHookEx(g_MhHook, nCode, wParam, lParam);
	if (wParam != WM_NCLBUTTONDOWN && !moving) {
		return ::CallNextHookEx(g_MhHook, nCode, wParam, lParam);
	}
	if (gMutex == NULL) {
		gMutex = ::OpenMutex(SYNCHRONIZE, FALSE, L"Global\MUTEX");
	}
	
	if (!hooksActive) {
		DWORD dwait = ::WaitForSingleObject(gMutex, INFINITE);
		//boost::interprocess::named_semaphore semaphore(boost::interprocess::open_only_t(), "moveSem");
		//semaphore.wait();
		typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> CharAllocator;
		typedef boost::interprocess::basic_string<char, std::char_traits<char>, CharAllocator> my_string;
		my_string *s;
		try {
			
			managed_shared_memory managed_shm{ open_only, "enableHooks" };
			s = managed_shm.find_or_construct<my_string>("enableHooks")("false", managed_shm.get_segment_manager());
			std::string st(s->data(), s->size());
			OutputDebugStringA(st.c_str());
			//istringstream(st) >> hooksActive;
			hooksActive = st == "true" ? true : false;
			//CloseHandle(mMutex);
			//semaphore.post();
			ReleaseMutex(gMutex);
		}
		catch (boost::interprocess::bad_alloc &ex)
		{
			ReleaseMutex(gMutex);
			//semaphore.post();
		}
	}
	OutputDebugString(L"\nshould hook\n");
	OutputDebugStringA(std::to_string(hooksActive).c_str());
	OutputDebugString(L"\nshould hook\n");
	if (!hooksActive) {
		return ::CallNextHookEx(g_MhHook, nCode, wParam, lParam);
	}
	
	LPMOUSEHOOKSTRUCT mhs = (LPMOUSEHOOKSTRUCT)lParam;
	char buf[99] = "";

	sprintf(buf, "%p", mhs->hwnd);//Get the hwnd id
	std::string str(buf);

	if (shouldHook.count(str) == 0) {// check to see if the current window is an openfin window

		wchar_t  szBuffer[200 + 1];
		DWORD windowProcessId = 0;
		GetWindowThreadProcessId(mhs->hwnd, &windowProcessId);
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
			PROCESS_VM_READ,
			FALSE, windowProcessId);
		

		GetModuleFileNameEx(hProcess, NULL, szBuffer, 200);
		char str2[201];
		wcstombs(str2, szBuffer, 200);
		std::string exeName = str2;
		bool openfinFound = exeName.find("openfin") != std::string::npos;
		bool hasTitle = str == "";
		shouldHook[str] = true;
		if (openfinFound || hasTitle) {
			shouldHook[str] = false;
		}
		
		
		if (!shouldHook[str]) {
			
			return ::CallNextHookEx(g_MhHook, nCode, wParam, lParam);
		}
	}
	else {
		if(!shouldHook[str]) return ::CallNextHookEx(g_MhHook, nCode, wParam, lParam);
	}



	// Post private messages to Main window
	// wParam specifies the virtual key code
	// lParam specifies the key data
	

	

	std::map<HWND, std::string>::iterator it;

	mWindow = mhs->hwnd;
	it = windowHandles.find(mhs->hwnd);
	if (it == windowHandles.end()) {
		myUUID = getUUID();
		windowHandles[mhs->hwnd] =myUUID;
		windowHandlesByString[myUUID] = mhs->hwnd;
		
	}
	else {
		myUUID = windowHandles[mhs->hwnd];
	}



	std::string exportData = buildFullExport(lParam, myUUID);
	moving = true;
	try {
		shared_memory_object::remove("StartMove");
		managed_shared_memory managed_shm{ open_or_create, "StartMove", 1024 };
		typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> CharAllocator;
		typedef boost::interprocess::basic_string<char, std::char_traits<char>, CharAllocator> my_string;
		my_string *s = managed_shm.find_or_construct<my_string>("StartObject")(exportData.c_str(),managed_shm.get_segment_manager());
	
	}
	catch (boost::interprocess::bad_alloc &ex)
	{
		bool abc = false;
	}

	::SendMessage(g_MhWnd, WM_MOVESTART, wParam, lParam);
	

	//start(1, mhs->hwnd);
	//return true;
	return ::CallNextHookEx(g_MhHook, nCode, wParam, lParam);
}



LRESULT CALLBACK MouseLLProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (!moving) {
		//_execute = false;
		//threadCount = 0;
		return CallNextHookEx(g_MLLhHook, nCode, wParam, lParam);
	}
	MOUSEHOOKSTRUCT * pMouseStruct = (MOUSEHOOKSTRUCT *)lParam;
	//OutputDebugString(L"MouseLoc\n X:");
	//OutputDebugStringA(std::to_string(pMouseStruct->pt.x).c_str());
	//OutputDebugString(L"      Y:");
	//OutputDebugStringA(std::to_string(pMouseStruct->pt.y).c_str());
	//sOutputDebugString(L"\n");
	if (pMouseStruct != NULL) {
		if (wParam == WM_LBUTTONUP)
		{
			std::string endExport = buildEndMoveExport(lParam, myUUID);
			::PostMessage(g_MhWnd, WM_MOVEEND, wParam, lParam);
			moving = false;
			mouseDown = false;
			//_execute = false;
			//threadCount = 0;
			//LoopThread1->join();
		}
		else {
			mouseDown = true;
			if (gMutex == NULL) {
				gMutex = ::OpenMutex(SYNCHRONIZE, FALSE, L"Global\MUTEX");
			}

			
			DWORD dwait = ::WaitForSingleObject(gMutex, INFINITE);
			//boost::interprocess::named_semaphore semaphore(boost::interprocess::open_only_t(), "moveSem");
			//semaphore.wait();
			//OutputDebugString(L"-----working mousellc   --");
			std::string sendUpdate = buildUpdateExport(lParam, myUUID);
			try {
			
				shared_memory_object::remove("UpdateMove");
				managed_shared_memory managed_shm{ open_or_create, "UpdateMove", 1024 };
				typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> CharAllocator;
				typedef boost::interprocess::basic_string<char, std::char_traits<char>, CharAllocator> my_string;
				my_string *s = managed_shm.find_or_construct<my_string>("UpdateObject")(sendUpdate.c_str(), managed_shm.get_segment_manager());
			}
			catch (boost::interprocess::bad_alloc &ex)
			{
				bool abc = false;
			}
			
			::SendMessage(g_MhWnd, WM_MOVEUPDATE, wParam, lParam);
			//semaphore.post();
			ReleaseMutex(gMutex);
			//return true;
		}
	}
	return CallNextHookEx(g_MLLhHook, nCode, wParam, lParam);


	// Post private messages to Main window
	// wParam specifies the virtual key code
	// lParam specifies the key data
	
	
	//return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

// Hook callback
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || nCode == HC_NOREMOVE)
		return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);

    if (lParam & 0x40000000)	// Check the previous key state
	{
		return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);
	}
	
	// Post private messages to Main window
	// wParam specifies the virtual key code
	// lParam specifies the key data
	if (wParam == WM_KEYUP)
	{
		// Post private messages to Main window
		// wParam specifies the virtual key code
		// lParam specifies the scan code
		::PostMessage(g_hWnd, WM_KEYINPUTUP, wParam, lParam);
	}
	else if (wParam == WM_KEYDOWN) {



		::PostMessage(g_hWnd, WM_KEYSTROKE, wParam, lParam);
	}
	
    return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

// Install or uninstall the hook
BOOL WINAPI SetKeyboardHook(BOOL bInstall, DWORD dwThreadId, HWND hWndCaller)
{
	BOOL bOk;
	g_hWnd = hWndCaller;
	
	if (bInstall)
	{
		g_hHook = ::SetWindowsHookEx(WH_KEYBOARD, KeyboardHookProc, 
			ModuleFromAddress(KeyboardHookProc), dwThreadId);
		bOk = (g_hHook != NULL);
	}
	else 
	{
		bOk = ::UnhookWindowsHookEx(g_hHook);
		g_hHook = NULL;
	}
	
	return bOk;
}


// Install or uninstall the hook for global mouse calls
BOOL WINAPI SetMouseHook(BOOL bInstall, DWORD dwThreadId, HWND hWndCaller)
{
	BOOL bOk;
	g_MhWnd = hWndCaller;

	if (bInstall)
	{
		g_MhHook = ::SetWindowsHookEx(WH_MOUSE, MouseProc,
			ModuleFromAddress(MouseProc), dwThreadId);
		g_MLLhHook = ::SetWindowsHookEx(WH_MOUSE_LL, MouseLLProc,ModuleFromAddress(MouseLLProc), dwThreadId);
		bOk = (g_MhHook != NULL);
		bOk = (g_MLLhHook != NULL);
		
	}
	else
	{
		bOk = ::UnhookWindowsHookEx(g_MhHook);
		g_MhHook = NULL;
		bOk = ::UnhookWindowsHookEx(g_MLLhHook);
		g_MLLhHook = NULL;
	}

	return bOk;
}







// Install or uninstall the hook for global mouse calls
BOOL WINAPI SetLLMouseHook(BOOL bInstall, DWORD dwThreadId, HWND hWndCaller)
{
	BOOL bOk;
	g_MhWnd = hWndCaller;

	if (bInstall)
	{
		g_MLLhHook = ::SetWindowsHookEx(WH_MOUSE_LL, MouseLLProc,
			ModuleFromAddress(MouseLLProc), dwThreadId);
		bOk = (g_MLLhHook != NULL);
	}
	else
	{
		bOk = ::UnhookWindowsHookEx(g_MLLhHook);
		g_MLLhHook = NULL;
	}

	return bOk;
}

// Install or uninstall the hook for global mouse calls
void WINAPI MoveCurrentWindow(std::string uuid)
{

	if (uuid == myUUID) {
		::SetWindowPos(mWindow, 0, 1, 1, 100, 100, SWP_NOSIZE);
	}
	

}






std::map<DWORD, bool> keys;//true is down.
// Hook callback
LRESULT CALLBACK LowKeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || nCode == HC_NOREMOVE)
		return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);
	
    if (lParam & 0x40000000)	// Check the previous key state
	{
		return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);
	}

	KBDLLHOOKSTRUCT  *pkbhs = (KBDLLHOOKSTRUCT *)lParam;

	
	//check that the message is from keyboard or is synthesized by SendInput API
	if((pkbhs->flags & LLKHF_INJECTED))
        return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);

	if(wParam == WM_KEYDOWN)
	{
		if (keys.count(pkbhs->vkCode) > 0) {
			if(keys[pkbhs->vkCode])  return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);
		}
		// Post private messages to Main window
		// wParam specifies the virtual key code
		// lParam specifies the scan code
		keys[pkbhs->vkCode] = true;
		::PostMessage(g_hWnd, WM_KEYINPUT, pkbhs->vkCode, pkbhs->scanCode);
	}
	if (wParam == WM_KEYUP)
	{
		if (keys.count(pkbhs->vkCode) > 0) {
			if (keys[pkbhs->vkCode]) keys[pkbhs->vkCode] = false;
		}
		// Post private messages to Main window
		// wParam specifies the virtual key code
		// lParam specifies the scan code
		::PostMessage(g_hWnd, WM_KEYINPUTUP, pkbhs->vkCode, pkbhs->scanCode);
	}
	
    return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

BOOL WINAPI SetLowKeyboardHook(BOOL bInstall, DWORD dwThreadId, HWND hWndCaller)
{
	BOOL bOk;
	g_hWnd = hWndCaller;
	
	if (bInstall)
	{
		g_hHook = ::SetWindowsHookEx(WH_KEYBOARD_LL, LowKeyboardHookProc, 
			ModuleFromAddress(LowKeyboardHookProc), dwThreadId);
		bOk = (g_hHook != NULL);
	}
	else 
	{
		bOk = ::UnhookWindowsHookEx(g_hHook);
		g_hHook = NULL;
	}
	
	return bOk;
}