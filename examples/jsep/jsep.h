#ifndef __JSEP_H__
#define __JSEP_H__
#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#define JSEP_THIS_CALL  __thiscall
#define JSEP_CDECL_CALL __cdecl
#ifdef _BUILD_JSEP_API_LEVEL__
#define _JSEP_EXPORT__ __declspec(dllexport)
#else
#define _JSEP_EXPORT__ __declspec(dllimport)
#endif //_BUILD_JSEP_API_LEVEL__
#else
#include <unistd.h>
#include <dlfcn.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
#define JSEP_IMPORT 1
#elif TARGET_OS_MAC
#include <libproc.h>
#endif
#endif //__APPLE__
#define JSEP_THIS_CALL
#define JSEP_CDECL_CALL
#ifdef _BUILD_JSEP_API_LEVEL__
#define _JSEP_EXPORT__ __attribute__ ((visibility ("default")))
#else
#define _JSEP_EXPORT__
#endif //_BUILD_JSEP_API_LEVEL__
#endif

#ifdef __cplusplus
#include <string>
#include <vector>
#define JSEP_PUBLIC extern "C" _JSEP_EXPORT__
#define JSEP_INLINE extern inline
#else
#define JSEP_PUBLIC extern _JSEP_EXPORT__
#define JSEP_INLINE extern __inline
#endif //__cplusplus

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
////////////////////////////////////////////////////////////////////////////////
/** 接口函数版本号
 * JSEP_API结构更新,必须更新此版本号
 *
 * @remarks
 *  可用如下代码, 测试JSEP库是否加载成功
 *      JsepAPI(JSEP_API_LEVEL) ? "ok" : "failed"
 *
 *  环境变量
 *   - JSEP_LOG_TO_DEBUG 可改变日志级别,可选值 "INFO", "WARN", "ERROR"
 *   - JSEP_LOG_DIR_PATH 可将日志写入目录
 */
#define JSEP_API_LEVEL      1

typedef int RTCBoolean;
typedef struct RTCPeerConnection RTCPeerConnection;
typedef struct RTCSocket RTCSocket;
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup JSON 格式的配置参数类型
 * @{
 */

/**
 * 通用配置参数类型.
 * 例如
 * {'iceServers': [{'urls': [ url1, url2 ]}]}
 *
 * 支持的配置参数
 * - bundlePolicy:'balanced'
 * - rtcpMuxPolicy:'require' | 'negotiate'
 * - iceTransportPolicy:'all'
 * - STRUN 服务器地址
 *  iceServers: [{
 *          urls: [ 'url' ],
 *          username:'',
 *          credential:'',
 *  }]
 * - 限制参数
 *  constraints:{
 *      minPort: 0
 *      maxPort: 0
 *      googIPv6: true
 *      googDscp: true
 *      DtlsSrtpKeyAgreement: true
 *      RtpDataChannels: false
 *      googSuspendBelowMinBitrate: true
 *      googNumUnsignalledRecvStreams: 20
 *      googScreencastMinBitrate:<int>
 *      googHighStartBitrate:<int>
 *      googHighBitrate:true
 *      googVeryHighBitrate:true
 *      googCombinedAudioVideoBwe:true
 *  }
 * - 可选参数
 *   options: {
 *      networkIgnoreMask: 16 //整型掩码, 可选值: ETHERNET=1, WIFI=2, CELLULAR=4, VPN=8, LOOPBACK=16
 *      disableEncryption: false //关闭 SRTP
 *   }
 */
typedef const char* RTCConfiguration;

/** ICE 候选地址 */
typedef const char* RTCIceCandidate;

/**
 * Ice连接的配置参数
 *
 * 目前支持
 * - username 或 usernameFragment 或 ufrag
 * - password
 * - iceLite 或 renomination
 */
typedef const char* RTCIceParameters;

/**
 * 音视频媒体的限制参数
 *
 * 目前支持
 * - 视频
 *    video: {
 *      //与ZMF对接的参数
 *      zmfCapture:Id0      //ZMF采集源ID
 *      zmfRender:Id0       //ZMF渲染源ID,转接为视频采集源
 *
 *      //内置数值参数
 *      maxWidth,minWidth,  //宽度限制
 *      maxHeight,minHeight,//高度限制
 *      maxFrameRate,minFrameRate,//帧速率限制
 *      maxAspectRatio,minAspectRatio,//宽度比限制
 *      googNoiseReduction,//是否降噪
 *    }
 * - 音频
 *    audio: {
 *      DTMF:false          //关闭DTMF,可节省些内存.
 *
 *      //内置布尔参数
 *      echoCancellation,googEchoCancellation,googEchoCancellation2, googDAEchoCancellation,
 *      googAutoGainControl,googAutoGainControl2,
 *      googNoiseSuppression,googNoiseSuppression2,
 *      intelligibilityEnhancer,
 *      levelControl,
 *      googHighpassFilter,
 *      googTypingNoiseDetection,
 *      googAudioMirroring,
 *      levelControlInitialPeakLevelDBFS,
 *      googAudioNetworkAdaptorConfig
 *    }
 *  或
 *    audio: false          //强制关闭音频
 */
typedef const char* MediaStreamConstraints;

typedef const char* RTCAnswerOptions;

typedef const char* RTCSessionDescription;


/**
 * 协商过程的 Offer 创建参数
 *
 * 目前支持
 *  - OfferToReceiveAudio: true
 *  - OfferToReceiveVideo: true
 *  - VoiceActivityDetection: true
 *  - IceRestart: false
 *  - googUseRtpMUX: true
 */
typedef const char* RTCOfferOptions;

/**
 * 额外数据通道 DataChannel 的配置参数
 *
 * 目前支持
 * - ordered:true,       //是否保证次序,默认true
 * - maxPacketLifeTime:0,//超过限时,将不重发.默认-1,始终重发
 * - maxRetransmits:0,   //超过次数,将不重发,默认-1,,始终重发
 * - protocol:'',        //自定义的上层应用协议名,默认空
 * - negotiated:false,   //是否由上层应用负责协商建立过程,
 *                         即不触发OnChannelOpen事件.默认false由内部自动完成
 */
typedef const char* RTCDataChannelInit;

/**
 * WebSocket 配置参数
 *
 * 客户侧连接目前支持
 * - origin:''    //可选的来源标识
 * - protocols:'' //可选的应用层协议,多个协议以','分开
 * - ca_certs:'cert.pem' //CA证书所在的PEM文件或目录, 用于验证对方证书, '*' 表示使用系统证书
 * - hostname:''         //客户端连接主机名称,用于验证对方证书中的名称
 * - certfile:'mypub.pem'  //自身证书(公钥)
 * - keyfile:'mykey.pem'   //自身私钥
 * - cert_reqs:0  //详细参见 SSL_VERIFY_*
 * - ignore_bad_cert:false //忽略对端证书(公钥)的验证错误,仅用于调试
 * - timeout:2000 //服务端握手的超时时间
 * - backlog:100  //服务端侦听的最大长度
 * - ipv6:false   //服务端侦听地址为''或*时,指定为in6addr_any, 默认是INADDR_ANY
 */
typedef const char* RTCWebSocketInit;

/**
 * 详细的统计数据
 * 根据统计类型对应不同的 JSON 格式
 *
 * 目前支持如下统计类型
 * - googLibjingleSession  全局会话
 * - transport             传输层
 * - VideoBwe              视频带宽估计
 * - remoteSsrc            对端RTP流
 * - ssrc                  RTP流
 *   视频: {bytesSent:0,codecImplementationName:'',framesEncoded:0}
 *   音频: {}
 *
 * - googTrack             媒体流
 *   {googTrackId:'video0'}
 *
 * - localcandidate        本地ICE
 *   {candidateType:'host',ipAddress:'192.168.0.240',networkType:'unknown', portNumber:'53217',priority:1518018303,transport:'tcp'}
 *
 * - remotecandidate       对端ICE
 *   {candidateType:'host',ipAddress:'192.168.0.240',portNumber:'53217',priority:1518018303,transport:'tcp'}
 *
 * - googComponent
 *
 * - googCandidatePair
 *
 * - googCertificate
 *
 * - datachannel           数据通道
 *   {datachannelid:1,label:'',protocol:'',state:'open'}
 */
typedef const char* RTCStats;
/** @} */
//////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup RTCSessionEvent事件携带JSON参数的字段名
 * @{
 */
/** 事件类型, 数据类型:enum RTCSessionEvent */
#define JespEvent                           "JespEvent"

/** SDP类型, 数据类型:UTF8字符串, 可选值
 * - "offer" 发起类型
 * - "answer" 应答类型
 */
#define JsepSdpType                         "JsepSdpType"

/** SDP位置, 数据类型:UTF8字符串, 可选值
 * - "local"  本地接收侧
 * - "remote" 对应发送侧
 */
#define JsepSdpLocation                     "JsepSdpLocation"

/** SDP对象,数据类型: JSON串 @seee RTCSessionDescription */
#define JsepSdp                             "JsepSdp"

/** 原因, 数据类型: UTF8字符串 */
#define JsepReason                          "JsepReason"

/** ICE地址, 数据类型: JSON串 @see RTCIceCandidate */
#define JsepIceCandidate                    "JsepIceCandidate"

/** ICE连接状态, 数据类型: UTF8字符串, 可选值
 * - "new" 初始状态,
 * - "checking" 探测进行状态
 * - "connected" 成功连接
 * - "completed" 探测结束
 * - "disconnected" 断开连接
 * - "failed" 连接过程已失败,不可恢复
 * - "closed", 关闭连接
 * 只有在connected和completed状态下,才可进行通信功能.
 */
#define JsepIceConnectionState              "JsepIceConnectionState"

/** 协商状态, 数据类型: UTF8字符串, 可选值
 * - "stable", 没有进行协商的稳定状态.
 * - "have-local-offer", 已调SetLocalDescription(offer),后须接收并调用SetRemoteDescription(answer)
 * - "have-remote-offer",已调SetRemoteDescription(offer),后须创建并应答SetLocalDescription(answer)
 * - "have-remote-pranswer",已调SetRemoteDescription(answer),没有SetLocalDescription(offer)状态
 * - "have-local-pranswer",已调SetLocalDescription(answer),没有SetRemoteDescription(offer)状态
 * - "closed",连接已关闭的状态.
 */
#define JsepSignalingState                  "JsepSignalingState"

/** 媒体流ID, 数据类型: UTF8字符串 */
#define JsepStreamId                        "JsepStreamId"

/** 媒体流中音频轨道个数, 数据类型: int */
#define JsepAudioTrackCount                 "JsepAudioTrackCount"

/** 媒体流中视频轨道个数, 数据类型: int */
#define JsepVideoTrackCount                 "JsepVideoTrackCount"

/** 数据通道ID, 数据类型: UTF8字符串 */
#define JsepChannelId                       "JsepChannelId"

/** 数据通道属性, 数据类型: JSON串 @see RTCDataChannelInit */
#define JsepChannelConfig                   "JsepChannelConfig"

/** 消息数据, 数据类型: UTF8字符串 */
#define JsepMessage                         "JsepMessage"

/** DTMF 已发送音符, 数据类型: UTF8字符串
 * 若为""或nullptr则表示已发送完毕
 */
#define JsepTone                            "JsepTone"

/** 统计类型, 数据类型: UTF8字符串, 可选值
 * - googLibjingleSession  全局会话
 * - transport             传输层
 * - VideoBwe              视频带宽估计
 * - remoteSsrc            对端RTP流
 * - ssrc                  RTP流
 * - googTrack             媒体流
 * - localcandidate        本地ICE
 * - remotecandidate       对端ICE
 * - googComponent
 * - googCandidatePair
 * - googCertificate
 * - datachannel           数据通道
 */
#define JsepStatsType                       "JsepStatsType"

/** 统计ID, 数据类型: UTF8字符串 */
#define JsepStatsId                         "JsepStatsId"

/** 统计时间截, 数据类型: double */
#define JsepStatsTimestamp                  "JsepStatsTimestamp"

/** 统计值, 数据类型: JSON串 @see RTCStats */
#define JsepStats                           "JsepStats"

/** @} */
////////////////////////////////////////////////////////////////////////////////
/**
 * 通信中的错误值
 */
enum RTCSessionError {
    /** 无效操作,未知错误 */
    RTCSessionError_InvalidOperation    = -1,
    /** 非法的参数使用 */
    RTCSessionError_InvalidArgument     = -2,
    /** 没有视频流 */
    RTCSessionError_MissingVideoTrack   = -3,
    /** 版本不匹配 */
    RTCSessionError_MismatcheaVersion   = -4,
};
////////////////////////////////////////////////////////////////////////////////
/**
 * RTCSocket 事件
 * 在不使用RTCSocketObserver, 而是使用统一的回调函时,
 * void (JSEP_CDECL_CALL *observer)(RTCSocketObserver*, RTCSocket*, const char* data, int length, enum RTCSocketEvent)
 * 通过不同的事件标识其中 data 的实际含义
 */
enum RTCSocketEvent {
    /** 收到消息. 对应 RTCSocketObserver::OnSocketMessage */
    RTCSocketEvent_Message = 0,
    /** 状态改变,其数据为新状态. 对应 RTCSocketObserver::OnSocketStateChange */
    RTCSocketEvent_StateChange,
    /** 获取新的本地候选地址, 其数据为候选地址. 对应 RTCSocketObserver::OnSocketIceCandidate */
    RTCSocketEvent_IceCandidate,
};
enum RTCSocketFlag {
    /** 二进制消息 */
    RTCSocketFlag_Binary = 1,
    /** WebSocket 使用 Masking */
    RTCSocketFlag_Masking= 2,
};

////////////////////////////////////////////////////////////////////////////////
/**
 * GetStats接口的参数
 * 用于细分或过滤统计结果
 */
enum RTCStatsFlag {
    /** 详细的, 调试级别的统计 */
    RTCStatsFlag_Debug = 1,
    /** 包含音频相关的统计 */
    RTCStatsFlag_Audio = 2,
    /** 包含音频相应的统计 */
    RTCStatsFlag_Video = 4,
};
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup 通信中的会议事件, 携带有JSON格式参数
 *  不使用RTCSessionObserver, 而是使用统一的回调函数
 *  void (JSEP_CDECL_CALL *observer)(RTCSessionObserver*, enum RTCSessionEvent event, const char* json, int length)
 *  通过不同的事件标识,其中 json 的对应实现含义
 * @{
 */
enum RTCSessionEvent {
    /**
     * 需要进行重新协商
     * 对应 RTCSessionObserver::OnRenegotiationNeeded
     *
     * @remarks
     *  通常需调JSEP_CreateOffer(),再重复协商流程
     */
    RTCSessionEvent_RenegotiationNeeded = 1,

    /**
     * 创建 SDP 成功
     * 对应 RTCSessionObserver::OnCreateDescriptionSuccess
     *  {JsepSdpType:%s,JsepSdp:{...}}
     *
     * @remarks
     *  协商时,需要调用JSEP_SetLocalDescription()设置该SDP
     */ 
    RTCSessionEvent_CreateDescriptionSuccess,

    /**
     * 创建 SDP 失败
     * 对应 RTCSessionObserver::OnCreateDescriptionFailure
     *  {JsepSdpType:%s,JsepReason:%s}
     *
     */ 
    RTCSessionEvent_CreateDescriptionFailure,

    /**
     * 设置 SDP 成功
     * 对应 RTCSessionObserver::OnSetDescriptionSuccess
     *  {JsepSdpType:%s,JsepSdpLocation:%s,JsepSdp:{...}}
     *
     * @remarks
     *  若是remote offer, 则需要创建对应的 answer SDP,并反馈对端
     */
    RTCSessionEvent_SetDescriptionSuccess,

    /**
     * 设置 SDP 失败
     * 对应 RTCSessionObserver::OnSetDescriptionFailure
     *  {JsepSdpType:%s,JsepSdpLocation:%s, JsepReason:%s}
     */
    RTCSessionEvent_SetDescriptionFailure,

    /**
     * 新的ICE候选地址
     * 对应 RTCSessionObserver::OnIceCandidate
     *  {JsepIceCandidate:{...}}
     *
     * @remarks
     *  协商时,将该ICE地址通过信令通知对端
     */
    RTCSessionEvent_IceCandidate,

    /**
     * ICE连接状态改变
     * 对应 RTCSessionObserver::OnIceConnectionStateChange
     * {JsepIceConnectionState:%s}
     */
    RTCSessionEvent_IceConnectionStateChange,

    /**
     * 协商状态改变
     * 对应 RTCSessionObserver::OnSignalingChange
     * {JsepSignalingState:%s}
     */
    RTCSessionEvent_SignalingChange,

    /**
     * 已添加对端媒体流
     * 对应 RTCSessionObserver::OnAddRemoteStream
     *  {JsepStreamId:%s,JsepAudioTrackCount:%d,JsepVideoTrackCount:%d}
     *
     * @remarks
     *  需要调用JSEP_PublishRemoteStream(),发布该流,以便与Zmf对接
     */
    RTCSessionEvent_AddRemoteStream,

    /**
     * 已移除对端媒体流
     * 对应 RTCSessionObserver::OnRemoveRemoteStream
     *  {JsepStreamId:%s}
     */
    RTCSessionEvent_RemoveRemoteStream,

    /**
     * DTMF 音符已发送
     * 对应 RTCSessionObserver::OnToneChange
     *  {JsepTone:%s}
     */
    RTCSessionEvent_ToneChange,

    /**
     * 收到统计报告
     * 对应 RTCSessionObserver::OnStatsReport
     *  {JsepStatsType:%s,JsepStatsId:%s,JsepStatsTimestamp:%f,JsepStats:{...}}
     */
    RTCSessionEvent_StatsReport,

    /**
     * 已打开数据通道,可收发消息
     * 对应 RTCSessionObserver::OnDataChannelOpen
     *  {JsepChannelId:%s,JsepChannelConfig:{...}}
     */
    RTCSessionEvent_DataChannelOpen,

    /**
     * 收到数据
     * 对应 RTCSessionObserver::OnDataChannelMessage
     *  {JsepChannelId:%s,JsepMessage:%s}
     */
    RTCSessionEvent_DataChannelMessage,

    /**
     * 已关闭数据通道,不能再使用JSEP_SendMessage函数
     * 对应 RTCSessionObserver::OnDataChannelClose
     *  {JsepChannelId:%s,JsepReason:%s}
     */
    RTCSessionEvent_DataChannelClose,
};

#ifdef __cplusplus
/**
 * 会话过程侦听器, 除Windows平台外都将在独立线程中回调.
 * C++中通过继承该接口,使用回调方式，避免JSON的多余消耗.
 */
struct RTCSessionObserver {
public:
    /**
     * 创建SDP成功
     * @see RTCSessionEvent_CreateDescription
     *
     * @param[in] type SDP类型,可选值为"offer","answer"
     * @param[in] rtcSessionDescription SDP实例, @see RTCSessionDescription
     *
     * @remarks
     *  协商时,需要调用JSEP_SetLocalDescription()设置该SDP
     */
    virtual void JSEP_THIS_CALL OnCreateDescriptionSuccess(const char* type, RTCSessionDescription rtcSessionDescription) = 0;

    /**
     * 创建SDP失败
     * @see RTCSessionEvent_CreateDescription
     *
     * @param[in] type SDP类型,可选值为"offer","answer"
     * @param[in] error 错误原因
     */
    virtual void JSEP_THIS_CALL OnCreateDescriptionFailure(const char* type, const char* error) = 0;

    /**
     * 设置SDP成功
     * @see RTCSessionEvent_SetDescription
     *
     * @param[in] type SDP类型,可选值为"offer","answer"
     * @param[in] locate 位置, 可选值为"local","remote"
     * @param[in] rtcSessionDescription  SDP字符串,@see RTCSessionDescription
     *
     * @remarks
     *  协商时,将该SDP通知对端,
     *  若是"remote","offer",则需要创建对应的"answer",并反馈对端
     */
    virtual void JSEP_THIS_CALL OnSetDescriptionSuccess(const char* type, const char* locate, RTCSessionDescription rtcSessionDescription) = 0;

    /**
     * 设置SDP失败
     * @see RTCSessionEvent_SetDescription
     *
     * @param[in] type SDP类型,可选值为"offer","answer"
     * @param[in] locate 位置, 可选值为"local","remote"
     * @param[in] error 错误原因
     */
    virtual void JSEP_THIS_CALL OnSetDescriptionFailure(const char* type, const char* locate, const char* error) = 0;

    /**
     * 产生ICE地址
     *
     * @param[in] rtcIceCandidate 本地备选地址, @see RTCIceCandidate
     *
     * @see RTCSessionEvent_IceCandidate
     *
     * @remarks
     *  协商时,将该ICE地址通过信令通知对端
     */
    virtual void JSEP_THIS_CALL OnIceCandidate(RTCIceCandidate rtcIceCandidate) = 0;

    /**
     * 反馈ICE连接状态
     * @see RTCSessionEvent_IceConnectionStateChange
     *
     * @param[in] iceState 状态, @see JsepIceConnectionState
     *
     */
    virtual void JSEP_THIS_CALL OnIceConnectionStateChange(const char* iceState) = 0;

    /**
     * 反馈协商状态
     * @see RTCSessionEvent_SignalingChange
     *
     * @param[in] signalingState 状态, @see JsepSignalingState
     */
    virtual void JSEP_THIS_CALL OnSignalingChange(const char* signalingState) = 0;

public:
    /**
     * 已添加对端媒体流
     * @see RTCSessionEvent_AddRemoteStream
     *
     * @param[in] streamId  流Id
     * @param[in] audioTrackCount 音频个数
     * @param[in] videoTrackCount 视频个数
     */
    virtual void JSEP_THIS_CALL OnAddRemoteStream(const char* streamId, int audioTrackCount, int videoTrackCount) = 0;

    /**
     * 已移除对端媒体流
     * @see RTCSessionEvent_RemoveRemoteStream
     */
    virtual void JSEP_THIS_CALL OnRemoveRemoteStream(const char* streamId) = 0;

    /**
     * 需要进行重新协商
     * @see RTCSessionEvent_RenegotiationNeeded
     */
    virtual void JSEP_THIS_CALL OnRenegotiationNeeded(void) = 0;

public:
    /**
     * DTMF 音符已发送
     * @see RTCSessionEvent_ToneChange
     *
     * @param[in] tone 已发送音符.若为""都表示已发送完毕
     */
    virtual void JSEP_THIS_CALL OnToneChange(const char* tone) = 0;

    /**
     * 统计报告
     * @see RTCSessionEvent_StatsReport
     *
     * @param[in] statsType 统计类别,@see JsepStatsType
     * @param[in] statsId 统计ID
     * @param[in] stats 详细数据,JSON格式, @see RTCStats
     * @param[in] timestamp 时间点
     */
    virtual void JSEP_THIS_CALL OnStatsReport(const char* statsType, const char* statsId, RTCStats stats, const double timestamp) = 0;

public:
    /**
     * 已打开数据通道,可收发消息
     * @see RTCSessionEvent_DataChannelOpen
     *
     * @param[in] channelId 通道ID
     * @param[in] config 通道相应的JSON配置,@see RTCDataChannelInit
     */
    virtual void JSEP_THIS_CALL OnDataChannelOpen(const char* channelId, RTCDataChannelInit config) = 0;

    /**
     * 收到对端数据
     * @see RTCSessionEvent_DataChannelMessage
     *
     * @param[in] channelId 通道ID
     * @param[in] buffer 消息
     */
    virtual void JSEP_THIS_CALL OnDataChannelMessage(const char* channelId, const char* buffer, int length) = 0;

    /**
     * 已关闭数据通道
     * @see RTCSessionEvent_DataChannelClose
     *
     * @param[in] channelId 通道ID
     * @param[in] reason 关闭原因
     */
    virtual void JSEP_THIS_CALL OnDataChannelClose(const char* channelId, const char* reason) = 0;

protected:
    virtual ~RTCSessionObserver(){}
};

/**
 * RTCSocket事件侦听器
 */
struct RTCSocketObserver {
public:
    /** 状态改变
     * 
     * @param[in] state 状态值
     *
     * @remarks
     *  状态可选值
     *  new
     *  checking
     *  completed
     *  connected[ local=candidate;remote=candidate]
     *  open[ protocol]
     *  closed
     *  failed
     */
    virtual void JSEP_THIS_CALL OnSocketStateChange(RTCSocket* rtcsocket, const char* state) = 0;

    /**
     * 产生ICE地址
     *
     * @param[in] candidate 地址实例
     *
     * @remarks
     *  协商时,将该ICE地址通过信令通知对端
     */
    virtual void JSEP_THIS_CALL OnSocketIceCandidate(RTCSocket* rtcsocket, const char* candidate) = 0;

    /** 收到服务器数据
     *
     * @param[in] buffer 消息
     */
    virtual void JSEP_THIS_CALL OnSocketMessage(RTCSocket* rtcsocket, const char* buffer, int length) = 0;

protected:
    virtual ~RTCSocketObserver(){}
};

#else
typedef struct RTCSessionObserver RTCSessionObserver;
typedef struct RTCSocketObserver  RTCSocketObserver;
#endif //__cplusplus

#ifdef __OBJC__
/**
 * RTCSessionEvent 以 NSNotification 方式的广播.
 * NSNotification.userInfo 与 JSON 格式 相对应
 * 例如,事件RTCSessionEvent值的字典关键名为 JespEvent
 */
#define RTCSessionNotification "RTCSessionNotification"

/**
 * RTCSocketEvent 以 NSNotification 方式的广播.
 * NSNotification.userInfo 中,
 * 事件RTCSessionEvent值(NSNumber)的字典关键名为 JespEvent
 * 数据data,length(NSData) 的字典关键名为 JsepMessage
 * 对象RTCSocket*(NSValue)的字典关键名为'JsepSocket'
 */
#define RTCSocketNotification "RTCSocketNotification"

#endif //__OBJC__

/** @} */
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup 协商流程
 *
 *  本地流程:
 *      - JSEP_AddLocalStream()         添加媒体流
 *      - JSEP_CreateOffer()            创建offer SDP
 *      - JSEP_SetLocalDescription()    设置offer SDP
 *      - 将 offer SDP 通知对端
 *
 *  对端流程:
 *      - 收到offer SDP
 *      - JSEP_AddLocalStream()         添加媒体流
 *      - JSEP_SetRemoteDescription()   设置offer SDP
 *      - JSEP_CreateAnswer()           创建answer SDP
 *      - JSEP_SetLocalDescription()    设置answer SDP
 *      - 将 answer SDP 反馈对端
 *
 *  本地流程
 *      - 收到answer SDP
 *      - JSEP_SetRemoteDescription()   设置answer SDP
 * @{
 */

/**
 * 添加本地媒体流
 *
 * @param[in] iface P2P通信实例
 * @param[in] streamId 媒体流ID
 * @param[in,out] bAudio 是否含音频
 * @param[in,out] bVideo 是否含视频
 * @param[in] constraints 媒体流的限制参数,@see MediaStreamConstraints
 *
 * @return 成功返回0, bAudio, bVideo分别指示是否含音视频
 *
 * @@remarks
 *  本地媒体流通常在协商前添加,对端媒体流是自动添加的.
 *  streamId 要保证唯一性,通常用账户ID.
 */
#define JSEP_AddLocalStream(iface,streamId,bAudio, bVideo, constraints) \
    JsepAPI(JSEP_API_LEVEL)->AddLocalStream(iface, streamId, bAudio, bVideo, constraints)

/** 
 * 移除本地媒体流
 *
 * @param[in] iface P2P通信实例
 * @param[in] streamId 媒体流ID,若""或nullptr则移除所有本地流
 */
#define JSEP_RemoveLocalStream(iface, streamId) \
    JsepAPI(JSEP_API_LEVEL)->RemoveLocalStream(iface, streamId)

/**
 * 将媒体流发布到ZMF中
 *
 * @param[in] iface P2P通信实例
 * @param[in] renderOrCapturerBits 按位将视频轨道作为0-ZMF渲染,1-ZMF镜头
 * @param[in] videoTrackMask  上个参数renderOrCapturerBits中的有效位掩码
 *
 * @return 成功返回0
 */
#define JSEP_PublishRemoteStream(iface, streamId, renderOrCapturerBits, videoTrackMask) \
    JsepAPI(JSEP_API_LEVEL)->PublishRemoteStream(iface, streamId, renderOrCapturerBits, videoTrackMask)

/**
 * 添加对端的ICE备选地址
 *
 * @param[in] iface P2P通信实例
 * @param[in] rtcIceCandidate 对端地址, @see RTCIceCandidate
 *            通常对端由 RTCSessionEvent_IceCandidate 事件的JsepIceCandidate字段获得
 *
 * @return 成功返回0
 */
#define JSEP_AddIceCandidate(iface, rtcIceCandidate) \
    JsepAPI(JSEP_API_LEVEL)->AddIceCandidate(iface, rtcIceCandidate)

/**
 * 创建offer SDP
 *
 * @param[in] iface P2P通信实例
 * @param[in] rtcOfferOptions 可选的SDP的限制参数, @see RTCOfferOptions
 *
 * @return 无效参数立即返回-1,反之异步过程,结果由RTCSessionEvent_CreateDescription 事件返回.
 */
#define JSEP_CreateOffer(iface, rtcOfferOptions) \
    JsepAPI(JSEP_API_LEVEL)->CreateOffer(iface, rtcOfferOptions)

/**
 * 配置本地(接收)的SDP
 *
 * @param[in] iface P2P通信实例
 * @param[in] rtcSessionDescription SDP的JSON串, @see RTCSessionDescription
 *            通常本地由 RTCSessionEvent_CreateDescription 事件的JsepSdp获得
 *
 * @return 无效参数立即返回-1,反之异步过程,结果由 RTCSessionEvent_EventSetDescription 事件返回.
 */
#define JSEP_SetLocalDescription(iface, rtcSessionDescription) \
    JsepAPI(JSEP_API_LEVEL)->SetLocalDescription(iface, rtcSessionDescription)

/**
 * 配置对端(发送)的SDP
 *
 * @param[in] iface P2P通信实例
 * @param[in] rtcSessionDescription SDP的JSON串,@see RTCSessionDescription
 *            通常对端由 RTCSessionEvent_CreateDescription 事件的JsepSdp获得
 *
 * @return 无效参数立即返回-1,反之异步过程,结果由RTCSessionEvent_SetDescription 事件返回.
 */
#define JSEP_SetRemoteDescription(iface, rtcSessionDescription) \
    JsepAPI(JSEP_API_LEVEL)->SetRemoteDescription(iface, rtcSessionDescription)

/**
 * 创建 answer SDP
 *
 * @param[in] iface P2P通信实例
 * @param[in] rtcAnswerOptions SDP的限制参数,@see JSEP_CreateOffer
 *
 * @return 无效参数立即返回-1,反之异步过程,结果由 RTCSessionEvent_CreateDescription 事件返回.
 */
#define JSEP_CreateAnswer(iface,rtcAnswerOptions) \
    JsepAPI(JSEP_API_LEVEL)->CreateAnswer(iface, rtcAnswerOptions)

/** @} */
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup 通用数据通道
 *
 *  - 创建数据通道,将需要重新进行协商过程
 *  - 对端将自动创建相应的数据通道.
 *  - 本地和对端都将收到 RTCSessionEvent_DataChannelOpen 事件,之后方可使用该通道.
 *  - 当 RTCSessionEvent_DataChannelClose 事件后,通道已失效
 *
 * @{
 */

/**
 * 动态创建数据通道
 *
 * @param[in] iface P2P通信实例
 * @param[in] channelId 通道ID
 * @param[in] rtcDataChannelInit 配置参数, @see RtcDataChannelInit
 *
 * @return 成功返回0
 */
#define JSEP_CreateDataChannel(iface, channelId, rtcDataChannelInit) \
    JsepAPI(JSEP_API_LEVEL)->CreateDataChannel(iface, channelId, rtcDataChannelInit)

/**
 * 关闭数据通道
 *
 * @param[in] iface P2P通信实例
 * @param[in] channelId 通道ID
 */
#define JSEP_CloseDataChannel(iface, channelId) \
    JsepAPI(JSEP_API_LEVEL)->CloseDataChannel(iface, channelId)

/**
 * 发送自定义数据
 *
 * @param[in] iface P2P通信实例
 * @param[in] channelId 通道ID
 * @param[in] message 消息
 * @param[in] length  消息长度, 非正数,则表示message字符串长度
 *
 * @return 成功返回0
 *
 * @remarks
 *  必须在 RTCSessionEvent_DataChannelOpen 事件后,通道才能收发数据
 */
#define JSEP_SendMessage(iface, channelId, message, length) \
    JsepAPI(JSEP_API_LEVEL)->SendMessage(iface, channelId, message, length)

/** @} */
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup 创建实例
 * @{
 */

/**
 * 内部'全局音频流泵'ID,虚拟的输入/输出设备.
 * 外部与之对接音频时,须使用该ID
 */
#define JSEP_AUDIO_PUMP     " jsepAudioPump"

/**
 * 创建P2P通信实例
 *
 * @param[in] rtcConfiguration  RTC配置参数, @see RTCConfiguration
 * @param[in] zmfAudioPump 是否使用内置的'全局音频泵',可与外部音频流对接, @see JSEP_AUDIO_PUMP
 * @param[in] isCaller 是否为主叫
 * @param[in] observer 会议侦听器
 * @param[in] callback 统一的JSON事件回调函数,除Windows平台外都将在子线程中回调
 *
 * @return 成功返回P2P通信实例
 *
 * @remarks
 *  释放需使用JSEP_Release()
 *  callback有效时,将observer作为其参数,被优先使用,否则直接回调observer.
 */
#define JSEP_RTCPeerConnection(rtcConfiguration, zmfAudioPump, isCaller, observer, callback)\
    JsepAPI(JSEP_API_LEVEL)->CreatePeerConnection(rtcConfiguration, zmfAudioPump, isCaller, observer, callback)

/**
 * 关闭并释放P2P通信实例
 *
 * @param[in] iface P2P通信实例.执行后,该对象失效
 *
 * @return 返回创建时设置的 observer
 */
#define JSEP_Release(iface) \
    JsepAPI(JSEP_API_LEVEL)->ReleasePeerConnection(iface)

/** @} */
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup 额外功能
 * @{
 */

/**
 * 发送DTMF
 *
 * @param[in] iface P2P通信实例
 * @param[in] tones DMTF的音符,0-9,A-D或a-d,#,*. 忽略无法识别的字符.
 * @param[in] duration_ms 每个音符的持续毫秒MS,不能超过6000或小于70
 * @param[in] inter_tone_gap 音符间隔,至少为50ms,但应尽可能短
 *
 * @return 成功返回0,将触发 RTCSessionEvent_ToneChange 事件.
 *
 * @remarks
 *  队列化发送DTMF 任务.
 *  ','表示延迟2秒处理下一个字符
 *  若调用时,上次仍在运行,则之前的任务将被取消
 */
#define JSEP_InsertDtmf(iface, tones, duration_ms, inter_tone_gap) \
    JsepAPI(JSEP_API_LEVEL)->InsertDtmf(iface, tones, duration_ms, inter_tone_gap)

/**
 * 获取统计
 *
 * @param[in] iface P2P通信实例
 * @param[in] statsType 统计类别, @see JsepStatsType
 * @param[in] statsFlags  统计标识, @see RTCStatsFlag
 *
 * @remarks
 *  statsType 有如下情况
 *  - ''或0表示获取所有统计
 *  - 若含 RTCStatsFlag_Audio 或 RTCStatsFlag_Video 时, statsType 作为streamId
 *  - 指定类别, 多个由','分开
 *
 * @return 成功返回0, 将触发 RTCSessionEvent_StatsReport 事件.
 */
#define JSEP_GetStats(iface, statsType, statsFlags) \
    JsepAPI(JSEP_API_LEVEL)->GetStats(iface, statsType, statsFlags)


/**
 * 开始/停止记录通信事件
 *
 * @param[in] iface P2P通信实例
 * @param[in] filename 日志文件名,空则停止记录
 * @param[in] max_size_mb 日志文件允许大小MB
 *
 * @return 成功返回0
 *
 * @remarks
 *  日志文件符合 rtc_event_log.proto 定义的二进制格式,
 *  可由其他工具解析.
 */
#define JSEP_LogRtcEvent(iface, filename, max_size_mb) \
    JsepAPI(JSEP_API_LEVEL)->LogRtcEvent(iface, filename, max_size_mb)

/**
 * 开始/停止记录音频处理过程
 *
 * @param[in] filename 日志文件名,空则停止记录
 * @param[in] max_size_mb 日志文件允许大小MB
 *
 * @return 成功返回0
 *
 * @remarks
 *  日志文件符合 debug.proto 定义的二进制格式,
 *  可由其他工具解析.
 */
#define JSEP_DumpAudioProcessing(filename, max_size_mb) \
    JsepAPI(JSEP_API_LEVEL)->DumpAudioProcessing(filename, max_size_mb)

/**
 * 手工设置发送码率
 *
 * @param[in] iface P2P通信实例
 * @param[in] current_bitrate_bps  当前码率,若不大于0,则不设置
 * @param[in] max_bitrate_bps 允许的最大码率,若不大于0,则不设置
 * @param[in] min_bitrate_bps 允许的最小码率,若不大于0,则不设置
 *
 * @return 成功返回0
 */
#define JSEP_SetBitrate(iface, current_bitrate_bps, max_bitrate_bps, min_bitrate_bps) \
    JsepAPI(JSEP_API_LEVEL)->SetBitrate(iface, current_bitrate_bps, max_bitrate_bps, min_bitrate_bps)

/**
 * 返回最近出错的描述
 *
 * @return 返回描述字符串,若无出错, 则是""
 */
#define JSEP_LastErrorDescription() JsepAPI(JSEP_API_LEVEL)->LastErrorDescription()

/** @} */
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup RTCSocket 功能
 * @{
 */

/**
 * 创建 WebSocket 客户端并连接到服务器
 * WebSocket 是可靠的TCP传输
 * 
 * @param[in] wsURL WebSocket服务器地址,例如'ws://example.com/path:7000' 或 'wss://192.168.0.22:7000'
 * @param[in] rtcWebSocketInit 配置参数, @see RTCWebSocketInit
 * @param[in] observer 侦听器
 * @param[in] callback 统一的回调函数,除Windows平台外都将在子线程中回调
 *
 * @return 返回WebSocket实例,失败返回nullptr
 *
 * @remarks
 *  释放实例 需使用RTCSocket_Close()
 *  callback有效时,将observer作为其参数,被优先使用,否则直接回调observer.
 */
#define WebSocket_Connect(wsURL, rtcWebSocketInit, observer, callback) \
    JsepAPI(JSEP_API_LEVEL)->CreateWebSocket(wsURL, rtcWebSocketInit, observer, callback)

/**
 * 创建 WebSocket 服务器并侦听客户的连接
 * WebSocket 是可靠的TCP传输
 * 
 * @param[in] wsURL 侦听地址,例如'ws://:7000' 或 'wss://192.168.0.22:7000'
 * @param[in] rtcWebSocketInit 配置参数, @see RTCWebSocketInit
 * @param[in] observer 侦听器
 * @param[in] callback 统一的回调函数,除Windows平台外都将在子线程中回调
 *
 * @return 返回WebSocket实例,失败返回nullptr
 *
 * @remarks
 *  停止侦听并释放实例 需使用RTCSocket_Close()
 *  callback有效时,将observer作为其参数,被优先使用,否则直接回调observer.
 *  侦听到新连接的改变,都将触发 RTCSocketEvent_StateChange, 其中 rtcsocket 为该连接的服务实例
 *      - 收到新的连接, state 是 'new', 
 *      - 连接建立成功, state 是 'open[ protocol]'
 *      - 连接断开, state 是 'closed', 此时需要调用 RTCSocket_Close(rtcsocket) 关闭该连接
 *  连接中,收到数据, 将触发 RTCSocketEvent_Message, 其中 rtcsocket 为该连接的服务实例
 */

#define WebSocket_Listen(wsURL, rtcWebSocketInit, observer, callback) \
    JsepAPI(JSEP_API_LEVEL)->CreateWebSocketServer(wsURL, rtcWebSocketInit, observer, callback)

/**
 * 创建 IceSocket
 *
 * IceSocket 是不可靠的多路径 P2P 传输
 * 
 * @param[in] rtcConfiguration  STUN/TRUN 服务器参数, @see RTCConfiguration 
 * @param[in] rtcIceParameters  ICE参数,@see RTCIceParameters
 * @param[in] isCaller  是否主叫方
 * @param[in] observer 侦听器
 * @param[in] callback 统一的回调函数,除Windows平台外都将在独立线程中回调
 *
 * @return 返回IceSocket实例,失败返回nullptr
 *
 * @remarks
 *  释放实例 需使用RTCSocket_Close()
 *  callback有效时,将observer作为其参数,被优先使用,否则直接回调observer.
 */
#define IceSocket_Connect(rtcConfiguration, rtcIceParameters, isCaller, observer, callback) \
    JsepAPI(JSEP_API_LEVEL)->CreateIceSocket(rtcConfiguration, rtcIceParameters, isCaller, observer, callback)

/**
 * 关闭 RTCSocket
 *
 * @param[in] rtcSocket RTCSocket实例, 执行后,该对象失效.
 * @return 返回创建时设置的 observer
 */
#define RTCSocket_Close(rtcSocket) \
    JsepAPI(JSEP_API_LEVEL)->CloseSocket(rtcSocket)

/**
 * 发送消息
 *
 * @param[in] rtcSocket RTCSocket实例
 * @param[in] message 消息
 * @param[in] length 消息长度,非正数,则表示message字符串长度
 * @param[in] sendFlags 发送标识, @see RTCSocketFlag
 *
 * @return 成功返回发送长度,反之错误码
 *
 * @remarks
 */
#define RTCSocket_Send(rtcSocket, message, length, sendFlags) \
    JsepAPI(JSEP_API_LEVEL)->SendSocket(rtcSocket, message, length, sendFlags)

/**
 * 添加对端的ICE地址
 *
 * @param[in] iceSocket IceSocket实例
 * @param[in] candidate 对端地址
 *
 * @return 成功返回0,反之错误码
 */
#define IceSocket_AddRemoteCandidate(iceSocket, candidate) \
    JsepAPI(JSEP_API_LEVEL)->AddSocketIceCandidate(iceSocket, candidate)

/**
 * 设置对端的ICE参数
 *
 * @param[in] iceSocket IceSocket实例
 * @param[in] rtcIceParameters 对端参数, @see RTCIceParameters
 *
 * @return 成功返回0,反之错误码
 */

#define IceSocket_SetRemoteParameters(iceSocket, rtcIceParameters) \
    JsepAPI(JSEP_API_LEVEL)->SetSocketIceParameters(iceSocket, rtcIceParameters)

/** @} */
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup JSON 功能
 * @{
 */
enum JsonForm {
    /** 不带引号,具备原子性的JSON对象 */
    JsonForm_Primitive      = 1,
    /** 字符串 */
    JsonForm_String         = 2,
    /** 数组对象 */
    JsonForm_Array          = 3,
    /** 复合的字典对象 */
    JsonForm_Object         = 4,
};

enum JsonError {
    /** JSON对象表述符数组tokens, 长度不够 */
    JsonError_Insufficient = -1,
    /** 非法的JSON字符串 */
    JsonError_Invalid    = -2,
    /** 不是完整的JSON字符串 */
    JsonError_Partial    = -3,
};
/** 解析器.  必须以0,初始化 */
typedef struct { int _[3]; } JsonParser;
typedef struct JsonValue  JsonValue;

/**
 * 解析 JSON 字符串
 * 必须保证 有足够长度的 jsonValue
 *
 * @param[in] json 字符串
 * @param[in] jsonValue 预分配数组
 *
 * @return 成功返回个数, 返回错误值
 */
#define Json_ParseString(json, jsonValue) \
    JsepAPI(JSEP_API_LEVEL)->ParseJson(json, 0, (JsonParser*)0, jsonValue, sizeof(jsonValue)/sizeof(JsonValue));

/**
 * 完整版的解析
 *
 * @param[in] json 字符串
 * @param[in] n_json 字符串长度
 * @param[in] jsonParser 解析状态
 * @param[in] jsonValue 预分配数组
 * @param[in] size 预分配个数
 * 
 * @return 成功返回个数,返回错误值
 *
 * @example 
 *  JsonValue token[256], *root=token;
 *  int r, n= sizeof(token)/sizeof(JsonValue);
 *  {
 *      JsonParser parse={{0}};
 *      do {
 *          r = Json_Parse(json, n_json, &parse, root, n);
 *          if (r == JsonError_Insufficient) {//预分配不足
 *              n *= 2;
 *              if (root == token)
 *                  root = (JsonValue*)memcpy(malloc(sizeof(JsonValue)*n), token, sizeof(token));
 *              else
 *                  root = (JsonValue *)realloc(t, sizeof(JsonValue)*n);
 *          }
 *          else if (r > 0)
 *              break;
 *          else {//出错
 *              if (root != token) free(root);
 *              return false;
 *          }
 *      } while(1);
 *  }
 *  .......
 *
 *  if (root != token) free(root);
 *  return true;
 */
#define Json_Parse(json, n_json, jsonParser, jsonValue, size) \
    JsepAPI(JSEP_API_LEVEL)->ParseJson(json, n_json, jsonParser, jsonValue, size)

/**
 * 返回 JSON 对应的字符串
 * 自动处理转义情况
 *
 * @param[in] jsonValue JSON 对象
 * @param[out] buf  输出的字符串,必须有足够长度 jsonValue->n_json
 * @return 返回写入的长度, 不含末尾'\0'
 */
#define Json_Unescape(jsonValue, buf) \
    JsepAPI(JSEP_API_LEVEL)->UnescapeJson(jsonValue, buf)

/**
 * JSON 对象与字符串比较
 * 自动处理转义情况
 *
 * @param[in] jsonValue JSON 对象
 * @param[in] str 字符串
 * @return 类似strcmp, 0表示相等
 */
#define Json_Compare(jsonValue, str) \
    JsepAPI(JSEP_API_LEVEL)->CompareJson(jsonValue, str, 0)

/** 同上, 允许指定字符串长度 */
#define Json_Compare2(jsonValue, str, n_str) \
    JsepAPI(JSEP_API_LEVEL)->CompareJson(jsonValue, str, n_str)

/**
 * 返回 JSON 表或组的指定元素
 *
 * @param[in] jsonValue JSON 表对象或组对象
 * @param[in] index  元素的序号(从0开始), -1表示末尾
 *
 * @return 对应的 JSON 对象, 失败则返回无效对象, 肯定不会是 nullptr
 */
#define Json_Child(jsonValue, index) \
    JsepAPI(JSEP_API_LEVEL)->ChildJson(jsonValue, index, (const char*)0, 0)

/**
 * 查找 JSON 表中关键词对应的值
 *
 * @param[in] jsonValue JSON 表对象
 * @param[in] key  关键词
 *
 * @return 对应的 JSON 对象, 失败则返回无效对象, 肯定不会是 nullptr
 */
#define Json_Value(jsonValue, key) \
    JsepAPI(JSEP_API_LEVEL)->ChildJson(jsonValue, 0, key, 0)

/** 同上, 允许指定关键词长度 */
#define Json_Value2(jsonValue, key, n_key) \
    JsepAPI(JSEP_API_LEVEL)->ChildJson(jsonValue, 0, key, n_key)

/** @} */
////////////////////////////////////////////////////////////////////////////////
/**
 * @defgroup FilePlay 功能
 * @{
 */
#define FilePlay_Open(filePath, userdata, pfnVideo, pfnAudio) \
    JsepAPI(JSEP_API_LEVEL)->OpenFile(filePath, userdata, pfnVideo, pfnAudio)

#define FilePlay_GetInfo(filePath, name, val) \
    JsepAPI(JSEP_API_LEVEL)->GetFileInfo(filePath, name, val)

#define FilePlay_Close(filePath) \
    JsepAPI(JSEP_API_LEVEL)->CloseFile(filePath)

#define FilePlay_ReadAudio(filePath, samplingHz, channcels, pcmBuf, len) \
    JsepAPI(JSEP_API_LEVEL)->ReadFileAudio(filePath, samplingHz, channcels, pcmBuf, len)

#define FilePlay_Pause(filePath) \
    JsepAPI(JSEP_API_LEVEL)->PauseFilePlay(filePath)

#define FilePlay_Resume(filePath) \
    JsepAPI(JSEP_API_LEVEL)->ResumeFilePlay(filePath)

#define FilePlay_Seek(filePath, offsetMs, origin) \
    JsepAPI(JSEP_API_LEVEL)->SeekFile(filePath, offsetMs, origin)

/** @} */
////////////////////////////////////////////////////////////////////////////////
typedef struct {
    const char* (JSEP_CDECL_CALL *LastErrorDescription)();

    // media stream
    int (JSEP_CDECL_CALL *AddLocalStream) (RTCPeerConnection* iface, const char* streamId, RTCBoolean* bAudio, RTCBoolean *bVideo, MediaStreamConstraints constraints);
    void (JSEP_CDECL_CALL *RemoveLocalStream) (RTCPeerConnection* iface, const char* streamId);
    int (JSEP_CDECL_CALL *PublishRemoteStream)(RTCPeerConnection* iface, const char* streamId, int renderOrCapturerBits, int videoTrackMask);

    // data channel
    int (JSEP_CDECL_CALL *CreateDataChannel) (RTCPeerConnection* iface, const char* channelId, RTCDataChannelInit rtcDataChannelInit);
    void (JSEP_CDECL_CALL *CloseDataChannel) (RTCPeerConnection* iface, const char* channelId);
    int (JSEP_CDECL_CALL *SendMessage) (RTCPeerConnection* iface, const char* channelId, const char* buffer, int length);

    // peer
    RTCPeerConnection* (JSEP_CDECL_CALL *CreatePeerConnection)(RTCConfiguration rtcConfiguration, RTCBoolean zmfAudioPump, RTCBoolean isCaller, RTCSessionObserver* userdata,
        void (JSEP_CDECL_CALL *observer)(RTCSessionObserver*userdata, enum RTCSessionEvent event, const char* json, int length));
    RTCSessionObserver* (JSEP_CDECL_CALL *ReleasePeerConnection) (RTCPeerConnection* iface);

    // negotiation
    int (JSEP_CDECL_CALL *CreateAnswer) (RTCPeerConnection* iface, RTCAnswerOptions rtcAnswerOptions);
    int (JSEP_CDECL_CALL *CreateOffer) (RTCPeerConnection* iface, RTCOfferOptions rtcOfferOptions);
    int (JSEP_CDECL_CALL *AddIceCandidate) (RTCPeerConnection* iface, RTCIceCandidate rtcIceCandidate);
    int (JSEP_CDECL_CALL *SetLocalDescription) (RTCPeerConnection* iface, RTCSessionDescription rtcSessionDescription);
    int (JSEP_CDECL_CALL *SetRemoteDescription) (RTCPeerConnection* iface, RTCSessionDescription rtcSessionDescription);

    // dynamic setting
    int (JSEP_CDECL_CALL *InsertDtmf) (RTCPeerConnection* iface, const char* tones, int duration_ms, int inter_tone_gap);
    int (JSEP_CDECL_CALL *SetBitrate)(RTCPeerConnection* iface, int current_bitrate_bps, int max_bitrate_bps, int min_bitrate_bps);

    // debug
    int (JSEP_CDECL_CALL *GetStats) (RTCPeerConnection* iface, const char* statsType, int statsFlags);
    int (JSEP_CDECL_CALL *LogRtcEvent)(RTCPeerConnection* iface, const char* filename, int max_size_mb);
    int (JSEP_CDECL_CALL *DumpAudioProcessing)(const char* filename, int max_size_mb);

    // socket
    RTCSocket* (JSEP_CDECL_CALL *CreateWebSocket)(const char* wsURL, RTCWebSocketInit rtcWebSocketInit, RTCSocketObserver* userdata,
        void (JSEP_CDECL_CALL *observer)(RTCSocketObserver* userdata, RTCSocket* rtcsocket, const char* message, int length, enum RTCSocketEvent event));
    RTCSocket* (JSEP_CDECL_CALL *CreateWebSocketServer)(const char* wsURL, RTCWebSocketInit rtcWebSocketInit, RTCSocketObserver* userdata,
        void (JSEP_CDECL_CALL *observer)(RTCSocketObserver* userdata, RTCSocket* rtcsocket, const char* message, int length, enum RTCSocketEvent event));
    RTCSocket* (JSEP_CDECL_CALL *CreateIceSocket)(RTCConfiguration rtcConfiguration, RTCIceParameters rtcIceParameters, RTCBoolean isCaller, RTCSocketObserver* userdata,
        void (JSEP_CDECL_CALL *observer)(RTCSocketObserver* userdata, RTCSocket* rtcsocket, const char* message, int length, enum RTCSocketEvent event));

    RTCSocketObserver* (JSEP_CDECL_CALL *CloseSocket)(RTCSocket* rtcsocket);
    int (JSEP_CDECL_CALL *SendSocket)(RTCSocket* rtcsocket, const char* message, int length, int sendFlags);

    // ice socket
    int (JSEP_CDECL_CALL *AddSocketIceCandidate)(RTCSocket* rtcsocket, const char* candidate);
    int (JSEP_CDECL_CALL *SetSocketIceParameters)(RTCSocket* rtcsocket, RTCIceParameters rtcIceParameters);

    // json
    int (JSEP_CDECL_CALL *ParseJson)(const char *json, int len, JsonParser *parser, JsonValue* tokens, int n_tokens);
    const JsonValue* (JSEP_CDECL_CALL *ChildJson)(const JsonValue* jsonValue, int index, const char* key, int n_key);
    int (JSEP_CDECL_CALL *CompareJson)(const JsonValue* jsonValue, const char* str, int n_str);
    int (JSEP_CDECL_CALL *UnescapeJson)(const JsonValue* jsonValue, char* buf);

    //file play
    int (JSEP_CDECL_CALL *OpenFile)(const char *filePath, void *userdata,
        void (*pfnVideo)(void *userdata, int playedMs, int width, int height, void *i420Buf),
        void (*pfnAudio)(void *userdata, int playedMs, int samplingHz, int channels, void *pcmBuf, int len));
    const char* (JSEP_CDECL_CALL *GetFileInfo)(const char *filePath, const char *name, int* value);
    void (JSEP_CDECL_CALL *CloseFile)(const char *filePath);
    int (JSEP_CDECL_CALL *SeekFile)(const char *filePath, int offsetMs, int origin);
    int (JSEP_CDECL_CALL *ReadFileAudio)(const char *filePath, int samplingHz, int channcels, void *pcmBuf, int len);
    int (JSEP_CDECL_CALL *PauseFilePlay)(const char *filePath);
    int (JSEP_CDECL_CALL *ResumeFilePlay)(const char *filePath);

} JSEP_API;

#if defined(JSEP_IMPORT) || defined(_BUILD_JSEP_API_LEVEL__)
JSEP_PUBLIC const JSEP_API* JSEP_CDECL_CALL JsepAPI(int apiLevel);
#else
JSEP_INLINE const JSEP_API* JsepAPI(int apiLevel) {
    typedef const JSEP_API* (JSEP_CDECL_CALL *PFN_JSEPAPI)(int);
    static PFN_JSEPAPI pfn = (PFN_JSEPAPI) 0;
    if (!pfn) do {
        void* handle = 0;
        char folderpath[2048] = {0};
#ifdef _WIN32
        strcpy(folderpath, "jsep.dll");
        handle = (void*)LoadLibraryA(folderpath);
#else
        strcpy(folderpath, "libjsep.so");
        handle = (void*)dlopen(folderpath, RTLD_LOCAL|RTLD_LAZY);
#endif
        if (!handle) {
#ifdef __linux__
            if (readlink ("/proc/self/exe", folderpath, sizeof(folderpath)) != -1) {
                char* p = strrchr(folderpath, '/');
                strcpy(p, "/libjsep.so");
                handle = (void*)dlopen(folderpath, RTLD_LOCAL|RTLD_LAZY);
            }
#elif defined(TARGET_OS_MAC)
            char *p;
            char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
            char folderpath[PROC_PIDPATHINFO_MAXSIZE];
            pid_t pid = getpid();
            proc_pidpath (pid, pathbuf, sizeof(pathbuf));
            realpath(pathbuf, folderpath);
            p = strrchr(folderpath, '/');
            strcpy(p, "/libjsep.dylib");
            handle = (void*)dlopen(folderpath, RTLD_LOCAL|RTLD_LAZY);
            if (!handle) {
                strcpy(p, "/../Frameworks/libjsep.dylib");
                handle = (void*)dlopen(folderpath, RTLD_LOCAL|RTLD_LAZY);
            }
#endif
        }
        if (handle) {
#ifdef _WIN32
            pfn = (PFN_JSEPAPI)GetProcAddress((HMODULE)handle, "JsepAPI");
#else
            pfn = (PFN_JSEPAPI)dlsym(handle, "JsepAPI");
#endif
        }
        if (!pfn){
            void* str;
#ifndef _WIN32
            str = (void*)dlerror();
#else
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&str, 0, NULL);
            OutputDebugStringA((LPCSTR)str);
#endif
            fprintf(stderr, "\n%s\nUnable to load JSEP library: %s\n", (char*)str, folderpath);
            return (const JSEP_API*)0;
        }
    } while(0);
    return pfn(apiLevel);
}
#endif //JSEP_IMPORT
////////////////////////////////////////////////////////////////////////////////
/**
 * JSON对象表述符.
 * @param[in] type 类型, 必须是JSON_OBJECT, JSON_ARRAY, JSON_STRING 之一
 * @param[in] json JSON字符串
 * @param[in] n_json 字符串长度
 * @param[in] n_child 子对象个数
 */
struct JsonValue {
    enum JsonForm type;
    const char* json;
    int n_json;
    int n_child;
    int parent;
#ifdef __cplusplus
    int compare(const std::string& str) const { return JsepAPI(JSEP_API_LEVEL)->CompareJson(this, str.data(), str.length()); }
    const JsonValue& operator[](const std::string& key) const { return *(JsepAPI(JSEP_API_LEVEL)->ChildJson(this, 0, key.data(), key.length())); }
    const JsonValue& operator[](int index) const { return *(JsepAPI(JSEP_API_LEVEL)->ChildJson(this, index, 0, 0)); }
    bool operator==(const std::string& str) const { return compare(str) == 0; }
    bool operator!=(const std::string& str) const { return compare(str) != 0; } 
    operator std::string() const { return str(); }
    static std::string escape (const std::string& str){
        std::string ret;
        ret.reserve(str.length());
        for (std::string::size_type i = 0; i < str.length(); ++i) {
            switch (str[i]) {
            case '"': ret += "\\\""; break;
            case '\b': ret += "\\b"; break;
            case '\f': ret += "\\f"; break;
            case '\n': ret += "\\n"; break;
            case '\r': ret += "\\r"; break;
            case '\t': ret += "\\t"; break;
            case '\\': ret += "\\\\"; break;
            default: ret += str[i]; break;
            }
        }
        return ret;
    }
    static const std::vector<JsonValue> parse(const char* json, int n_json=0) {
        int ret;
        JsonParser p={{0}};
        std::vector<JsonValue> vals(256);
        do {
            ret = JsepAPI(JSEP_API_LEVEL)->ParseJson(json, n_json, &p, &vals[0], vals.size());
            if (ret == JsonError_Insufficient)
                vals.resize(vals.size()*2);
            else{
                if (ret < 0)
                    vals.clear();
                else
                    vals.resize(ret);
                break;
            }
        } while (1);
        return vals;
    }
    const std::string str() const {
        if (type != JsonForm_String)
            return std::string(json, n_json);
        else {
            int i = 0;
            while (i<n_json && json[i]!='\\') ++i;
            if (i == n_json)
                return std::string(json, n_json);
            else {
                std::string ret(json, i);
                do {
                    char ch = json[i++];
                    if (ch == '\\') {
                        switch(json[i++]) {
                        case '\\':ch = '\\'; break;
                        case '"': ch = '\"'; break;
                        case 'f': ch = '\f'; break;
                        case 'n': ch = '\n'; break;
                        case 'r': ch = '\r'; break;
                        case 't': ch = '\t'; break;
                        case '0': ch = '\0'; break;
                        }
                    }
                    ret.push_back(ch);
                } while (i < n_json);
                return ret;
            }
        }
    }
#endif //__cplusplus
};
#endif //__JSEP_H__
