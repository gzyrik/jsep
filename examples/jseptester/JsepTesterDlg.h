
// JsepTesterDlg.h : 头文件
//

#pragma once
#include "afxwin.h"
#include <vector>
#include <map>
#include "../jsep/webrtc.h"
// CJsepTesterDlg 对话框
class CJsepTesterDlg : public CDialog, public WebRTC::View, public RTCSocketObserver
{
// 构造
public:
	CJsepTesterDlg(CWnd* pParent = NULL);	// 标准构造函数

// 对话框数据
	enum { IDD = IDD_JSEPTESTER_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 支持
// 实现
protected:
	HICON m_hIcon;
	// 生成的消息映射函数
    virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual BOOL OnInitDialog();
    afx_msg void OnCancel();
    afx_msg void OnOK();
	afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
    CButton m_btnCall;
    CButton m_btnConnect;
    CButton m_btnP2P;
    CEdit   m_lstLog;
    CButton m_btnGetStats;


    int m_padding;
    char m_hostName[1024];
    CStatic m_viewVideo;
    CButton m_btnShareScreen;
    CButton m_btnVideo;
    CButton m_btnLogEvent;
    CButton m_btnDumpAPM;
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnBnClickedClear();
    afx_msg void OnBnClickedGetStats();
    afx_msg void OnBnClickedConnect();
    afx_msg void OnBnClickedP2P();
    afx_msg void OnBnClickedCall();
    afx_msg void OnBnClickedSharescreen();
    afx_msg void OnBnClickedVideo();
    afx_msg void OnBnClickedLogEvent();
    afx_msg void OnBnClickedDumpAPM();
private:
    LRESULT OnZmfVideoEvent(WPARAM wParam, LPARAM lParam);
    LRESULT OnJsetpEvent(WPARAM wParam, LPARAM lParam);
private:
    RTCSocket* m_server;
    std::map<std::string, RTCSocket*> m_clients;
    virtual void JSEP_THIS_CALL OnSocketStateChange(RTCSocket* rs, const char* state);
    virtual void JSEP_THIS_CALL OnSocketIceCandidate(RTCSocket* rs, const char* candidate){}
    virtual void JSEP_THIS_CALL OnSocketMessage(RTCSocket* rs, const char* buffer, int length);
private:
    WebRTC* m_client;
    bool  m_calling;
    std::string m_peershare, m_peervideo, m_localvideo;
    virtual void Trace(const char* format, ...);
    virtual void OnAddStream(const std::string& streamId, const std::string& type);
    virtual void OnRemoveStream(const std::string& streamId, const std::string& type);
    virtual void _QueueUIThreadCallback(int msg_id, char* data);
    virtual void OnClose();
    virtual void OnOpen();
    virtual void OnCall(const std::string& fromId, bool p2pOnly);
    virtual void OnHangup();
    virtual void OnMessage(const std::string& message, const std::string& fromId){
        Trace("%s>>%s", fromId.c_str(), message.c_str());
    }
    virtual void OnStatsReport(const std::string &statsTyep, const std::string &statsId, const std::string &stats, const double timestamp){
        Trace("STATS: %s:%s=%s", statsTyep.c_str(), statsId.c_str(), stats.c_str());
    }
};
