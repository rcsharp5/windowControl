/****************************** Module Header ******************************\
* Module Name:	CppWindowsHookDlg.cpp
* Project:		CppWindowsHook
* Copyright (c) Microsoft Corporation.
* 
* This project is the user interface of CppWindowsHook sample. It uses the 
* dialog based MFC application template and move the code for the about 
* dialog.
* 
* This source is subject to the Microsoft Public License.
* See http://www.microsoft.com/opensource/licenses.mspx#Ms-PL.
* All other rights reserved.
* 
* History:
* * 5/01/2009 9:04 PM Rong-Chun Zhang Created
\***************************************************************************/


#include "stdafx.h"
#include "CppWindowsHook.h"
#include "CppWindowsHookDlg.h"
#include <atlstr.h>
#include "../CppHookDll/HookDll.h"
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_condition.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <boost/function.hpp>

#include <psapi.h>
#pragma comment( lib, "psapi.lib" )
#include <Rpcdce.h>
#pragma comment(lib, "rpcrt4.lib")
#include <oleacc.h>
#pragma comment( lib, "oleacc.lib" )

#include <comutil.h>
#pragma comment( lib, "comsuppwd.lib" )

#include <windows.h>
#pragma comment( lib, "User32.lib" )

#include <sddl.h>
#pragma comment( lib, "Advapi32.lib" )
#include <iostream>
#include <chrono> 

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <string>


#include "server_ws.hpp"
#include "client_ws.hpp"

typedef SimpleWeb::SocketServer<SimpleWeb::WS> WsServer;
typedef SimpleWeb::SocketClient<SimpleWeb::WS> WsClient;
using namespace std;
using namespace boost::interprocess;




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

struct WindowHandles
{
	std::string mainHandle;
	std::string parentHandle;
	std::string name;
	bool canResize;
	HWND handle;
	HWND parentHWND;
	bool init = false;
	int l;
};



std::shared_ptr<WsServer::Connection>  socketConnection;
WsServer server;
int const MOVEINTERVAL = 40;
bool shouldParseEvents;// = false;
std::string globalPassedUUID;
std::string globalPassedParentHandle;
HWND globalHandle;
std::string movingWIndowDetails;
std::map< std::string, WindowHandles> windowHandlesByString;//uuid-handleId's

shared_memory_object shm;
shared_memory_buffer * moveData;
bool _moving = false;
std::map< std::string, std::string> windowMaps;
std::map< std::string, WindowHandles> activeWindows;
std::map< std::string, WindowHandles> hiddenWindows;// used to keep track of windows hidden by Finsemble
bool init = false; //so we can skip the first round of hooks.
bool m_visible = false;




#ifndef UNICODE  
typedef std::string String;
#else
typedef std::wstring String;
#endif
// CCppWindowsHookDlg dialog

CCppWindowsHookDlg::CCppWindowsHookDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CCppWindowsHookDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

BEGIN_MESSAGE_MAP(CCppWindowsHookDlg, CDialog)
	ON_MESSAGE(WM_KEYSTROKE, OnHookKeyboard)
	ON_MESSAGE(WM_KEYINPUT, OnHookLowKeyboard)
	//ON_MESSAGE(WM_MOVESTART, OnMoveStart)
	ON_MESSAGE(WM_MOVEEND, OnMoveEnd)
	ON_MESSAGE(WM_MOVEUPDATE, OnMoveRequest)
	ON_MESSAGE(WM_TEST, OnTest)
	ON_MESSAGE(WM_KEYINPUTUP, OnHookLowKeyboardUp)
	ON_WM_WINDOWPOSCHANGING()
	
	
	ON_BN_CLICKED(IDC_SETHOOK, &CCppWindowsHookDlg::OnBnClickedSethook)
	ON_BN_CLICKED(IDC_SETHOOKTHREAD, &CCppWindowsHookDlg::OnBnClickedSethookthread)
	ON_BN_CLICKED(IDC_SETHOOKINPUT, &CCppWindowsHookDlg::OnBnClickedSethookinput)
	ON_BN_CLICKED(IDC_RESETTEXT, &CCppWindowsHookDlg::OnBnClickedResettext)
END_MESSAGE_MAP()


// CCppWindowsHookDlg message handlers



void sendSocketMessage(std::string message) {


	auto send_stream = make_shared<WsServer::SendStream>();
	*send_stream << message;
	if (!socketConnection) return;
	server.send(socketConnection, send_stream, [](const boost::system::error_code& ec) {
		if (ec) {
			cout << "Server: Error sending message. " <<
				//See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
				"Error: " << ec << ", error message: " << ec.message() << endl;
		}
	});
	send_stream.reset();
}


BOOL CALLBACK updateActiveWindows(HWND hWnd, LPARAM lParam) {

	char handleBuff[99] = "";
	sprintf(handleBuff, "%p", hWnd);
	std::string handleString = handleBuff;

	HWND hParent;
	hParent = GetAncestor(hWnd, GA_PARENT);

	char parentBuff[99] = "";
	sprintf(handleBuff, "%p", hParent);
	std::string parentString = parentBuff;

	char wnd_title[256];
	GetWindowTextA(hWnd, wnd_title, 256);
	std::string data = wnd_title;
	int length = GetWindowTextLength(hWnd);
	if (length == 0 ) return true;
	if (!IsWindowVisible(hWnd)) return true; ///
	//OutputDebugString(L"\n name: ");
	//OutputDebugStringA(data.c_str());
	//OutputDebugString(L"\n ");
	WindowHandles h = WindowHandles();
	h.mainHandle = handleString;
	h.parentHandle = parentString;
	h.name = data;
	h.handle = hWnd;
	h.parentHWND = hParent;
	h.l = length;
	h.init = true;
	activeWindows[handleString] = h;


	return true;
}




VOID CALLBACK listenForWindows(HWINEVENTHOOK hoook,DWORD dwEvent, HWND hwnd,LONG oID,LONG idChild,DWORD eventThread,DWORD eventTime )
{

	if (!init) return;
	IAccessible* pAcc = NULL;
	VARIANT varChild;
	if (!IsWindow(hwnd)) return;
	HRESULT hr = AccessibleObjectFromEvent(hwnd, oID, idChild, &pAcc, &varChild);
	if ((hr == S_OK) && (pAcc != NULL))
	{
		HWND parent = GetParent(hwnd);
		bool vis = IsWindowVisible(parent);

		if (dwEvent == EVENT_OBJECT_CREATE) {

		
			char wnd_title[256];
			GetWindowTextA(parent, wnd_title, 256);
			std::string data = wnd_title;
			
			if (data == "" || !vis) return;
			if (windowMaps.count(data) > 0) {
				return;
			}
			char handleBuff[99] = "";
			sprintf(handleBuff, "%p", parent);
			std::string handleString = handleBuff;
			windowMaps[data] = true;


			activeWindows.clear();
			EnumWindows(updateActiveWindows, NULL);
			//std::map< std::string, WindowHandles> aw = activeWindows;


			
			OutputDebugString(L"\nbefore cre handle\n");
			OutputDebugStringA(handleString.c_str());
			OutputDebugString(L"\n");

			OutputDebugStringA(data.c_str());
			OutputDebugString(L"\ncreate\n");
			return;
			//pAcc->Release();
		}
		if (dwEvent == EVENT_OBJECT_DESTROY) {
			activeWindows.clear();
			EnumWindows(updateActiveWindows, NULL);
			//std::map< std::string, WindowHandles> aw = activeWindows;
		}
		pAcc->Release();
		}
	bool abc = false;
}

PSECURITY_DESCRIPTOR MakeAllowAllSecurityDescriptor(void) // Read that you can get permissions issues...We'll see
{
	WCHAR *pszStringSecurityDescriptor;
	//if (GetWindowsVersion(NULL) >= 6)
		pszStringSecurityDescriptor = L"D:(A;;GA;;;WD)(A;;GA;;;AN)S:(ML;;NW;;;ME)";
	//else
		//pszStringSecurityDescriptor = L"D:(A;;GA;;;WD)(A;;GA;;;AN)";

	PSECURITY_DESCRIPTOR pSecDesc;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptor(pszStringSecurityDescriptor, SDDL_REVISION_1, &pSecDesc, NULL))
		return NULL;

	return pSecDesc;
}

HANDLE mMutex;
HANDLE testEvent;
HANDLE waitHandle;

map<std::string, boost::function<void()> > funcs;
void testEventwait() {
	OutputDebugString(L"event fired");
	funcs["start"]();
	//CCppWindowsHookDlg::OnMoveStart();
};


void CCppWindowsHookDlg::OnWindowPosChanging(WINDOWPOS FAR* lpwndpos) {

	if (!m_visible) {
		//lpwndpos->flags &= ~SWP_SHOWWINDOW;
	}
	CDialog::OnWindowPosChanging(lpwndpos);
}
// Hook handler
long CCppWindowsHookDlg::OnHookKeyboard(WPARAM wParam, LPARAM lParam)
{
	CString str;
	GetKeyNameText(lParam, str.GetBuffer(80), 80);
	str.ReleaseBuffer();
	::std::stringstream ss;
	ss << (LPCTSTR)str;


	std::string keyPressed;
	ss >> keyPressed;
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("topic");
	writer.String("keyDown");
	writer.Key("key");
	writer.String(keyPressed.c_str());

	writer.EndObject();

	CString strItem(L"User strike down:" + str + L"\r\n");
	sendSocketMessage(s.GetString());
	// Add key data into the editing box
	CString strEdit;
	GetDlgItem(IDC_MSG)->GetWindowText(strEdit);
	GetDlgItem(IDC_MSG)->SetWindowText(strItem + strEdit);

	return 0;
}
long CCppWindowsHookDlg::OnHookKeyboardUp(WPARAM wParam, LPARAM lParam)
{
	CString str;
	GetKeyNameText(lParam, str.GetBuffer(80), 80);
	str.ReleaseBuffer();

	::std::stringstream ss;
	ss << (LPCTSTR)str;


	std::string keyPressed;
	ss >> keyPressed;
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();               
	writer.Key("topic");             
	writer.String("keyUp");
	writer.Key("key");
	writer.String(keyPressed.c_str());

	writer.EndObject();

	CString strItem(L"User strike up:" + str + L"\r\n");
	sendSocketMessage(s.GetString());
	// Add key data into the editing box
	CString strEdit;
	GetDlgItem(IDC_MSG)->GetWindowText(strEdit);
	GetDlgItem(IDC_MSG)->SetWindowText(strItem + strEdit);

	return 0;
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




BOOL CALLBACK enumWindowsProc( HWND hWnd,LPARAM lParam) {
	
	//get the current handle string
	std::string & uuid =*reinterpret_cast<std::string*>(lParam);//uuid

	char wnd_title[256];
	GetWindowTextA(hWnd, wnd_title, 256);
	std::string  wtitle = wnd_title;



	char handleBuff[99] = "";
	sprintf(handleBuff, "%p", hWnd);
	std::string handleString = handleBuff;
	
	WindowHandles currentHandles = windowHandlesByString[globalPassedUUID];
	HWND hParent;
	hParent = GetAncestor(hWnd, GA_PARENT);


	if (currentHandles.mainHandle == handleString || currentHandles.parentHandle == handleString) {
		OutputDebugString(L"\n");
		
		OutputDebugStringA(wtitle.c_str());
		if (currentHandles.name == "Chrome Legacy Window") {
			OutputDebugString(L"\nfound chrome handle\n");
			OutputDebugStringA(wtitle.c_str());
			OutputDebugString(L"\nfound chrome handle\n");
			windowHandlesByString[globalPassedUUID].handle = hWnd;
		}
		else {
			windowHandlesByString[globalPassedUUID].handle = hWnd;
		}

		
		return false;
	}
	//get the parent handle string
	
	char parentBuff[99] = "";
	sprintf(parentBuff, "%p", hWnd);
	std::string parentString = parentBuff;
	if (currentHandles.mainHandle == parentString || currentHandles.parentHandle == parentString) {
		OutputDebugString(L"\n");
		OutputDebugString(L"found parentStirng handle");
		windowHandlesByString[globalPassedUUID].handle = hParent;
		return false;
	}

	return TRUE;
}




BOOL CALLBACK FindWindowByProcess(HWND hwnd, LPARAM lParam)
{
	// The data the user has typed will be stored here
	//data.resize(GetWindowTextLength(hwnd) + 1, '\0'); // r
	//GetWindowText(hwnd, LPWSTR(data.c_str()), GetWindowTextLength(hwnd) + 1);
	char wnd_title[256];
	GetWindowTextA(hwnd, wnd_title, 256);
	std::string data = wnd_title;
	char handleBuff[99] = "";

	sprintf(handleBuff, "%p", hwnd);
	std::string handleStirng = handleBuff;
	//if (activeClass == handleStirng) {
		//movingWindow = hwnd;
	//	return FALSE;
	//}
	return true;
}


long CCppWindowsHookDlg::OnTest(WPARAM wParam, LPARAM lParam)
{
	OutputDebugStringA("skype here");
	return 0;
}


WindowHandles getWindow(std::string handleString,std::string parentString)
{
	if (activeWindows.count(handleString) > 0)
		return activeWindows[handleString];
	if (activeWindows.count(parentString) > 0)
		return activeWindows[parentString];
	if (hiddenWindows.count(handleString) > 0)
		return hiddenWindows[handleString];
	if (hiddenWindows.count(parentString) > 0)
		return hiddenWindows[parentString];
	return WindowHandles();
}
void parseMoveEvents()
{

	std::thread LoopThread([=]()
	{

		bool parsing = true;



		//boost::interprocess::named_semaphore semaphore(boost::interprocess::open_only_t(), "moveSem");
		while (parsing) {
			CString str;
			if (!shouldParseEvents) {
				parsing = false; //this lets us parse the last event passed
			}


			typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> CharAllocator;
			typedef boost::interprocess::basic_string<char, std::char_traits<char>, CharAllocator> my_string;
			my_string *s;
			std::string json;


			try {
				if (mMutex == NULL) {
					mMutex = ::OpenMutex(SYNCHRONIZE, FALSE, L"Global\MUTEX");
				}
				DWORD dwait = ::WaitForSingleObject(mMutex, INFINITE);

				//semaphore.wait();

				managed_shared_memory managed_shm{ open_only, "UpdateMove" };

				s = managed_shm.find_or_construct<my_string>("UpdateObject")("Hello!", managed_shm.get_segment_manager());
				std::string st(s->data(), s->size());
				json = st;
				if (json == "Hello!") {
					ReleaseMutex(mMutex);
					//semaphore.post();
					std::this_thread::sleep_for(
						std::chrono::milliseconds(MOVEINTERVAL));
					continue;
				}
				str = st.c_str();
				//OutputDebugStringA("statmove\n");
				//	OutputDebugStringA(json.c_str());
				managed_shm.destroy<my_string>("UpdateObject");
				ReleaseMutex(mMutex);
				//semaphore.post();
			}
			catch (interprocess_exception const& ipe)
			{
				std::cerr << "Error: #" << ipe.get_error_code() << ", " << ipe.what() << "\n";
			}
			rapidjson::Document doc;
			doc.Parse(json.c_str());
			//OutputDebugStringA(json.c_str());

			if (doc.Parse(json.c_str()).HasParseError()) {
				auto err = doc.Parse(json.c_str()).GetParseError();
				//cout << "Server: Sending message \"" << d.Parse(json).GetParseError() << "\" to " << (size_t)connection.get() << endl;
				return;
			}

			POINT p;
			if (GetCursorPos(&p)) {

			}
			rapidjson::Value mouseX(std::to_string(p.x).c_str(), doc.GetAllocator());
			rapidjson::Value mouseY(std::to_string(p.y).c_str(), doc.GetAllocator());
			doc.AddMember("x", mouseX, doc.GetAllocator());
			doc.AddMember("y", mouseY, doc.GetAllocator());


			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			doc.Accept(writer);
			std::string output = buffer.GetString();
			sendSocketMessage(output);


			std::this_thread::sleep_for(
				std::chrono::milliseconds(MOVEINTERVAL));

		}

	});
	LoopThread.detach();

}

void OnMoveStart()
{
	//movingWindow = (HWND)wParam;
	//char wnd_title[256];
	//GetWindowTextA(movingWindow, wnd_title, 256);
	//std::string data = wnd_title;
	//LPMOUSEHOOKSTRUCT mhs = (LPMOUSEHOOKSTRUCT)lParam;



	CString str;
	std::string json;
	typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> CharAllocator;
	typedef boost::interprocess::basic_string<char, std::char_traits<char>, CharAllocator> my_string;
	my_string *s;
	try {
		managed_shared_memory managed_shm{ open_only, "StartMove" };
		 s =  managed_shm.find_or_construct<my_string>("StartObject")("Hello!", managed_shm.get_segment_manager());
		 std::string st(s->data(), s->size());
		 str = st.c_str();
		 movingWIndowDetails = st;
		 
		 json = st;
	}
	catch (boost::interprocess::bad_alloc &ex)
	{
		
	}

	rapidjson::Document doc;
	doc.Parse(json.c_str());
	
	globalPassedUUID = doc["uuid"].GetString();
	WindowHandles h;
	 h= getWindow(doc["usedHandle"].GetString(), doc["parentHandle"].GetString());
	if (!h.init) {
		EnumWindows(updateActiveWindows, NULL);
		h = getWindow(doc["usedHandle"].GetString(), doc["parentHandle"].GetString());
	}
	
	h.canResize = doc["canResize"].GetBool();
	activeWindows[h.mainHandle] = h;
	
	rapidjson::Value& u = doc["uuid"];//setthe uuid as the handle
	char uuidbuf[15];
	int len = sprintf(uuidbuf, h.mainHandle.c_str());
	u.SetString(uuidbuf,len, doc.GetAllocator());



	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);
	OutputDebugStringA("\nstartmove UUID:");
	//if (len == 0) {
		OutputDebugStringA(buffer.GetString());
		OutputDebugStringA("\n");
	//}
	sendSocketMessage(buffer.GetString());
	//sendSocketMessage(json);

	_moving = true;
	shouldParseEvents = true;
	parseMoveEvents();

	CString strItem(L"Move started:" +str+ L"\r\n");
	CString strEdit;
	//GetDlgItem(IDC_MSG)->GetWindowText(strEdit);
	//GetDlgItem(IDC_MSG)->SetWindowText(strItem + strEdit);

	return;
}


long CCppWindowsHookDlg::OnMoveEnd(WPARAM wParam, LPARAM lParam)
{
	//CString json = movingWIndowDetails;
	CString str;
	rapidjson::Document doc;
	doc.Parse(movingWIndowDetails.c_str());
	if (doc.Parse(movingWIndowDetails.c_str()).HasParseError()) {
		auto err = doc.Parse(movingWIndowDetails.c_str()).GetParseError();
		//cout << "Server: Sending message \"" << d.Parse(json).GetParseError() << "\" to " << (size_t)connection.get() << endl;
		return 0 ;
	}

	
	if (!doc.HasMember("topic")) return 0;
	rapidjson::Value& s = doc["topic"];
	s.SetString("moveEnd");
	POINT p;
	if (GetCursorPos(&p)) {
		OutputDebugStringA(std::to_string(p.x).c_str());
		OutputDebugStringA(std::to_string(p.y).c_str());
	}
	rapidjson::Value mouseX(std::to_string(p.x).c_str(), doc.GetAllocator());
	rapidjson::Value mouseY(std::to_string(p.y).c_str(), doc.GetAllocator());
	doc.AddMember("x", mouseX, doc.GetAllocator());
	doc.AddMember("y", mouseY, doc.GetAllocator());

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);

	sendSocketMessage(buffer.GetString());
	shouldParseEvents = false;
	_moving = false;
	CString strItem(L"Move Ended:"  +str+ L"\r\n");
	CString strEdit;
	GetDlgItem(IDC_MSG)->GetWindowText(strEdit);
	GetDlgItem(IDC_MSG)->SetWindowText(strItem + strEdit);

	return 0;
}
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
void moveResponse(std::string json) {
	
	rapidjson::Document doc;

	
	doc.Parse(json.c_str());
	if (doc.Parse(json.c_str()).HasParseError()) {
		auto err = doc.Parse(json.c_str()).GetParseError();
		//cout << "Server: Sending message \"" << d.Parse(json).GetParseError() << "\" to " << (size_t)connection.get() << endl;
	}

	
		

		
		bool uuidExists = doc.HasMember("uuid");
		if (!uuidExists) {
			
			return;
		};
		std::string tUUID = doc["uuid"].GetString();


		//char buf[99] = "";
		//sprintf(buf, "%p", hwnd);//Get the hwnd id
		//std::string str(buf);

		std::map<std::string, WindowHandles>::iterator it;

		//it = windowHandlesByString.find(tUUID);
		//if (it == windowHandlesByString.end()) {
			//semaphore.post();
		//	return;

		//}
		WindowHandles wh = getWindow(tUUID, tUUID);

		//WindowHandles wh = windowHandlesByString[tUUID];
		HWND movingHWND = wh.handle;


		
		rapidjson::Document location = get_nested(doc, "location");

		int left = location["left"].GetInt();
		int top = location["top"].GetInt();
		
		//bool posWorked = false;
		if (location["top"].GetInt() < 0) {
		//	OutputDebugString(L"\nless than 0\n");
		}
		//OutputDebugString(L"\nleft --\n");
	std::string t = std::to_string(location["left"].GetInt());
		//OutputDebugStringA(t.c_str());
		//OutputDebugString(L"  RIGHT:");
		std::string r = std::to_string(location["right"].GetInt());
		//OutputDebugStringA(r.c_str());
		//OutputDebugString(L"  width:");
		std::string w = std::to_string(location["width"].GetInt());
		//OutputDebugStringA(w.c_str());
		//OutputDebugString(L"  top:");
		std::string to = std::to_string(location["top"].GetInt());
		//OutputDebugStringA(to.c_str());
		//OutputDebugString(L"  bottom:");
		std::string b = std::to_string(location["bottom"].GetInt());
		//OutputDebugStringA(b.c_str());
			//OutputDebugString(L"\n left ---\n");
			bool cr = wh.canResize;
			bool posWorked;
			//if (wh.canResize) {
			
				 posWorked = ::SetWindowPos(wh.handle,0, location["left"].GetInt(), location["top"].GetInt(), location["width"].GetInt(), location["height"].GetInt(), NULL);
				 //if(!posWorked)::SetWindowPos(wh.parentHWND, 0, location["left"].GetInt(), location["top"].GetInt(), location["width"].GetInt(), location["height"].GetInt(), NULL);
			//}
			//else {
			//	 posWorked = ::SetWindowPos(movingHWND, 0, location["left"].GetInt(), location["top"].GetInt(), location["width"].GetInt(), location["height"].GetInt(), SWP_NOSIZE);
			//}
			//OutputDebugString(L"\nmoved??-----");
			//OutputDebugStringA(std::to_string(posWorked).c_str());
}


long CCppWindowsHookDlg::OnMoveRequest(WPARAM wParam, LPARAM lParam)
{

	return true;
}

void CCppWindowsHookDlg::showWindow(std::string windowString) {

	rapidjson::Document doc;
	doc.Parse(windowString.c_str());
	if (doc.Parse(windowString.c_str()).HasParseError()) {
		auto err = doc.Parse(windowString.c_str()).GetParseError();
		//cout << "Server: Sending message \"" << d.Parse(json).GetParseError() << "\" to " << (size_t)connection.get() << endl;
		return;
	}




	if (!doc.HasMember("uuid")) return;

	WindowHandles wh = getWindow(doc["uuid"].GetString(), doc["uuid"].GetString());
	OutputDebugString(L"\nshow window-----");
	OutputDebugStringA(doc["uuid"].GetString());
	OutputDebugString(L"\nshow window-----");
	OutputDebugStringA(wh.name.c_str());
	OutputDebugString(L"\n endshow window-----");
	
	if (hiddenWindows.count(wh.mainHandle) > 0 && activeWindows.count(wh.mainHandle) == 0) {
		activeWindows[wh.mainHandle] = wh;
		hiddenWindows.erase(wh.mainHandle);
	}
	hiddenWindows[wh.mainHandle] = wh;
	::ShowWindow(wh.handle,5);


}
void CCppWindowsHookDlg::hideWindow(std::string windowString) {
	rapidjson::Document doc;
	doc.Parse(windowString.c_str());
	if (doc.Parse(windowString.c_str()).HasParseError()) {
		auto err = doc.Parse(windowString.c_str()).GetParseError();
		//cout << "Server: Sending message \"" << d.Parse(json).GetParseError() << "\" to " << (size_t)connection.get() << endl;
		return;
	}




	if (!doc.HasMember("uuid")) return;

	WindowHandles wh = getWindow(doc["uuid"].GetString(), doc["uuid"].GetString());
	OutputDebugString(L"\nhide window-----");
	OutputDebugStringA(wh.name.c_str());
	OutputDebugString(L"\n  hideshow window-----");
	hiddenWindows[wh.mainHandle] = wh;
	::ShowWindow(wh.handle, 0);
}





// Hook handler(WH_KEYBOARD_LL)
// wParam specifies the virtual key code
// lParam specifies the scan code
void CCppWindowsHookDlg::OnDestroy() {


	OutputDebugString(L"destroy");
}


long CCppWindowsHookDlg::OnHookLowKeyboard(WPARAM wParam, LPARAM lParam)
{	
	CString str;
	// Convert the virtual key code into a scancode (as required by GetKeyNameText).
	UINT scanCode = MapVirtualKeyEx(wParam, 0, GetKeyboardLayout(0));
    switch(wParam)
    {
		// Certain keys end up being mapped to the number pad by the above function,
		// as their virtual key can be generated by the number pad too.
		// If it's one of the known number-pad duplicates, set the extended bit:
		case VK_INSERT:
		case VK_DELETE:
		case VK_HOME:
		case VK_END:
		case VK_NEXT:  // Page down
		case VK_PRIOR: // Page up
		case VK_LEFT:
		case VK_RIGHT:
		case VK_UP:
		case VK_DOWN:
		  scanCode |= 0x100; // Add extended bit
		  break;
    }      

    // GetKeyNameText() expects the scan code to be on the same format as WM_KEYDOWN
    GetKeyNameText(scanCode << 16, str.GetBuffer(80), 80);
    str.ReleaseBuffer();





	::std::stringstream ss;
	ss << (LPCTSTR)str;


	std::string keyPressed;
	ss >> keyPressed;
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("topic");
	writer.String("keyDown");
	writer.Key("key");
	writer.String(keyPressed.c_str());

	writer.EndObject();

	
	sendSocketMessage(s.GetString());



	CString strItem(L"Keyboard input down:" + str + L"\r\n");

	// Add key data into the editing box
	CString strEdit;
	GetDlgItem(IDC_MSG)->GetWindowText(strEdit);
	GetDlgItem(IDC_MSG)->SetWindowText(strItem + strEdit);

	return 0;
}
long CCppWindowsHookDlg::OnHookLowKeyboardUp(WPARAM wParam, LPARAM lParam)
{
	CString str;
	// Convert the virtual key code into a scancode (as required by GetKeyNameText).
	UINT scanCode = MapVirtualKeyEx(wParam, 0, GetKeyboardLayout(0));
	switch (wParam)
	{
		// Certain keys end up being mapped to the number pad by the above function,
		// as their virtual key can be generated by the number pad too.
		// If it's one of the known number-pad duplicates, set the extended bit:
	case VK_INSERT:
	case VK_DELETE:
	case VK_HOME:
	case VK_END:
	case VK_NEXT:  // Page down
	case VK_PRIOR: // Page up
	case VK_LEFT:
	case VK_RIGHT:
	case VK_UP:
	case VK_DOWN:
		scanCode |= 0x100; // Add extended bit
		break;
	}

	// GetKeyNameText() expects the scan code to be on the same format as WM_KEYDOWN
	GetKeyNameText(scanCode << 16, str.GetBuffer(80), 80);
	str.ReleaseBuffer();


	::std::stringstream ss;
	ss << (LPCTSTR)str;


	std::string keyPressed;
	ss >> keyPressed;
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("topic");
	writer.String("keyUp");
	writer.Key("key");
	writer.String(keyPressed.c_str());

	writer.EndObject();

	sendSocketMessage(s.GetString());

	CString strItem(L"Keyboard input Up:" + str + L"\r\n");

	// Add key data into the editing box
	CString strEdit;
	GetDlgItem(IDC_MSG)->GetWindowText(strEdit);
	GetDlgItem(IDC_MSG)->SetWindowText(strItem + strEdit);

	return 0;
}

// Install or uninstall global keyboard hook
void CCppWindowsHookDlg::OnBnClickedSethook()
{
	CString strButtonText;
	GetDlgItem(IDC_SETHOOK)->GetWindowText(strButtonText);
	if(strButtonText == "Set Hook(Global)")
	{
		// Install Hook
		if (!SetMouseHook(TRUE, 0, m_hWnd))
		{
			AfxMessageBox(L"Fail to Install Hook!");
			return;
		}
		GetDlgItem(IDC_SETHOOK)->SetWindowText(L"Unset Hook(Global)");
		GetDlgItem(IDC_SETHOOKTHREAD)->EnableWindow(FALSE);
		GetDlgItem(IDC_SETHOOKINPUT)->EnableWindow(FALSE);
	}
	else
	{
		if (!SetMouseHook(FALSE))
		{
			AfxMessageBox(L"Fail to Install Hook!");
			return;
		}
        GetDlgItem(IDC_SETHOOK)->SetWindowText(L"Set Hook(Global)");
		GetDlgItem(IDC_SETHOOKTHREAD)->EnableWindow(TRUE);
		GetDlgItem(IDC_SETHOOKINPUT)->EnableWindow(TRUE);
	}
}

// Install or uninstall thread keyboard hook
void CCppWindowsHookDlg::OnBnClickedSethookthread()
{
	CString strButtonText;
	GetDlgItem(IDC_SETHOOKTHREAD)->GetWindowText(strButtonText);
	if(strButtonText == "Set Hook(Thread)")
	{
		// Install Hook
		if(!SetKeyboardHook(TRUE, ::GetCurrentThreadId(), m_hWnd))
		{
			AfxMessageBox(L"Fail to Install Hook!");
			return;
		}
		GetDlgItem(IDC_SETHOOKTHREAD)->SetWindowText(L"Unset Hook(Thread)");
		GetDlgItem(IDC_SETHOOK)->EnableWindow(FALSE);
		GetDlgItem(IDC_SETHOOKINPUT)->EnableWindow(FALSE);
	}
	else
	{
		if(!SetKeyboardHook(FALSE))
		{
			AfxMessageBox(L"Fail to Install Hook!");
			return;
		}
        GetDlgItem(IDC_SETHOOKTHREAD)->SetWindowText(L"Set Hook(Thread)");
		GetDlgItem(IDC_SETHOOK)->EnableWindow(TRUE);
		GetDlgItem(IDC_SETHOOKINPUT)->EnableWindow(TRUE);
	}
}

void CCppWindowsHookDlg::OnBnClickedSethookinput()
{
	CString strButtonText;
	GetDlgItem(IDC_SETHOOKINPUT)->GetWindowText(strButtonText);
	if(strButtonText == "Set Hook(Input)")
	{
		// Install Hook
		if (!SetLowKeyboardHook(TRUE, 0, m_hWnd))
		{
			AfxMessageBox(L"Fail to Install Hook!");
			return;
		}
		GetDlgItem(IDC_SETHOOKINPUT)->SetWindowText(L"Unset Hook(Input)");
		GetDlgItem(IDC_SETHOOKTHREAD)->EnableWindow(FALSE);
		GetDlgItem(IDC_SETHOOK)->EnableWindow(FALSE);
	}
	else
	{
		if (!SetLowKeyboardHook(FALSE))
		{
			AfxMessageBox(L"Fail to Install Hook!");
			return;
		}
        GetDlgItem(IDC_SETHOOKINPUT)->SetWindowText(L"Set Hook(Input)");
		GetDlgItem(IDC_SETHOOKTHREAD)->EnableWindow(TRUE);
		GetDlgItem(IDC_SETHOOK)->EnableWindow(TRUE);
	}
}

void CCppWindowsHookDlg::OnBnClickedResettext()
{
	GetDlgItem(IDC_MSG)->SetWindowText(NULL);
}

BOOL CCppWindowsHookDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetWindowTextW(L"FinSim");
	testEvent = CreateEvent(NULL,        // no security
		FALSE,       // manual-reset event
		FALSE,      // not signaled
		L"testEvent"); // event name
	funcs["start"] = &OnMoveStart;
	RegisterWaitForSingleObject(&waitHandle, testEvent, (WAITORTIMERCALLBACK)testEventwait, NULL, INFINITE, WT_EXECUTEDEFAULT);


	//ChangeWindowMessageFilter(m_hWnd, WM_TEST)
	ChangeWindowMessageFilter(WM_TEST, 1);
	//PSECURITY_DESCRIPTOR pSecDec = MakeAllowAllSecurityDescriptor();
	//SECURITY_ATTRIBUTES secAttr;
	//secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	//secAttr.lpSecurityDescriptor = pSecDec;
	//secAttr.bInheritHandle = FALSE;
	//&secAttr
	mMutex = ::CreateMutex(NULL, TRUE, L"Global\MUTEX");
	ReleaseMutex(mMutex);
	//Set hook for detecting when windows come online
	SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_DESTROY, NULL, listenForWindows, 0, 0, WINEVENT_OUTOFCONTEXT);
	globalHandle = m_hWnd;
	SetLowKeyboardHook(true, 0, m_hWnd);
	//updateActiveWindows()
	init = true;
	EnumWindows(updateActiveWindows, NULL);
	//std::map< std::string, WindowHandles> aw = activeWindows;//delete this
	std::thread server_thread([this]() {
		setupServer();
	});
	server_thread.detach();

	shared_memory_object::remove("RMoveRequest");
	shared_memory_object::remove("UpdateMove");
	shared_memory_object::remove("enableHooks");
	shared_memory_object::remove("StartMove");
	managed_shared_memory managed_shm{ open_or_create, "UpdateMove",999 };
	managed_shared_memory shm(open_or_create, "RMoveRequest", 1024);
	managed_shared_memory managed_shm2{ open_or_create, "enableHooks",999 };

	if (!SetMouseHook(TRUE, 0, globalHandle))
	{
		AfxMessageBox(L"Fail to Install Hook!");

	}
	//LocalFree(mMutex);

	// Set the icon for this dialog.  The framework does this automatically
	// when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon



	return TRUE;
}
void CCppWindowsHookDlg::setupServer()
{
	server.config.port = 8080;
	auto& echo = server.endpoint["^//?$"];

	echo.on_message = [this](std::shared_ptr<WsServer::Connection> connection, std::shared_ptr<WsServer::Message> message) {
		socketConnection = connection;

		auto message_str = message->string();
		const char* json = message_str.c_str();
		rapidjson::Document d;
		d.Parse(json);
		if (d.Parse(json).HasParseError()) {
			auto err = d.Parse(json).GetParseError();
			cout << "Server: Sending message \"" << d.Parse(json).GetParseError() << "\" to " << (size_t)connection.get() << endl;
		}

		bool hasTopic = d.HasMember("topic");
		if (!hasTopic) return;
		rapidjson::Value& s = d["topic"];
		std::string topic = s.GetString();

		if (topic == "moveWindow") {
			moveResponse(message_str);
			//moveWindow(message_str);
			//movewindowtest(movingWindow, message_str);
		}
		else if (topic == "updateWindowList") {
			OutputDebugString(L"window list updates");
			EnumWindows(updateActiveWindows, NULL);
		}
		else if (topic == "getApps") {
			//EnumWindows(EnumWindowsProcMy, 0);
		}
		else if (topic == "showWindow") {
			showWindow(message_str);

		}
		else if (topic == "hideWindow") {
			hideWindow(message_str);

		}
		else if (topic == "sendMovement") {

		}
		else if (topic == "enableHooks") {
			if (mMutex == NULL) {
				mMutex = ::OpenMutex(SYNCHRONIZE, FALSE, L"Global\MUTEX");
			}
			DWORD dwait = ::WaitForSingleObject(mMutex, INFINITE);
			//boost::interprocess::named_semaphore semaphore(boost::interprocess::open_only_t(), "moveSem");
			//semaphore.wait();
		
			typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> CharAllocator;
			typedef boost::interprocess::basic_string<char, std::char_traits<char>, CharAllocator> my_string;
			my_string *s;
			OutputDebugString(L"enable hooks");
			try {
				managed_shared_memory managed_shm{ open_only, "enableHooks" };
				managed_shm.destroy<my_string>("enableHooks");
				s = managed_shm.find_or_construct<my_string>("enableHooks")("true", managed_shm.get_segment_manager());
				
			}
			catch (boost::interprocess::bad_alloc &ex)
			{
				
			}
		//	managed_shared_memory managed_shm{ open_or_create, "enableHooks",1024 };
			//managed_shm.destroy<int>("enableHooks");
			//managed_shm.construct<int>("enableHooks")(1, managed_shm.get_segment_manager());
			ReleaseMutex(mMutex);
			//semaphore.post();
			

		}
		else if (topic == "disableHooks") {

		}
		else if (topic == "addBlackListedWindow") {

		}
		else if (topic == "moveStart") {
			
			bool hasData = d.HasMember("data");
			if (!hasTopic) return;

			auto send_stream = make_shared<WsServer::SendStream>();
			*send_stream << d["data"].GetString();;
			//server.send is an asynchronous function
			server.send(connection, send_stream, [](const boost::system::error_code& ec) {
				if (ec) {
					cout << "Server: Error sending message. " <<
						//See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
						"Error: " << ec << ", error message: " << ec.message() << endl;
				}
			});
		}

	};

	echo.on_open = [](std::shared_ptr<WsServer::Connection> connection) {
		OutputDebugStringW(L"connected");
		auto message_str = "connected";
		rapidjson::StringBuffer s;
		rapidjson::Writer<rapidjson::StringBuffer> writer(s);

		writer.StartObject();               // Between StartObject()/EndObject(), 
		writer.Key("topic");                // output a key,
		writer.String("connected");
		writer.Key("status");                // output a key,
		writer.String("connected");             // follow by a value.
		writer.EndObject();

		auto send_stream = make_shared<WsServer::SendStream>();
		*send_stream << s.GetString();
		server.send(connection, send_stream);
	};

	//See RFC 6455 7.4.1. for status codes
	echo.on_close = [](std::shared_ptr<WsServer::Connection> connection, int status, const std::string& /*reason*/) {
		cout << "Server: Closed connection " << (size_t)connection.get() << " with status code " << status << endl;
	};

	//See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
	echo.on_error = [](std::shared_ptr<WsServer::Connection> connection, const boost::system::error_code& ec) {
		OutputDebugStringW(L"error on error");
		cout << "Server: Error in connection " << (size_t)connection.get() << ". " <<
			"Error: " << ec << ", error message: " << ec.message() << endl;
	};
	
	server.start();
	
}
