
// JsepTesterDlg.cpp : 实现文件
//

#include "stdafx.h"
#include "JsepTester.h"
#include "JsepTesterDlg.h"
#include "afxdialogex.h"
#include <sstream>
#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CJsepTesterDlg 对话框
#define  MESSAGE_JSEP_EVENT WM_USER+10
CJsepTesterDlg::CJsepTesterDlg(CWnd* pParent /*=NULL*/)
: CDialog(CJsepTesterDlg::IDD, pParent), m_padding(0), m_client(0), m_calling(false){
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CJsepTesterDlg::DoDataExchange(CDataExchange* pDX){
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_CALL, m_btnCall);
    DDX_Control(pDX, IDC_CONNECT, m_btnConnect);
    DDX_Control(pDX, IDC_P2P, m_btnP2P);
    DDX_Control(pDX, IDC_LOG, m_lstLog);
    DDX_Control(pDX, IDC_VIEW_VIDEO, m_viewVideo);
    DDX_Control(pDX, IDC_GETSTATS, m_btnGetStats);
    DDX_Control(pDX, IDC_SHARESCREEN, m_btnShareScreen);
    DDX_Control(pDX, IDC_VIDEO, m_btnVideo);
    DDX_Control(pDX, IDC_LOG_EVENT, m_btnLogEvent);
    DDX_Control(pDX, IDC_DUMP_APM, m_btnDumpAPM);
}

BEGIN_MESSAGE_MAP(CJsepTesterDlg, CDialog)
	ON_WM_PAINT()
    ON_WM_TIMER()
    ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(IDC_CLEAR, &CJsepTesterDlg::OnBnClickedClear)
    ON_BN_CLICKED(IDC_GETSTATS, &CJsepTesterDlg::OnBnClickedGetStats)
    ON_BN_CLICKED(IDC_CONNECT, &CJsepTesterDlg::OnBnClickedConnect)
    ON_BN_CLICKED(IDC_P2P, &CJsepTesterDlg::OnBnClickedP2P)
    ON_BN_CLICKED(IDC_CALL, &CJsepTesterDlg::OnBnClickedCall)
    ON_WM_SIZE()
    ON_BN_CLICKED(IDC_SHARESCREEN, &CJsepTesterDlg::OnBnClickedSharescreen)
    ON_BN_CLICKED(IDC_VIDEO, &CJsepTesterDlg::OnBnClickedVideo)
    ON_BN_CLICKED(IDC_LOG_EVENT, &CJsepTesterDlg::OnBnClickedLogEvent)
    ON_BN_CLICKED(IDC_DUMP_APM, &CJsepTesterDlg::OnBnClickedDumpAPM)
    ON_MESSAGE(ZmfVideoEvent, &CJsepTesterDlg::OnZmfVideoEvent)
    ON_MESSAGE(MESSAGE_JSEP_EVENT, &CJsepTesterDlg::OnJsetpEvent)
END_MESSAGE_MAP()


// CJsepTesterDlg 消息处理程序
BOOL CJsepTesterDlg::OnInitDialog(){
	CDialog::OnInitDialog();

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标
    HANDLE f = ::CreateFileW(L"stderr.log",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, NULL, NULL);
    SetStdHandle(STD_ERROR_HANDLE, f);
	// TODO:  在此添加额外的初始化代码
    SetDlgItemText(IDC_WS, TEXT("ws://192.168.0.240:7000"));
    SetDlgItemText(IDC_STUN, TEXT("stun:test@115.29.4.150:3478"));
    SetDlgItemText(IDC_STUN_PWD, TEXT("test"));
    m_btnCall.EnableWindow(FALSE);
    m_btnGetStats.EnableWindow(FALSE);
    m_btnP2P.EnableWindow(FALSE);
    m_btnShareScreen.EnableWindow(FALSE);
    m_btnLogEvent.EnableWindow(FALSE);
    m_btnDumpAPM.EnableWindow(FALSE);
    m_btnVideo.EnableWindow(FALSE);
    m_btnVideo.SetCheck(1);
    Zmf_VideoInitialize((void*)this->GetSafeHwnd());
    Zmf_AudioInitialize(0);
    DWORD len = sizeof(m_hostName);
    GetUserNameA(m_hostName, &len);
    SetDlgItemText(IDC_UID, CString(m_hostName));
    RECT rect;
    GetClientRect(&rect);
    m_padding = rect.bottom;
    m_lstLog.GetWindowRect(&rect);
    ScreenToClient(&rect);
    m_padding -= rect.bottom;
    UpdateData();
    std::ostringstream oss;
    oss << "{\"protocols\":\"turn,chat,p2p\""
        //<< ",\"ca_certs\":\"cert.pem\"" // CA 证书, 用于验证对方的证书
        << ",\"certfile\":\"usercert.pem\"" //  自身证书
        << ",\"keyfile\":\"userkey.pem\"" // 自身私钥
        << ",\"cert_reqs\":0" //不要求客户端提供证书
        //<< ",\"ipv6\":true"
        << "}";
    m_server = WebSocket_Listen("ws://*:7000", oss.str().c_str(), this, nullptr);
    if (m_server) Trace("--- Listen *:7000 ---");
	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}
void CJsepTesterDlg::OnSize(UINT nType, int cx, int cy){
    CDialog::OnSize(nType, cx, cy);
    if (m_padding == 0) return;
    RECT rect;
    m_lstLog.GetWindowRect(&rect);
    ScreenToClient(&rect);
    rect.bottom = cy - m_padding;
    m_lstLog.MoveWindow(&rect);
    m_viewVideo.GetWindowRect(&rect);
    ScreenToClient(&rect);
    rect.right = cx - m_padding;
    rect.bottom = cy - m_padding;
    m_viewVideo.MoveWindow(&rect);
}
void CJsepTesterDlg::OnCancel(){
    OnClose();
    Zmf_AudioTerminate();
    Zmf_VideoTerminate();
    RTCSocket_Close(m_server);
    m_server = nullptr;
    for (auto pair : m_clients) RTCSocket_Close(pair.second);
    CDialog::OnCancel();
}
void CJsepTesterDlg::OnOK() {
    if (GetDlgItem(IDC_MSG) == GetFocus()){
        CString text;
        GetDlgItemText(IDC_MSG, text);
        if (text.GetLength() == 0) return;
        CString toId;
        GetDlgItemText(IDC_TOID, toId);
        if (toId.GetLength() == 0)
            return Trace("invalid MSG or TO");
        if (m_client)
            m_client->Send((std::string)CT2A(text, CP_UTF8), (std::string)CT2A(toId, CP_UTF8));
    }
}
HCURSOR CJsepTesterDlg::OnQueryDragIcon(){
    return static_cast<HCURSOR>(m_hIcon);
}
void CJsepTesterDlg::OnPaint(){
    if (IsIconic()){
        CPaintDC dc(this); // 用于绘制的设备上下文

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // 使图标在工作区矩形中居中
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // 绘制图标
        dc.DrawIcon(x, y, m_hIcon);
    }
    else{
        CDialog::OnPaint();
    }
}
void CJsepTesterDlg::OnBnClickedClear(){
    m_lstLog.SetWindowTextW(L"");
}
void CJsepTesterDlg::OnStatsReport(const std::string &statsTyep, const std::string &statsId, const std::string &stats, const double timestamp) {
    Trace("%s: %s:%s=%s", m_statsType.c_str(), statsTyep.c_str(), statsId.c_str(), stats.c_str());
}
void CJsepTesterDlg::OnBnClickedGetStats(){
    if (m_client) {
        static size_t statsCount = 0;
        if (statsCount < m_streams.size()) {
            std::map<std::string, std::string>::iterator iter = m_streams.begin();
            for (size_t i = 0; i < statsCount; ++i) iter++;
            m_client->GetStats(iter->first, WebRTC::Audio | WebRTC::Video);
            m_statsType = iter->second;
            statsCount++;
        }
        else {
            m_client->GetStats("", WebRTC::Audio | WebRTC::Video);
            m_statsType = "STATS";
            statsCount = 0;
        }  
    }
}
void CJsepTesterDlg::OnBnClickedConnect(){
    if (m_client) return OnClose();
    CString uId;
    GetDlgItemText(IDC_UID, uId);
    CString url;
    GetDlgItemText(IDC_WS, url);
    if (uId.GetLength() == 0 || url.GetLength() == 0)
        return Trace("invalid WS or ID");

    m_client = WebRTC::Connect(*this, (std::string)CT2A(url, CP_UTF8), (std::string)CT2A(uId, CP_UTF8));
    if (!m_client) return Trace("Connect failed!");
    m_btnConnect.EnableWindow(FALSE);
    HWND hwnd;
    GetDlgItem(IDC_WS, &hwnd);
    ::EnableWindow(hwnd, FALSE);
    GetDlgItem(IDC_UID, &hwnd);
    ::EnableWindow(hwnd, FALSE);
    UpdateData();
}
void CJsepTesterDlg::OnBnClickedP2P(){
    if (m_calling)
        OnHangup();
    else{
        CString toId;
        GetDlgItemText(IDC_TOID, toId);
        if (toId.GetLength() == 0)
            return Trace("invalid TO");
        OnCall((std::string)CT2A(toId, CP_UTF8), true);
    }
}
void CJsepTesterDlg::OnBnClickedCall(){
    if (m_calling)
        OnHangup();
    else{
        CString toId;
        GetDlgItemText(IDC_TOID, toId);
        if (toId.GetLength() == 0)
            return Trace("invalid TO");
        OnCall((std::string)CT2A(toId, CP_UTF8), false);
    }
}
void CJsepTesterDlg::OnBnClickedSharescreen(){
    UpdateData(FALSE);
    if (m_client) m_client->Share(m_btnShareScreen.GetCheck() != 0);
}
void CJsepTesterDlg::OnBnClickedLogEvent()
{
    UpdateData(FALSE);
    if (m_client) m_client->LogRtcEvent(m_btnLogEvent.GetCheck() != 0 ? "event.log":"");
}
void CJsepTesterDlg::OnBnClickedDumpAPM()
{
    UpdateData(FALSE);
    if (m_client) m_client->DumpAudioProcessing(m_btnDumpAPM.GetCheck() != 0 ? "audio.log" : "");
}
void CJsepTesterDlg::OnBnClickedVideo() {
    UpdateData(FALSE);
    if (m_client) m_client->Update(m_btnVideo.GetCheck() != 0);
}
BOOL CJsepTesterDlg::PreTranslateMessage(MSG* pMsg){
    if (pMsg->message == WM_KEYDOWN && m_client && '0' <= pMsg->wParam && pMsg->wParam <= '9'){
        char ch[2] = { (char)pMsg->wParam, '\0' };
        m_client->InsertDtmf(ch, 100, 60);
    }
    return CDialog::PreTranslateMessage(pMsg);
}
//////////////////////////////////////////////////////////////////////////
LRESULT CJsepTesterDlg::OnJsetpEvent(WPARAM wParam, LPARAM lParam){
    if (m_client)
        m_client->_UIThreadCallback(wParam, (char*)lParam);
    else
        free((void*)lParam);
    return 0;
}
LRESULT CJsepTesterDlg::OnZmfVideoEvent(WPARAM wParam, LPARAM lParam){
    if (wParam == ZmfVideoRenderDidReceive || wParam == ZmfVideoRenderDidResize) {
        std::ostringstream oss;
        {
            auto& token = JsonValue::parse((const char*)lParam);
            auto& json = token[0];
            if (json[ZmfSourceType] == "1")
                oss << "local ";
            else
                oss << (std::string)json[ZmfRender] << " remote ";
            oss << (std::string)json[ZmfWidth] << " x " << (std::string)json[ZmfHeight];
        }
        Trace(oss.str().c_str());
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
void CJsepTesterDlg::Trace(const char* format, ...){
    char log[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(log, sizeof(log), format, args);
    va_end(args);
    CString str(CA2T(log, CP_UTF8));
    CString oldstr;
    m_lstLog.GetWindowText(oldstr);
    str.Append(TEXT("\r\n"));
    str.Append(oldstr);
    m_lstLog.SetWindowText(str);
}
void CJsepTesterDlg::OnAddStream(const std::string& streamId, const std::string& type){
    if (type != "previewvideo" && type != "previewshare")
        m_streams[streamId] = type;

    if (type == "previewvideo"){
        m_previewvideo = streamId;
        Zmf_VideoRenderAdd((void*)m_viewVideo.GetSafeHwnd(), streamId.c_str(), 3, ZmfRenderAuto);
    }
    else if (type == "peervideo"){
        m_peervideo = streamId;
        Zmf_VideoRenderAdd((void*)m_viewVideo.GetSafeHwnd(), streamId.c_str(), 1, ZmfRenderAuto);
    }
    else if (type == "peershare"){ 
        m_peershare = streamId;
        Zmf_VideoRenderAdd((void*)m_viewVideo.GetSafeHwnd(), streamId.c_str(), 0, ZmfRenderAuto);
    }
    if (m_peershare.size() > 0 && m_peervideo.size() > 0)
        Zmf_VideoRenderMove((void*)m_viewVideo.GetSafeHwnd(), m_peervideo.c_str(), 0.75, 0, 1, 0.25);
    if (m_previewvideo.size() > 0 && (m_peershare.size() > 0 || m_peervideo.size() > 0))
        Zmf_VideoRenderMove((void*)m_viewVideo.GetSafeHwnd(), m_previewvideo.c_str(), 0, 0, 0.25, 0.25);
}
void CJsepTesterDlg::OnRemoveStream(const std::string& streamId, const std::string& type){
    if (type != "previewvideo" && type != "previewshare")
        m_streams.erase(streamId);

    Zmf_VideoRenderRemove((void*)m_viewVideo.GetSafeHwnd(), streamId.c_str());
    if (type == "previewvideo" && m_previewvideo == streamId)
        m_previewvideo.clear();
    else if (type == "peervideo" && m_peervideo == streamId)
        m_peervideo.clear();
    else if (type == "peershare" && m_peershare == streamId)
        m_peershare.clear();
    if (m_peershare.size() == 0){
        if (m_peervideo.size() > 0)
            Zmf_VideoRenderMove((void*)m_viewVideo.GetSafeHwnd(), m_peervideo.c_str(), 0, 0, 1, 1);
        else if (m_previewvideo.size() > 0)
            Zmf_VideoRenderMove((void*)m_viewVideo.GetSafeHwnd(), m_previewvideo.c_str(), 0, 0, 1, 1);
    }
}
void CJsepTesterDlg::OnCall(const std::string& fromId, bool p2pOnly){
    if (m_calling) return;
    CString url, pwd;
    GetDlgItemText(IDC_STUN, url);
    GetDlgItemText(IDC_STUN_PWD, pwd);
    m_peershare = m_peervideo = "";
    if (p2pOnly) {
        if (!m_client->P2P((std::string)CT2A(url, CP_UTF8), (std::string)CT2A(pwd, CP_UTF8), fromId))
            return;
        m_btnCall.EnableWindow(FALSE);
        m_btnVideo.EnableWindow(FALSE);
        m_btnP2P.SetWindowText(TEXT("Hangup"));
        Trace("--- p2p ok ---");
    }
    else {
        if (!m_client->Call((std::string)CT2A(url, CP_UTF8), (std::string)CT2A(pwd, CP_UTF8), m_btnVideo.GetCheck() != 0, fromId))
            return;
        Zmf_VideoRenderStart((void*)m_viewVideo.GetSafeHwnd(), ZmfRenderView);
        m_btnGetStats.EnableWindow(TRUE);
        m_btnShareScreen.EnableWindow(TRUE);
        m_btnLogEvent.EnableWindow(TRUE);
        m_btnDumpAPM.EnableWindow(TRUE); 
        m_btnP2P.EnableWindow(FALSE);
        m_btnCall.SetWindowText(TEXT("Hangup"));
        Trace("--- call ok ---");
    }
    SetDlgItemText(IDC_TOID, CString(fromId.c_str()));
    HWND hwnd;
    GetDlgItem(IDC_STUN, &hwnd);
    ::EnableWindow(hwnd, FALSE);
    GetDlgItem(IDC_STUN_PWD, &hwnd);
    ::EnableWindow(hwnd, FALSE);
    GetDlgItem(IDC_TOID, &hwnd);
    ::EnableWindow(hwnd, FALSE);
    UpdateData();
    m_calling = true;
}
void CJsepTesterDlg::OnHangup(){
    if (!m_calling) return;
    m_calling = false;
    if (m_client) m_client->Hangup();
    m_streams.clear();
    m_peershare = m_peervideo = m_previewvideo = "";
    m_btnShareScreen.SetCheck(0);
    m_btnLogEvent.SetCheck(0);
    m_btnDumpAPM.SetCheck(0);
    m_btnShareScreen.EnableWindow(FALSE);
    m_btnVideo.EnableWindow(TRUE);
    m_btnLogEvent.EnableWindow(FALSE);
    m_btnDumpAPM.EnableWindow(FALSE);
    m_btnGetStats.EnableWindow(FALSE);
    Zmf_VideoRenderStop((void*)m_viewVideo.GetSafeHwnd());
    Zmf_VideoRenderRemoveAll((void*)m_viewVideo.GetSafeHwnd());
    HWND hwnd;
    GetDlgItem(IDC_STUN, &hwnd);
    ::EnableWindow(hwnd, TRUE);
    GetDlgItem(IDC_STUN_PWD, &hwnd);
    ::EnableWindow(hwnd, TRUE);
    GetDlgItem(IDC_TOID, &hwnd);
    ::EnableWindow(hwnd, TRUE);
    m_btnCall.SetWindowText(TEXT("Call"));
    m_btnP2P.SetWindowText(TEXT("P2P"));
    m_btnCall.EnableWindow(TRUE);
    m_btnP2P.EnableWindow(TRUE);
    UpdateData();
    Invalidate();
    Trace("--- hangup ok ---");
}
void CJsepTesterDlg::_QueueUIThreadCallback(int msg_id, char* data) {
    ::PostMessage(GetSafeHwnd(), MESSAGE_JSEP_EVENT, msg_id, (LPARAM)data);
}
void CJsepTesterDlg::OnOpen(){
    m_btnConnect.SetWindowText(TEXT("Disconnect"));
    m_btnConnect.EnableWindow(TRUE);
    m_btnP2P.EnableWindow(TRUE);
    m_btnCall.EnableWindow(TRUE);
    m_btnVideo.EnableWindow(TRUE);
    UpdateData();
}
void CJsepTesterDlg::OnClose(){
    OnHangup();
    if (m_client) m_client->Close();
    m_client = 0; 
    m_btnVideo.EnableWindow(FALSE);
    m_btnCall.EnableWindow(FALSE);
    m_btnP2P.EnableWindow(FALSE);
    HWND hwnd;
    GetDlgItem(IDC_WS, &hwnd);
    ::EnableWindow(hwnd, TRUE);
    GetDlgItem(IDC_UID, &hwnd);
    ::EnableWindow(hwnd, TRUE);
    m_btnConnect.SetWindowText(TEXT("Connect"));
    m_btnConnect.EnableWindow(TRUE);
    UpdateData();
    Trace("--- close ok ---");
}
void CJsepTesterDlg::OnSocketStateChange(RTCSocket* rs, const char* state)
{
    std::string str(state);
    if (str == "closed") {
        RTCSocket_Close(rs);
        for (auto iter = m_clients.begin();
            iter != m_clients.end(); ++iter){
            if (iter->second != rs) continue;
            Trace("*WS:- %s", iter->first.c_str());
            m_clients.erase(iter);
            return;
        }
    }
}
void CJsepTesterDlg::OnSocketMessage(RTCSocket* rs, const char* buffer, int length)
{
    auto& token = JsonValue::parse(buffer, length);
    auto& json = token[0];
    const std::string& from = json["from"];
    const std::string& to = json["to"];
    if (!from.empty()) {
        if (m_clients.insert(std::make_pair(from, rs)).second){
            Trace("*WS:+ %s", from.c_str());
        }
    }
    if (!to.empty()) {
        auto iter = m_clients.find(to);
        if (iter != m_clients.end()){
            const std::string& type = json["type"];
            Trace("*WS:[%s]>[%s]:%s", from.c_str(), to.c_str(), type.c_str());
            RTCSocket_Send(iter->second, buffer, length);
        }
        else {
            const std::string cmd("{ \"txt\":\"hangup\", \"type\" : \"cmd\" }");
            Trace("*WS:%s to invalid Peer: %s", from.c_str(), to.c_str());
            RTCSocket_Send(rs, cmd.data(), cmd.size()); 
        }
    }
}