#include "jsep.h"
#include "../zmf/zmf.h"
#include "webrtc.h"
#include <map>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#ifdef _WIN32
#define strdup _strdup
#endif
enum {
    MESSAGE_JSEP_EVENT = 1,

    MESSAGE_WEBSOCKET_MESSAGE = 100,
    MESSAGE_WEBSOCKET_STATE,
    MESSAGE_WEBSOCKET_ICE,

    MESSAGE_ICESOCKET_MESSAGE = 200,
    MESSAGE_ICESOCKET_STATE,
    MESSAGE_ICESOCKET_ICE,
    
};
struct JsepClient : public WebRTC, public RTCSessionObserver, public RTCSocketObserver
{
    JsepClient(WebRTC::View& view, const std::string& id):
        m_view(&view), m_ws(0), m_is(0), m_myId(id), m_isAnswer(false),  m_pc(0), m_audioRunning(false)
    {}
    WebRTC::View* m_view;
    RTCSocket *m_ws, *m_is;
    const std::string m_myId;

    bool m_isAnswer;
    RTCPeerConnection* m_pc;
    std::string m_toId;
    std::string m_channelId;
    std::string m_localVideoId, m_localShareId;
    std::string m_cameraId, m_shareId;
    bool m_audioRunning;
    int m_updateCount;
    typedef std::map<std::string, std::string> RemoteStreamTypeMap;
    RemoteStreamTypeMap m_remoteStreamTypes;

    virtual bool P2P(const std::string& stunURL, const std::string& password, const std::string& toId);
    virtual bool P2P(const std::string& configuration, const std::string& toId);
    virtual bool Call(const std::string& stunURL, const std::string& password, bool video, const std::string& toId);
    virtual bool Call(const std::string& configuration, const std::string& constraints, const std::string& toId);
    virtual bool Update(bool video);
    virtual bool Update(const std::string& constraints);
    virtual bool InsertDtmf(const std::string& tones, int duration, int inter_tone_gap);
    virtual void Close();
    virtual void Send(const std::string& message, const std::string& toId);
    virtual void Hangup();
    virtual bool Share(bool share);
    virtual bool Share(const std::string& constraints);
    virtual void LogRtcEvent(const std::string& filename);
    virtual void DumpAudioProcessing(const std::string& filename);
    virtual bool GetStats(const std::string& statsType, int statsFlags);
    virtual void _UIThreadCallback(int msg_id, char* data);

    void sendSignaling(const std::string& type, const std::string& key, const std::string& json, const std::string* toId = 0);

    virtual void JSEP_THIS_CALL OnCreateDescriptionSuccess(const char* type, const char* desc) override;
    virtual void JSEP_THIS_CALL OnCreateDescriptionFailure(const char* type, const char* error) override;
    virtual void JSEP_THIS_CALL OnSetDescriptionSuccess(const char* type, const char* locate, const char* sdp) override;
    virtual void JSEP_THIS_CALL OnSetDescriptionFailure(const char* type, const char* locate, const char* error) override;
    virtual void JSEP_THIS_CALL OnIceCandidate(const char* candidate) override;
    virtual void JSEP_THIS_CALL OnIceConnectionStateChange(const char* iceState) override;
    virtual void JSEP_THIS_CALL OnSignalingChange(const char* signalingState) override;
    virtual void JSEP_THIS_CALL OnAddRemoteStream(const char* streamId, int audioTrackCount, int videoTrackCount) override;
    virtual void JSEP_THIS_CALL OnRemoveRemoteStream(const char* streamId) override;
    virtual void JSEP_THIS_CALL OnRenegotiationNeeded(void) override;
    virtual void JSEP_THIS_CALL OnToneChange(const char* tone) override;
    virtual void JSEP_THIS_CALL OnStatsReport(const char* statsType, const char* statsId, const char* stats, const double timestamp) override;
    virtual void JSEP_THIS_CALL OnDataChannelOpen(const char* channelId, const char* config) override;
    virtual void JSEP_THIS_CALL OnDataChannelMessage(const char* channelId, const char* buffer, int length) override;
    virtual void JSEP_THIS_CALL OnDataChannelClose(const char* channelId, const char* reason) override;

    virtual void JSEP_THIS_CALL OnSocketStateChange(RTCSocket* rs, const char* state) override;
    virtual void JSEP_THIS_CALL OnSocketIceCandidate(RTCSocket* rs, const char* candidate) override;
    virtual void JSEP_THIS_CALL OnSocketMessage(RTCSocket* rs, const char* buffer, int length) override;
    void OnWebSocketMessage(const std::string& message);
    void OnWebSocketOpen();
    void OnWebSocketClose();
};
static void JSEP_CDECL_CALL jsepObserver(RTCSessionObserver*userdata, enum RTCSessionEvent event, const char* json, int len) {
    WebRTC::View* view = ((JsepClient*)userdata)->m_view;
    if (view) view->_QueueUIThreadCallback(MESSAGE_JSEP_EVENT, strdup(json));
}
static void JSEP_CDECL_CALL socketObserver(RTCSocketObserver* userdata, RTCSocket* rs, const char* message, int length, enum RTCSocketEvent event){
    auto jc = ((JsepClient*)userdata);
    WebRTC::View* view = jc->m_view;
    if (view && (jc->m_ws == rs || jc->m_is == rs)) {
        std::string str(message, length);
        if (jc->m_ws == rs) view->_QueueUIThreadCallback(event + MESSAGE_WEBSOCKET_MESSAGE, strdup(str.c_str()));
        if (jc->m_is == rs) view->_QueueUIThreadCallback(event + MESSAGE_ICESOCKET_MESSAGE, strdup(str.c_str()));
    }
}
WebRTC* WebRTC::Connect(WebRTC::View& win, const std::string& server, const std::string& myId) {
    JsepClient* jc = new JsepClient(win, myId);
    std::ostringstream oss;
    oss << "{\"origin\":\"" << myId <<'"'
        << ",\"protocols\":\"turn,xxx,yyy,p2p,zzzz\""
        << ",\"ignore_bad_cert\":false"
        << ",\"ca_certs\":\"*\"" // 加载系统默认 CA 证书, 用于验证对方的证书
        //<< ",\"ca_certs\":\"cert.pem\"" // CA 证书, 用于验证对方的证书
        << ",\"hostname\":\"192.168.12.66\"" //连接名称,用于验证对方证书中的名称
        //<< ",\"certfile\":\"usercert.pem\"" //  自身证书
        //<< ",\"keyfile\":\"userkey.pem\"" // 自身私钥
        <<"}";
    jc->m_ws = WebSocket_Connect(server.c_str(), oss.str().c_str(), (RTCSocketObserver*)jc, socketObserver);
    if (jc->m_ws) return jc;
    delete jc;
    return 0;
}
void JsepClient::sendSignaling(const std::string& type, const std::string& key, const std::string& json, const std::string* toId){
    if (!m_ws) return;
    if (!toId) toId = &m_toId;
    if (toId->empty() || json.empty())
        return m_view->Trace("invalid toid");
    std::ostringstream oss;
    oss << "{\"type\":\""<<type << "\",\"from\":\"" << m_myId << "\",\"to\":\"" << *toId << "\",\"" << key << "\":";
    if (json[0] != '{'&& json[0] != '"')
        oss << "\"" << json << "\"";
    else
        oss << json;
    if (type == "sdp") oss << ",\"shareid\":\"" << m_localShareId << "\"";
    oss << "}";
    RTCSocket_Send(m_ws, oss.str().c_str(), 0);
    if (type == "sdp")
        m_view->Trace(">>sdp");
    else
        m_view->Trace(">>%s", json.c_str());
}
void JsepClient::Send(const std::string& message, const std::string& toId) {
    if (m_is){
        m_view->Trace("->%s", message.c_str());
        RTCSocket_Send(m_is, message.c_str(), 0);
    }
    else if (m_channelId.size()){
        m_view->Trace("=>%s", message.c_str());
        JSEP_SendMessage(m_pc, m_channelId.c_str(), message.c_str(), 0);
    }
    else
        sendSignaling("msg", "txt", message, &toId);
}
bool JsepClient::Update(bool bVideo){
    std::ostringstream constraints;
    char camera[512] = { 0 };
    if (bVideo && Zmf_CameraGetName(0, camera, 0) == 0) 
        constraints << "{\"video\":{\"zmfCapture\":\"" << JsonValue::escape(camera) << "\"}}";
    else
        constraints << "{\"audio\":true}";
    return Update(constraints.str());
}
bool JsepClient::Update(const std::string& constraints){
    if (!m_pc) return false;
    if (m_cameraId.size() > 0){
        m_view->OnRemoveStream(m_cameraId, "previewvideo");
        Zmf_VideoCaptureStop(m_cameraId.c_str());
        m_cameraId.clear();
    }
    if (m_localVideoId.size()){
        JSEP_RemoveLocalStream(m_pc, m_localVideoId.c_str());
        m_view->OnRemoveStream(m_localVideoId, "localstream");
        m_localVideoId.clear();
    }
    if (!constraints.size()) return true;
    int audio = 1, video = 1; {
        std::ostringstream streamId;
        streamId << m_myId << "_stream_" << m_updateCount;
        m_updateCount++;
        m_localVideoId = streamId.str();
    }
    if (0 != JSEP_AddLocalStream(m_pc, m_localVideoId.c_str(), &audio, &video, constraints.c_str())){
        JSEP_Release(m_pc);
        m_pc = 0;
        m_view->Trace("ERROR: add local stream: %s", constraints.c_str());
        return false;
    }
    m_view->OnAddStream(m_localVideoId, "localstream");
    if (video) {
        JsonValue token[10];
        Json_ParseString(constraints.c_str(), token);
        const JsonValue* vspec = Json_Value(token, "video");
        const JsonValue* camera = Json_Value(vspec, "zmfCapture");
        if (camera->type == JsonForm_String) {
            m_cameraId = *camera;
            m_view->OnAddStream(m_cameraId, "previewvideo");
            int width = 640, height = 480;
            if (vspec) {
                const JsonValue* maxWidth = Json_Value(vspec, "maxWidth");
                const JsonValue* maxHeight = Json_Value(vspec, "maxHeight");
                if (maxWidth->type == JsonForm_Primitive) width = atoi(maxWidth->json);
                if (maxHeight->type == JsonForm_Primitive) height = atoi(maxHeight->json);
            }
            Zmf_VideoCaptureStart(m_cameraId.c_str(), width, height, 30);
            m_view->Trace("start camera: %s", m_cameraId.c_str());
        }
    }
    if (audio && !m_audioRunning) {
        m_audioRunning = true;
        Zmf_AudioInputStart("default", 16000, 1, ZmfAecAuto, ZmfAgcAuto);
        Zmf_AudioOutputStart("default", 16000, 1);
        m_view->Trace("start audio: default");
    }
    return true;
}
bool JsepClient::P2P(const std::string& stunUrl, const std::string& password, const std::string& toId) {
    if (stunUrl.empty() || toId.empty()) return false;
    std::ostringstream configuration;
    configuration << "{"
        << "\"iceServers\":[{\"credential\":\"" << password << "\",\"urls\": [\"" << stunUrl << "\"]}],"
        << "\"options\":{\"networkIgnoreMask\":17}"
        << "}";
    return  P2P(configuration.str(), toId);
}
bool JsepClient::P2P(const std::string& configuration, const std::string& toId){
    if (m_is) return true;
    if (toId.empty() || configuration.empty()) return false;
    m_toId = toId;
    std::ostringstream parameters;
    parameters << "{\"username\":\"1234\",\"password\":\"123456789012345678901234\"}";
    const auto& iceParameters = parameters.str();
    m_view->Trace("%s %s", m_isAnswer ? "answer" : "call", toId.c_str());
    m_is = IceSocket_Connect(configuration.c_str(), iceParameters.c_str(), !m_isAnswer, this, socketObserver);
    if (!m_is) {
        m_view->Trace("ERROR: create ice: %s", configuration.c_str());
        return false;
    }
    sendSignaling("ice", "param", iceParameters);

    return true;
}
bool JsepClient::Call(const std::string& stunUrl, const std::string& password, bool bVideo, const std::string& toId) {
    if (stunUrl.empty() || toId.empty()) return false;
    std::ostringstream configuration;
    configuration << "{"
        << "\"iceServers\":[{\"credential\":\"" << password << "\",\"urls\": [\"" << stunUrl << "\"]}]"
        << ",\"constraints\":{\"minPort\":8000,\"maxPort\":9000}"
        <<"}";
    std::ostringstream constraints;
    char camera[512] = { 0 };
    if (bVideo && Zmf_CameraGetName(0, camera, 0) == 0)
        constraints << "{\"video\":{\"zmfCapture\":\"" << JsonValue::escape(camera) << "\"}}";
    else
        constraints << "{\"audio\":true}";
    return Call(configuration.str(), constraints.str(), toId);
}
bool JsepClient::Call(const std::string& configuration, const std::string& constraints, const std::string& toId) {
    if (m_pc) return true;
    if (toId.empty()
        || !m_localVideoId.empty() || !m_localShareId.empty()
        || !m_cameraId.empty() || !m_shareId.empty() || m_audioRunning)
        return false;

    m_toId = toId;
    m_remoteStreamTypes.clear();
    m_updateCount = 0;
    m_view->Trace("%s %s", m_isAnswer ? "answer" : "call", toId.c_str());
    m_pc = JSEP_RTCPeerConnection(configuration.c_str(), false, !m_isAnswer, (RTCSessionObserver*)this, jsepObserver);
    if (!m_pc) {
        m_view->Trace("ERROR: create pc: %s", configuration.c_str());
        return false;
    }
    if (!m_isAnswer){
        JSEP_CreateDataChannel(m_pc, "data-channel", NULL);
        m_view->Trace("DC create: data-channel");
    }
    return Update(constraints);
}
void JsepClient::Hangup() {
    if (m_is || m_pc) sendSignaling("cmd", "txt", "hangup");
    RTCSocket_Close(m_is);
    JSEP_Release(m_pc);
    m_is = 0;
    m_pc = 0;
    m_channelId.clear();
    m_isAnswer = false;
    m_localVideoId.clear();
    m_localShareId.clear();
    Zmf_AudioInputStopAll();
    Zmf_AudioOutputStopAll();
    Zmf_VideoCaptureStopAll();
    m_audioRunning = false;
    m_cameraId.clear();
    m_shareId.clear();
}
void JsepClient::_UIThreadCallback(int msg_id, char* data) {
    if (!m_view) {
        free(data);
        return;
    }
    switch (msg_id){
    case MESSAGE_WEBSOCKET_MESSAGE:
        OnSocketMessage(m_ws, data, strlen(data));
        break;
    case MESSAGE_WEBSOCKET_STATE:
        OnSocketStateChange(m_ws, data);
        break;
    case MESSAGE_ICESOCKET_MESSAGE:
        OnSocketMessage(m_is, data, strlen(data));
        break;
    case MESSAGE_ICESOCKET_STATE:
        OnSocketStateChange(m_is, data);
        break;
    case MESSAGE_ICESOCKET_ICE:
        OnSocketIceCandidate(m_is, data);
        break;
    case MESSAGE_JSEP_EVENT:
    {
        JsonValue token[100];
        Json_ParseString(data, token);
        JsonValue& json = token[0];
        const int event = atoi(json[JespEvent].json);
        switch (event) {
        case RTCSessionEvent_RenegotiationNeeded:
            OnRenegotiationNeeded();
            break;
        case RTCSessionEvent_AddRemoteStream:
            OnAddRemoteStream(json[JsepStreamId].str().c_str(), atoi(json[JsepAudioTrackCount].json), atoi(json[JsepVideoTrackCount].json));
            break;
        case RTCSessionEvent_RemoveRemoteStream:
            OnRemoveRemoteStream(json[JsepStreamId].str().c_str());
            break;
        case RTCSessionEvent_IceCandidate:
            OnIceCandidate(json[JsepIceCandidate].str().c_str());
            break;
        case RTCSessionEvent_IceConnectionStateChange:
            OnIceConnectionStateChange(json[JsepIceConnectionState].str().c_str());
            break;
        case RTCSessionEvent_SignalingChange:
            OnSignalingChange(json[JsepSignalingState].str().c_str());
            break;
        case RTCSessionEvent_CreateDescriptionSuccess:
            OnCreateDescriptionSuccess(json[JsepSdpType].str().c_str(), json[JsepSdp].str().c_str());
            break;
        case RTCSessionEvent_CreateDescriptionFailure:
            OnCreateDescriptionFailure(json[JsepSdpType].str().c_str(), json[JsepReason].str().c_str());
            break;
        case RTCSessionEvent_SetDescriptionSuccess:
            OnSetDescriptionSuccess(json[JsepSdpType].str().c_str(), json[JsepSdpLocation].str().c_str(), json[JsepSdp].str().c_str());
            break;
        case RTCSessionEvent_SetDescriptionFailure:
            OnSetDescriptionFailure(json[JsepSdpType].str().c_str(), json[JsepSdpLocation].str().c_str(), json[JsepReason].str().c_str());
            break;
        case RTCSessionEvent_ToneChange:
            OnToneChange(json[JsepTone].str().c_str());
            break;
        case RTCSessionEvent_StatsReport:
            OnStatsReport(json[JsepStatsType].str().c_str(), json[JsepStatsId].str().c_str(), json[JsepStats].str().c_str(), atof(json[JsepStatsTimestamp].json));
            break;
        case RTCSessionEvent_DataChannelOpen:
            OnDataChannelOpen(json[JsepChannelId].str().c_str(), json[JsepChannelConfig].str().c_str());
            break;
        case RTCSessionEvent_DataChannelMessage:{
            const std::string& msg = json[JsepMessage];
            OnDataChannelMessage(json[JsepChannelId].str().c_str(), msg.c_str(), msg.size());
            break;
        }
        case RTCSessionEvent_DataChannelClose:
            return OnDataChannelClose(json[JsepChannelId].str().c_str(), json[JsepReason].str().c_str());
        }
    }}
    free(data);
}
//////////////////////////////////////////////////////////////////////////
void JsepClient::OnRenegotiationNeeded(){ 
    if (!m_pc) return;
    m_view->Trace("create offer return %d", JSEP_CreateOffer(m_pc, 0));
}
void JsepClient::OnAddRemoteStream(const char* streamId, int audioTrackCount, int videoTrackCount){
    if (!m_pc) return;
    RemoteStreamTypeMap::iterator iter = m_remoteStreamTypes.find(streamId);
    if (iter == m_remoteStreamTypes.end()) {
        std::string type;
        if (videoTrackCount > 0)
            type = "peervideo";
        else if (audioTrackCount > 0)
            type = "peeraudio";
        else
            type = "peerdata";
        iter = m_remoteStreamTypes.insert(RemoteStreamTypeMap::value_type(streamId, type)).first;
    }
    m_view->OnAddStream(streamId, iter->second);
    if (videoTrackCount > 0){
        JSEP_PublishRemoteStream(m_pc, streamId, 0, 1);
        m_view->Trace("publish render: %s", streamId);
    }
}
void JsepClient::OnRemoveRemoteStream(const char* streamId){
    if (!m_pc) return;
    RemoteStreamTypeMap::iterator iter = m_remoteStreamTypes.find(streamId);
    if (iter == m_remoteStreamTypes.end()) return;
    m_view->OnRemoveStream(streamId, iter->second);
    m_remoteStreamTypes.erase(iter);
}
void JsepClient::OnIceCandidate(const char* candidate){
    if (!m_pc) return;
    sendSignaling("ice", "ice", candidate);
}
void JsepClient::OnIceConnectionStateChange(const char* iceState){
    if (!m_pc) return;
    m_view->Trace("ICE: %s", iceState);
}
void JsepClient::OnSignalingChange(const char* signalingState) {
    if (!m_pc) return;
    m_view->Trace("signaling: %s", signalingState);
}
void JsepClient::OnCreateDescriptionFailure(const char* type, const char* error){
    if (!m_pc) return;
    m_view->Trace("create %s : %s", type, error); 
}
void JsepClient::OnSetDescriptionFailure(const char* type, const char* locate, const char* error){
    if (!m_pc) return;
    m_view->Trace("set %s into %s : %s", type, locate, error);
}
void JsepClient::OnCreateDescriptionSuccess(const char* type, const char* desc){ 
    if (!m_pc) return;
    JSEP_SetLocalDescription(m_pc, desc);
}
void JsepClient::OnSetDescriptionSuccess(const char* type, const char* locate, const char* sdp){
    if (!m_pc) return;
    m_view->Trace("set %s into %s ok", type, locate);
    if (std::string(locate) == "local")
        sendSignaling("sdp", "sdp", sdp);
    if (std::string(type) == "offer" && std::string(locate) == "remote"){
        m_view->Trace("create answer return %d", JSEP_CreateAnswer(m_pc, NULL));
    }
}
void JsepClient::OnToneChange(const char* tone){
    if (!m_pc) return;
    m_view->Trace("DTMF: %s", tone);
}
void JsepClient::OnStatsReport(const char* statsTyep, const char* statsId, const char* stats, const double timestamp){
    if (!m_pc) return;
    m_view->OnStatsReport(statsTyep, statsId, stats, timestamp);
}
void JsepClient::OnDataChannelOpen(const char* channelId, const char* config){
    if (!m_pc) return;
    m_view->Trace("DC open: %s", channelId);
    m_channelId = channelId;
}
void JsepClient::OnDataChannelClose(const char* channelId, const char*  reason) {
    if (!m_pc) return;
    m_view->Trace("DC close: %s", channelId);
    m_channelId.clear();
}
void JsepClient::OnDataChannelMessage(const char*  channelId, const char* buffer, int length){
    if (!m_pc) return;
    m_view->OnMessage(std::string(buffer, length), m_toId);
}
void JsepClient::OnWebSocketOpen(){
    m_view->Trace("WS onopen");
    std::ostringstream oss;
    oss << "{\"from\":\"" << m_myId << "\"}";
    RTCSocket_Send(m_ws, oss.str().c_str(), 0);
    m_view->OnOpen();
}
void JsepClient::OnWebSocketClose(){
    Hangup();
    m_view->Trace("WS onclose");
    RTCSocket_Close(m_ws);
    m_ws = 0;
    m_view->OnClose();
}
void JsepClient::OnWebSocketMessage(const std::string& message) {
    const auto& token = JsonValue::parse(message.data(), message.size());
    auto& json = token[0];
    const std::string& type = json["type"];
    const std::string& txt = json["txt"];
    const std::string& from = json["from"];
    if (type == "msg")
        return m_view->OnMessage(txt, from);

    if (type == "cmd"){
        m_view->Trace("<<%s", txt.c_str());
        if (txt == "hangup"){
            const bool hangup = m_pc || m_is;
            JSEP_Release(m_pc);
            RTCSocket_Close(m_is);
            m_pc = 0;
            m_is = 0;
            m_isAnswer = false;
            if (hangup) m_view->OnHangup();
        }
        return;
    }
    if (type == "ice") {
        if (!m_is && !m_pc) {
            m_isAnswer = true;
            const auto p2pOnly = (json["ice"].type == JsonForm_String || json["param"].type == JsonForm_Object);
            m_view->OnCall(from, p2pOnly);
        }
        if (m_is && json["param"].type == JsonForm_Object){
            const std::string& str = json["param"];
            IceSocket_SetRemoteParameters(m_is, str.c_str());
            return m_view->Trace("<<%s", str.c_str());
        }
        else if (m_is && json["ice"].type == JsonForm_String){
            const std::string& str = json["ice"];
            IceSocket_AddRemoteCandidate(m_is, str.c_str());
            return m_view->Trace("<<%s", str.c_str());
        }
        else if (m_pc && json["ice"].type == JsonForm_Object){
            const std::string& str = json["ice"];
            JSEP_AddIceCandidate(m_pc, str.c_str());
            return m_view->Trace("<<%s", str.c_str());
        }
    }
    
    m_view->Trace("<<%s", type.c_str());
    if (json["sdp"].type == JsonForm_Object){
        if (!m_is && !m_pc) {
            m_isAnswer = true;
            m_view->OnCall(from, false);
        }
        const JsonValue& sid = json["shareid"];
        if (sid.type == JsonForm_String) m_remoteStreamTypes[sid] = "peershare";
        JSEP_SetRemoteDescription(m_pc, json["sdp"].str().c_str());
    }
}
void JsepClient::OnSocketStateChange(RTCSocket* rs, const char* state)
{
    if (m_is == rs)
        m_view->Trace("p2p: %s", state);
    if (m_ws == rs){
        if (strncmp(state, "open", 4) == 0)
            OnWebSocketOpen();
        if (strcmp(state, "closed") == 0)
            OnWebSocketClose();
    }
}
void JsepClient::OnSocketIceCandidate(RTCSocket* rs, const char* candidate)
{
    if (m_is != rs) return;
    sendSignaling("ice", "ice", candidate);
}
void JsepClient::OnSocketMessage(RTCSocket* rs, const char* buffer, int length)
{
    std::string str(buffer, length);
    if (m_ws == rs) OnWebSocketMessage(str);
    if (m_is == rs) m_view->Trace("-< %s", str.c_str());
}
bool JsepClient::Share(bool share){
    std::ostringstream constraints;
    char desktop[512] = { 0 };
    if (share && Zmf_DesktopGetName(0, desktop, 0) == 0) 
        constraints << "{\"video\":{\"zmfCapture\":\"" << JsonValue::escape(desktop) << "\"}}";
    return Share(constraints.str());

}
bool JsepClient::Share(const std::string& constraints){
    if (!m_pc) return false;
    if (m_shareId.size() > 0){
        m_view->OnRemoveStream(m_shareId, "previewshare");
        Zmf_VideoCaptureStop(m_shareId.c_str());
        m_shareId.clear();
    }
    if (m_localShareId.size()){
        JSEP_RemoveLocalStream(m_pc, m_localShareId.c_str());
        m_view->OnRemoveStream(m_localShareId, "localshare");
        m_localShareId.clear();
    }
    if (!constraints.size()) return true;
    int video = 1; {
        std::ostringstream streamId;
        streamId << m_myId << "_share" << m_updateCount;
        m_updateCount++;
        m_localShareId = streamId.str();
    }
    if (0 != JSEP_AddLocalStream(m_pc, m_localShareId.c_str(), NULL, &video, constraints.c_str())) {
        JSEP_Release(m_pc);
        m_pc = 0;
        m_view->Trace("ERROR: add share stream: %s", constraints.c_str());
        return false;
    }
    m_view->OnAddStream(m_localShareId, "localshare");
    if (video) {
        JsonValue token[10];
        Json_ParseString(constraints.c_str(), token);
        const JsonValue* vspec = Json_Value(token, "video");
        const JsonValue* share = Json_Value(vspec, "zmfCapture");
        if (share->type == JsonForm_String) {
            m_shareId = *share;
            m_view->OnAddStream(m_shareId, "previewshare");
            int width = 640, height = 480;
            if (vspec) {
                const JsonValue* maxWidth = Json_Value(vspec, "maxWidth");
                const JsonValue* maxHeight = Json_Value(vspec, "maxHeight");
                if (maxWidth->type == JsonForm_Primitive) width = atoi(maxWidth->json);
                if (maxHeight->type == JsonForm_Primitive) height = atoi(maxHeight->json);
            }
            Zmf_VideoCaptureStart(m_shareId.c_str(), width, height, 30);
            m_view->Trace("start share: %s", m_shareId.c_str());
        }
    }
    return true;
}
void JsepClient::LogRtcEvent(const std::string& filename)
{
    if (!m_pc) return;
    int ret = JSEP_LogRtcEvent(m_pc, filename.c_str(), 10000);
    m_view->Trace("log event %s return %d",
        filename.size() > 0 ?  filename.c_str() : "stoped", ret);
}
void JsepClient::DumpAudioProcessing(const std::string& filename)
{
    if (!m_pc) return;
    int ret = JSEP_DumpAudioProcessing(filename.c_str(), 10000);
    m_view->Trace("dump audioproc %s return %d",
        filename.size() > 0 ?  filename.c_str() : "stoped", ret);
}
bool JsepClient::InsertDtmf(const std::string& tones, int duration, int inter_tone_gap){
    if (!m_pc) return false;
    return 0 == JSEP_InsertDtmf(m_pc, tones.c_str(), duration, inter_tone_gap);
}
bool JsepClient::GetStats(const std::string& statsType, int statsFlags) {
    if (!m_pc) return false;
    return 0 == JSEP_GetStats(m_pc, statsType.c_str(), statsFlags);
}
void JsepClient::Close(){
    m_view = 0;
    Hangup();
    RTCSocket_Close(m_ws);
    m_ws = 0;
    delete this;
}
