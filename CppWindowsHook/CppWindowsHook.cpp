
// CppWindowsHook.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "CppWindowsHook.h"
#include "CppWindowsHookDlg.h"

#include "../CppHookDll/HookDll.h"
#include <thread>
#include <iostream>
#include <chrono> 
#ifdef _DEBUG
#define new DEBUG_NEW
#endif
using namespace std;

// CCppWindowsHookApp

BEGIN_MESSAGE_MAP(CCppWindowsHookApp, CWinAppEx)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

CCppWindowsHookApp theApp;

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message)                  /* handle the messages */
	{
		if (message == WM_MOVESTART){
			OutputDebugString(L"newmessaage");
		}
		/*EVENT crap*/
	}

	return 0;
}
CCppWindowsHookDlg *m_pMainWnd;
// CCppWindowsHookApp construction
int main() {
	/*INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	//CWinAppEx::InitInstance();
	AfxEnableControlContainer();
	CCppWindowsHookDlg dlg;
	m_pMainWnd = &dlg;
	INT_PTR nResponse = dlg.DoModal();

	//uninstall hook]
	//nResponse.
	delete m_pMainWnd;
	//delete nResponse;
	SetKeyboardHook(FALSE);
	SetLowKeyboardHook(FALSE);
	CCppWindowsHookApp abc =  CCppWindowsHookApp();*/
	HINSTANCE inst = GetModuleHandle(0);
	static const char* class_name = "DUMMY_CLASS";
	WNDCLASSEX wx = {};
	wx.cbSize = sizeof(WNDCLASSEX);
	wx.lpfnWndProc = WndProc;        // function which will handle messages
	wx.hInstance = inst;
	wx.lpszClassName = L"DUMMY_CLASS";
	HWND Handle;
	if (RegisterClassEx(&wx)) {
		 Handle = CreateWindowEx(0, L"DUMMY_CLASS",L"dummy_name", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
	}
	//if (!SetMouseHook(TRUE, 0, Handle))
//	{
	//	AfxMessageBox(L"Fail to Install Hook!");
		//return;
	//}
	while (true) {
		
		std::this_thread::sleep_for(
			std::chrono::milliseconds(4000));
	}
	//CCppWindowsHookApp abc;
	//abc.InitInstance();
	//InitInstance()
	
}
CCppWindowsHookApp::CCppWindowsHookApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}


// The one and only CCppWindowsHookApp object




// CCppWindowsHookApp initialization

BOOL CCppWindowsHookApp::InitInstance()
{
	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	try {

		INITCOMMONCONTROLSEX InitCtrls;
		InitCtrls.dwSize = sizeof(InitCtrls);
		// Set this to include all the common control classes you want to use
		// in your application.
		InitCtrls.dwICC = ICC_WIN95_CLASSES;
		InitCommonControlsEx(&InitCtrls);

		CWinAppEx::InitInstance();

		AfxEnableControlContainer();

		CCppWindowsHookDlg dlg;
	
		m_pMainWnd = &dlg;
		INT_PTR nResponse = dlg.DoModal();

		//uninstall hook]
		//nResponse.
		delete m_pMainWnd;
		//delete nResponse;
		SetKeyboardHook(FALSE);
		SetLowKeyboardHook(FALSE);
	}
	catch (...) {
		OutputDebugString(L"error");
	}
	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}
