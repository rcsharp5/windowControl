// CppWindowsHookDlg.h : header file
//

#pragma once
#include <string>


// CCppWindowsHookDlg dialog
class CCppWindowsHookDlg : public CDialog
{
// Construction
public:
	CCppWindowsHookDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	enum { IDD = IDD_CPPWINDOWSHOOK_DIALOG };

	protected:
	afx_msg long OnHookKeyboard(WPARAM wParam, LPARAM lParam);
	afx_msg long OnHookKeyboardUp(WPARAM wParam, LPARAM lParam);
	
	
	long OnHookLowKeyboardUp(WPARAM wParam, LPARAM lParam);

	long OnHookLowKeyboard(WPARAM wParam, LPARAM lParam);
	afx_msg long OnMoveEnd(WPARAM wParam, LPARAM lParam);
	afx_msg long OnMoveRequest(WPARAM wParam, LPARAM lParam);
	afx_msg long OnTest(WPARAM wParam, LPARAM lParam);
	afx_msg void OnWindowPosChanging(WINDOWPOS FAR* lpwndpos);
	
// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	
	void OnDestroy();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedSethook();
	afx_msg void OnBnClickedSethookthread();
	afx_msg void OnBnClickedSethookinput();
	afx_msg void OnBnClickedResettext();
	void setupServer();
	
	void showWindow(std::string windowString);
	void hideWindow(std::string windowString);
};
