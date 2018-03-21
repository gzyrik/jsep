using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

///<summary> 模仿 WEBRTC 1.0 标准实现1对1的通信能力
///</summary>
public static class JSEP 
{
    ///<summary> JSEP 接口入口,可用于判断是否具备JSEP功能
    ///</summary>
    public static readonly IntPtr JSEP_API;

    ///<summary>RTCSocket 事件</summary>
    public enum RTCSocketEvent
    {
        ///<summary>收到消息</summary>
        Message     = 0,

        ///<summary>状态改变,对应数据为新状态</summary>
        StateChange = 1,

        ///<summary>获取新的本地候选地址, 对应数据为候选地址</summary>
        IceCandidate= 2,
    };

    ///<summary> RTCSocket 客户端
    ///</summary>
    public class RTCSocket : IDisposable
    {
        internal IntPtr iface;
        internal RTCSocket() {}
       
        ///<summary> 销毁所有资源
        ///</summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        ///<summary> 销毁所有资源
        ///</summary>
        ///<param name="isDisposing"> 是否是主动丢弃</param>
        protected virtual void Dispose(bool isDisposing)
        {
            if (iface != IntPtr.Zero){
                OBSERVERS[(int)JSEP._CloseSocket(iface)] = null;
                SOCKETS.Remove(iface);
                iface = IntPtr.Zero;
            }
        }

        ///<summary> 自动销毁
        ///</summary>
        ~RTCSocket()
        {
            Dispose(false);
        }

        ///<summary> 发送数据
        ///</summary>
        ///<param name="message"> 消息 </param>
        ///<returns> 成功返回0 </returns>
        public int Send(string message)
        {
            byte[] buffer = Encoding.UTF8.GetBytes(message);
            return JSEP._SendSocket(iface, buffer, buffer.Length, 0);
        }
    }

    ///<summary> RTCSocket事件侦听回调函数
    ///</summary>
    ///<param name="rtcsocket"> RTCSocket </param>
    ///<param name="message"> 收到消息 </param>
    ///<param name="eventId"> 对应事件 </param>
    public delegate void RTCSocketObserver(RTCSocket rtcsocket, string message, RTCSocketEvent eventId);

    ///<summary> WebSocket 客户端
    ///</summary>
    public class WebSocket : RTCSocket 
    {
        private WebSocket() {}

        private static readonly _RTCSocketEventHandler WebSocketCallback = (user, iface, utf8, len, eventId) =>{
            RTCSocketObserver observer = (RTCSocketObserver)OBSERVERS[(int)user];
            if (observer != null){
                string txt = null;
                if (utf8 != null) txt = Encoding.UTF8.GetString(utf8);
                RTCSocket rtcsocket;
                if (!SOCKETS.TryGetValue(iface, out rtcsocket)){
                    rtcsocket = new WebSocket();
                    rtcsocket.iface = iface;
                    SOCKETS.Add(iface, rtcsocket);
                }
                observer(rtcsocket, txt, (RTCSocketEvent)eventId);
            }
        };

        ///<summary> 创建 WebSocket 客户端并连接到服务器
        /// WebSocket 是可靠的TCP传输
        ///</summary>
        ///<param name="wsURL"> 服务器地址</param>
        ///<param name="rtcWebSocketInit"> 配置参数</param>
        ///<param name="observer"> 事件侦听器</param>
        static public WebSocket Connect(string wsURL, string rtcWebSocketInit, RTCSocketObserver observer)
        {
            if (observer == null)
                throw new ArgumentNullException();

            int index = 1;
            while (index < OBSERVERS.Length && OBSERVERS[index] != null)
                ++index;
            if (index == OBSERVERS.Length)
                throw new OverflowException();

            OBSERVERS[index] = observer;
            IntPtr iface = JSEP._CreateWebSocket(wsURL, rtcWebSocketInit, (IntPtr)index, WebSocketCallback);
            if (iface == IntPtr.Zero) {
                OBSERVERS[index] = null;
                throw new ArgumentException();
            }
            WebSocket self = new WebSocket();
            SOCKETS.Add(iface, self);
            self.iface = iface;
            return self;
        }

        ///<summary> 创建 WebSocket 服务器并侦听客户的连接 
        /// WebSocket 是可靠的TCP传输
        ///</summary>
        ///<param name="wsURL"> 服务器地址</param>
        ///<param name="rtcWebSocketInit"> 配置参数</param>
        ///<param name="observer"> 事件侦听器</param>
        static public WebSocket Listen(string wsURL, string rtcWebSocketInit, RTCSocketObserver observer)
        {
            if (observer == null)
                throw new ArgumentNullException();

            int index = 1;
            while (index < OBSERVERS.Length && OBSERVERS[index] != null)
                ++index;
            if (index == OBSERVERS.Length)
                throw new OverflowException();

            OBSERVERS[index] = observer;
            IntPtr iface = JSEP._CreateWebSocketServer(wsURL, rtcWebSocketInit, (IntPtr)index, WebSocketCallback);
            if (iface == IntPtr.Zero) {
                OBSERVERS[index] = null;
                throw new ArgumentException();
            }
            WebSocket self = new WebSocket();
            SOCKETS.Add(iface, self);
            self.iface = iface;
            return self;
        }
    }

    ///<summary> IceSocket 客户端
    ///</summary>
    public class IceSocket : RTCSocket 
    {
        private IceSocket() {}

        private static readonly _RTCSocketEventHandler IceSocketCallback = (user, iface, utf8, len, eventId) =>{
            RTCSocketObserver observer = (RTCSocketObserver)OBSERVERS[(int)user];
            if (observer != null){
                string txt = null;
                if (utf8 != null) txt = Encoding.UTF8.GetString(utf8);
                RTCSocket rtcsocket;
                if (!SOCKETS.TryGetValue(iface, out rtcsocket)){
                    rtcsocket = new IceSocket();
                    rtcsocket.iface = iface;
                    SOCKETS.Add(iface, rtcsocket);
                }
                observer(rtcsocket, txt, (RTCSocketEvent)eventId);
            }
        };

        ///<summary> 创建 IceSocket
        /// IceSocket 是可靠的TCP传输
        ///</summary>
        ///<param name="rtcConfiguration"> 服务器地址</param>
        ///<param name="rtcIceParameters"> 可选的来源标识</param>
        ///<param name="isCaller"> 是否为主叫</param>
        ///<param name="observer"> 事件侦听器</param>
        static public IceSocket Connect(string rtcConfiguration, string rtcIceParameters, bool isCaller, RTCSocketObserver observer)
        {
            if (observer == null)
                throw new ArgumentNullException();

            int index = 1;
            while (index < OBSERVERS.Length && OBSERVERS[index] != null)
                ++index;
            if (index == OBSERVERS.Length)
                throw new OverflowException();

            OBSERVERS[index] = observer;
            IntPtr iface = JSEP._CreateIceSocket(rtcConfiguration, rtcIceParameters, isCaller, (IntPtr)index, IceSocketCallback);
            if (iface == IntPtr.Zero) {
                OBSERVERS[index] = null;
                throw new ArgumentException();
            }
            IceSocket self = new IceSocket();
            SOCKETS.Add(iface, self);
            self.iface = iface;
            return self;
        }

        ///<summary> 添加对端的ICE地址
        ///</summary>
        ///<param name="candidate">对端地址</param>
        ///<returns> 成功返回0 </returns>
        public int AddRemoteCandidate(string candidate)
        {
            return JSEP._AddSocketIceCandidate(iface, candidate);
        }

        ///<summary> 设置对端的ICE参数
        ///</summary>
        ///<param name="rtcIceParameters">对端参数, @see RTCIceParameters</param>
        ///<returns> 成功返回0 </returns>
        public int SetRemoteParameters(string rtcIceParameters)
        {
            return JSEP._SetSocketIceParameters(iface, rtcIceParameters);
        }
    }
    
    ///<summary> 实时通信中事件侦听回调函数
    ///</summary>
    ///<param name="eventId"> 为事件名,enum JsepEvent类型 </param>
    ///<param name="json"> JSON格式字符串</param>
    public delegate void RTCSessionObserver(RTCSessionEvent eventId, string json);

    ///<summary> 通信中的错误值</summary>
    public enum RTCSessionError
    {
        ///<summary>无效操作,未知错误</summary>
        InvalidOperation    = -1,

        ///<summary>非法的参数使用</summary>
        InvalidArgument     = -2,

        ///<summary>没有视频流</summary>
        MissingVideoTrack   = -3,

        ///<summary>版本不匹配</summary>
        MismatcheaVersion   = -4,
    };

    ///<summary> 通信中的会议事件,携带有JSON格式参数</summary>
    public enum RTCSessionEvent
    {
        ///<summary> 需要进行重新协商
        /// 通常需调JSEP_CreateOffer(),再重复协商流程
        ///</summary>
        RenegotiationNeeded = 1,

        ///<summary> 创建SDP成功
        /// {JsepSdpType:"%s",JsepSdp:{...}}
        /// 协商时,需要调用JSEP_SetLocalDescription()设置该SDP
        ///</summary>
        CreateDescriptionSuccess,

        ///<summary> 创建SDP失败
        /// {JsepSdpType:"%s",JsepReason:"%s"}
        ///</summary>
        CreateDescriptionFailure,

        ///<summary> 设置SDP成功
        /// {JsepSdpType:"%s",JsepSdpLocation:"%s",JsepSdp:{...}}
        /// 若是"remote","offer",则需要创建对应的 answer SDP,并反馈对端
        ///</summary>
        SetDescriptionSuccess,

        ///<summary> 设置SDP失败
        /// {JsepSdpType:"%s",JsepSdpLocation:"%s",JsepReason:"%s"}
        ///</summary>
        SetDescriptionFailure,

        ///<summary> 新的ICE候选地址
        /// {JsepIceCandidate:{...}}
        /// 协商时,将该ICE地址通过信令通知对端
        ///</summary>
        IceCandidate,

        ///<summary> ICE连接状态改变
        /// {JsepIceConnectionState:"%s"}
        ///</summary>
        IceConnectionStateChange,

        ///<summary> 协商状态改变
        /// {JsepSignalingState:"%s"}
        ///</summary>
        SignalingChange,

        ///<summary> 已添加对端媒体流
        /// {JsepStreamId:"%s",JsepAudioTrackCount:%d,JsepVideoTrackCount:%d}
        /// 需要调用JSEP_PublishStream(),发布该流,以便与Zmf对接
        ///</summary>
        AddRemoteStream,

        ///<summary> 已移除对端媒体流
        /// {JsepStreamId:"%s"}
        ///</summary>
        RemoveRemoteStream,

        ///<summary> DTMF 音符已发送
        /// {JsepTone:"%s"}
        ///</summary>
        ToneChange,

        ///<summary> 收到统计报告
        /// {JsepStatsType:"%s",JsepStatsId:"%s",JsepStatsTimestamp:%f,JsepStats:{...}}
        ///</summary>
        StatsReport,

        ///<summary> 已打开数据通道,可收发消息
        /// {JsepChannelId:"%s",JsepChannelConfig:{...}}
        ///</summary>
        DataChannelOpen,

        ///<summary> 收到数据
        /// {JsepChannelId:"%s",JsepMessage:"%s"}
        ///</summary>
        DataChannelMessage,

        ///<summary> 已关闭数据通道
        /// 已关闭数据通道,不能再使用JSEP_SendMessage函数
        /// {JsepChannelId:"%s",JsepReason:"%s"}
        ///</summary>
        DataChannelClose,
    }

    ///<summary> P2P通信实例
    ///</summary>
    public class RTCPeerConnection : IDisposable
    {
        private IntPtr iface;

        ///<summary> 创建P2P通信实例
        ///</summary>
        ///<param name="config"> 配置参数,JSON格式. 例如{'iceServers': [{'url': url}]}</param>
        ///<param name="zmfAudioPump"> 是否使用内置的'全局音频泵',可与外部音频流对接, @see JSEP_AUDIO_PUMP</param>
        ///<param name="isCaller"> 是否为主叫</param>
        ///<param name="observer"> 事件侦听器</param>
        ///<remarks>
        /// config 支持的配置参数
        ///     - STRUN 服务器地址
        ///         iceServers: [{
        ///             urls:'url'
        ///             username:''
        ///             credential:''
        ///         }]
        ///     - bundlePolicy:'balanced'
        ///     - rtcpMuxPolicy:'require' | 'negotiate'
        ///     - iceTransportPolicy:'all'
        ///     - zmfAudioPump: false //全局设置,使用JSEP内置的音频流泵,可与外部音频流对接
        ///
        /// constraints 支持的限制
        ///     - googIPv6: true
        ///     - googDscp: true
        ///     - DtlsSrtpKeyAgreement: true
        ///     - RtpDataChannels: false
        ///     - googSuspendBelowMinBitrate: true
        ///     - googNumUnsignalledRecvStreams: 20
        ///     - googScreencastMinBitrate:int
        ///     - googHighStartBitrate:int
        ///     - googHighBitrate:true
        ///     - googVeryHighBitrate:true
        ///     - googCombinedAudioVideoBwe:true
        ///</remarks>
        public RTCPeerConnection(string config, bool zmfAudioPump, bool isCaller, RTCSessionObserver observer)
        {
            if (observer == null)
                throw new ArgumentNullException();

            int index = 1;
            while (index < OBSERVERS.Length && OBSERVERS[index] != null)
                ++index;
            if (index == OBSERVERS.Length)
                throw new OverflowException();

            OBSERVERS[index] = observer;
            iface = JSEP._CreatePeerConnection(config, zmfAudioPump, isCaller, (IntPtr)index, RTCSessionCallback);
            if (iface == IntPtr.Zero) {
                OBSERVERS[index] = null;
                throw new ArgumentException();
            }
        }

        ///<summary> 销毁所有资源
        ///</summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        ///<summary> 销毁所有资源
        ///</summary>
        ///<param name="isDisposing"> 是否是主动丢弃</param>
        protected virtual void Dispose(bool isDisposing)
        {
            if (iface != IntPtr.Zero)
                OBSERVERS[(int)JSEP._ReleasePeerConnection(iface)] = null;
            iface = IntPtr.Zero;
        }

        ///<summary> 自动销毁
        ///</summary>
        ~RTCPeerConnection()
        {
            Dispose(false);
        }

        ///<summary> 发送自定义数据
        ///</summary>
        ///<param name="channelId"> 通道ID</param>
        ///<param name="message"> 消息 </param>
        ///<returns> 成功返回0 </returns>
        public int SendMessage(string channelId, string message)
        {
            byte[] buffer = Encoding.UTF8.GetBytes(message);
            return JSEP._SendMessage(iface, channelId, buffer, buffer.Length);
        }

        ///<summary> 添加对端的ICE备选地址
        ///</summary>
        ///<param name="candidate"> 备选地址,通常对端由 JsepEventIceCandidate 事件的JsepIceCandidate字段获得</param>
        ///<returns> 成功返回0 </returns>
        public int AddIceCandidate(string candidate)
        {
            return JSEP._AddIceCandidate(iface, candidate);
        }

        ///<summary> 添加本地媒体流
        ///</summary>
        ///<param name="streamId"> 媒体流ID</param>
        ///<param name="bAudio"> 是否含音频</param>
        ///<param name="bVideo"> 是否含视频</param>
        ///<param name="constraints"> 媒体流的限制参数,JSON格式</param>
        ///<returns> 成功返回0,成功返回0, bAudio, bVideo分别指示是否含音视频 </returns>
        ///<remarks>
        /// 本地媒体流通常在协商前添加,对端媒体流是自动添加的.
        /// streamId 要保证唯一性,通常用账户ID.
        ///
        /// constraints 目前支持
        /// - 视频
        ///   video: {
        ///     zmfCapture:Id0      //ZMF采集源ID
        ///     zmfRender:Id0       //ZMF第三方渲染源ID,转接为采集
        ///   }
        /// - 音频
        ///   audio: {
        ///     DTMF:false          //不使用JSEP_InsertDtmf函数,可节省些内存.
        ///   }
        ///</remarks>
        public int AddLocalStream (string streamId, ref bool bAudio, ref bool bVideo, string constraints)
        {
            return JSEP._AddLocalStream(iface, streamId, ref bAudio, ref bVideo, constraints);
        }

        ///<summary> 关闭数据通道
        ///</summary>
        ///<param name="channelId"> 通道ID</param>
        public void CloseDataChannel (string channelId)
        {
            JSEP._CloseDataChannel(iface, channelId);
        }

        ///<summary> 动态创建数据通道
        ///</summary>
        ///<param name="channelId"> 通道ID</param> 
        ///<param name="constraints"> 配置参数</param> 
        ///<returns> 成功返回0</returns>
        ///<remarks>
        /// 支持的配置参数
        /// - ordered:true,       //是否保证次序,默认true
        /// - maxPacketLifeTime:0,//超过限时,将不重发.默认-1,始终重发
        /// - maxRetransmits:0,   //超过次数,将不重发,默认-1,,始终重发
        /// - negotiated:false,   //是否由上层应用负责协商建立过程,
        ///                         即不触发DataChannelOpen事件.默认false由内部自动完成
        /// - protocol:'',        //自定义的上层应用协议名,默认空
        ///</remarks>
        public int CreateDataChannel (string channelId, string constraints)
        {
            return JSEP._CreateDataChannel(iface, channelId, constraints);
        }

        ///<summary> 创建 answer SDP
        ///</summary>
        ///<param name="constraints"> 可选的SDP的限制参数.JSON格式</param>
        ///<seealso cref="CreateOffer"/>
        ///<returns> 无效参数立即返回-1,反之异步过程,结果由JsepEventCreateDescription 事件返回</returns>
        public int CreateAnswer (string constraints)
        {
            return JSEP._CreateAnswer(iface, constraints);
        }

        ///<summary> 创建offer SDP
        ///</summary>
        ///<param name="constraints"> 可选的SDP的限制参数.JSON格式</param>
        ///<returns> 无效参数立即返回-1,反之异步过程,结果由JsepEventCreateDescription 事件返回</returns>
        ///<remarks>
        /// constraints 支持如下限制
        /// - OfferToReceiveAudio: true
        /// - OfferToReceiveVideo: true
        /// - VoiceActivityDetection: true
        /// - IceRestart: false
        /// - googUseRtpMUX: true
        ///</remarks>
        public int CreateOffer (string constraints)
        {
            return JSEP._CreateOffer(iface, constraints);
        }

        ///<summary> 获取统计
        ///</summary>
        ///<param name="statsType"> 统计类别,""或null表示获取所有统计</param>
        ///<param name="bDebug"> 是否更详细的调试级别</param>
        ///<returns> 成功返回0, 将触发 JsepEventStatsReport 事件</returns>
        public int GetStats (string statsType, bool bDebug)
        {
            return JSEP._GetStats(iface, statsType, bDebug);
        }

        ///<summary> 开始/停止记录通信事件
        ///</summary>
        ///<param name="filename"> 日志文件名,空或null则停止记录</param>
        ///<param name="max_size_mb"> 日志文件允许大小MB</param>
        ///<returns> 成功返回0</returns>
        public int LogRtcEvent (string filename, int max_size_mb)
        {
            return JSEP._LogRtcEvent(iface, Encoding.UTF8.GetBytes(filename), max_size_mb);
        }

        ///<summary> 手工设置发送码率
        ///</summary>
        ///<param name="current_bitrate_bps"> 当前码率,若不大于0,则不设置</param>
        ///<param name="max_bitrate_bps"> 允许的最大码率,若不大于0,则不设置</param>
        ///<param name="min_bitrate_bps"> 允许的最小码率,若不大于0,则不设置</param>
        ///<returns> 成功返回0</returns>
        public int SetBitrate (int current_bitrate_bps, int max_bitrate_bps, int min_bitrate_bps)
        {
            return JSEP._SetBitrate(iface, current_bitrate_bps, max_bitrate_bps, min_bitrate_bps);
        }

        ///<summary> 发送DTMF
        ///</summary>
        ///<param name="tones"> DMTF的音符,0-9,A-D或a-d,#,*. 忽略无法识别的字符</param>
        ///<param name="duration"> 每个音符的持续毫秒MS,不能超过6000或小于70</param>
        ///<param name="inter_tone_gap"> 音符间隔,必须至少为50ms,但应尽可能短</param>
        ///<returns> 成功返回0,将触发 JsepEventToneChange 事件 </returns>
        ///<remarks>
        /// 队列化发送DTMF 任务.
        /// ','表示延迟2秒处理下一个字符
        /// 若调用时,上次仍在运行,则之前的任务将被取消
        ///</remarks>
        public int InsertDtmf (string tones, int duration, int inter_tone_gap)
        {
            return JSEP._InsertDtmf(iface, tones, duration, inter_tone_gap);
        }

        ///<summary> 将媒体流发布到ZMF中
        ///</summary>
        ///<param name="streamId"> 对端媒体流ID</param>
        ///<param name="renderOrCapturerBits"> 按位将视频轨道作为0-ZMF渲染,1-ZMF镜头</param>
        ///<param name="videoTrackMask"> 上个参数renderOrCapturerBits中的有效位掩码</param>
        ///<returns> 成功返回0 </returns>
        public int PublishRemoteStream(string streamId, int renderOrCapturerBits, int videoTrackMask)
        {
            return JSEP._PublishRemoteStream(iface, streamId, renderOrCapturerBits, videoTrackMask);
        }

        ///<summary> 移除本地媒体流
        ///</summary>
        ///<param name="streamId"> 媒体流ID,若""或null则移除所有本地流</param>
        public void RemoveLocalStream (string streamId)
        {
            JSEP._RemoveLocalStream(iface, streamId);
        }

        ///<summary> 配置本地(接收)的SDP
        ///</summary>
        ///<param name="desc"> desc SDP的JSON串,通常对端由 JsepEventCreateDescription 事件的JsepSdp获得</param>
        ///<returns> 无效参数立即返回-1,反之异步过程,结果由JsepEventSetDescription 事件返回 </returns>
        public int SetLocalDescription (string desc)
        {
            return JSEP._SetLocalDescription(iface, desc);
        }

        ///<summary> 配置对端(发送)的SDP
        ///</summary>
        ///<param name="desc"> SDP的JSON串,通常对端由 JsepEventCreateDescription 事件的JsepSdp获得</param>
        ///<returns> 无效参数立即返回-1,反之异步过程,结果由JsepEventSetDescription 事件返回 </returns>
        public int SetRemoteDescription (string desc)
        {
            return JSEP._SetRemoteDescription(iface, desc);
        }
    }

    ///<summary> 开始/停止记录音频处理过程
    ///</summary>
    ///<param name="filename"> 日志文件名,空或null则停止记录</param>
    ///<param name="max_size_mb"> 日志文件允许大小MB</param>
    ///<returns> 成功返回0</returns>
    public static int DumpAudioProcessing(string filename, int max_size_mb)
    {
        return _DumpAudioProcessing(Encoding.UTF8.GetBytes(filename), max_size_mb);
    }

    ///<summary>
    /// 内部全局音频流泵ID,虚拟的输入/输出设备.
    /// 外部与之对接时,须使用该ID
    ///</summary>
    public const string JSEP_AUDIO_PUMP = " jsepAudioPump";

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#region RTCSessionEvent事件携带JSON参数的字段名
    ///<summary> 事件类型, 数据类型:enum JespEvent </summary>
    public const string  JespEvent  = "JespEvent";

    ///<summary> SDP类型, 数据类型:UTF8字符串, 可选值
    /// - "offer" 发起类型
    /// - "answer" 应答类型
    ///</summary>
    public const string  JsepSdpType = "JsepSdpType";

    ///<summary> SDP位置, 数据类型:UTF8字符串, 可选值
    /// - "local" 本地接收侧
    /// - "remote" 对应发送侧
    ///</summary>
    public const string  JsepSdpLocation  = "JsepSdpLocation";

    ///<summary> SDP对象,数据类型: JSON串 </summary>
    public const string  JsepSdp = "JsepSdp";

    ///<summary> 原因, 数据类型: UTF8字符串 </summary>
    public const string  JsepReason = "JsepReason";

    ///<summary> ICE地址, 数据类型: JSON串 </summary>
    public const string  JsepIceCandidate = "JsepIceCandidate";

    ///<summary> ICE连接状态, 数据类型: UTF8字符串, 可选值
    /// - "new" 初始状态,
    /// - "checking" 探测进行状态
    /// - "connected" 成功连接
    /// - "completed" 探测结束
    /// - "disconnected" 断开连接
    /// - "failed" 连接过程已失败,不可恢复
    /// - "closed", 关闭连接
    /// 只有在connected和completed状态下,才可进行通信功能.
    ///</summary>
    public const string  JsepIceConnectionState = "JsepIceConnectionState";

    ///<summary> 协商状态, 数据类型: UTF8字符串, 可选值
    /// - "stable", 没有进行协商的稳定状态.
    /// - "have-local-offer", 已调SetLocalDescription(offer),后须接收并调用SetRemoteDescription(answer)
    /// - "have-remote-offer",已调SetRemoteDescription(offer),后须创建并应答SetLocalDescription(answer)
    /// - "have-remote-pranswer",已调SetRemoteDescription(answer),没有SetLocalDescription(offer)状态
    /// - "have-local-pranswer",已调SetLocalDescription(answer),没有SetRemoteDescription(offer)状态
    /// - "closed",连接已关闭的状态.
    ///</summary>
    public const string JsepSignalingState  = "JsepSignalingState";

    ///<summary> 媒体流ID, 数据类型: UTF8字符串 </summary>
    public const string  JsepStreamId = "JsepStreamId";

    ///<summary> 媒体流中音频轨道个数, 数据类型: int </summary>
    public const string  JsepAudioTrackCount = "JsepAudioTrackCount";

    ///<summary> 媒体流中视频轨道个数, 数据类型: int </summary>
    public const string  JsepVideoTrackCount = "JsepVideoTrackCount";

    ///<summary> 数据通道ID, 数据类型: UTF8字符串 </summary>
    public const string  JsepChannelId = "JsepChannelId";

    ///<summary> 数据通道属性, 数据类型: JSON串 </summary>
    public const string  JsepChannelConfig = "JsepChannelConfig";

    ///<summary> 消息数据, 数据类型: UTF8字符串 </summary>
    public const string  JsepMessage = "JsepMessage";

    ///<summary> DTMF 已发送音符, 数据类型: UTF8字符串
    /// 若为""或nullptr则表示已发送完毕
    ///</summary>
    public const string  JsepTone = "JsepTone";

    ///<summary> 统计类型, 数据类型: UTF8字符串, 可选值
    /// - googLibjingleSession  全局会话
    /// - transport             传输层
    /// - VideoBwe              视频带宽估计
    /// - remoteSsrc            对端RTP流
    /// - ssrc                  RTP流
    /// - googTrack             媒体流
    /// - localcandidate        本地ICE
    /// - remotecandidate       对端ICE
    /// - googComponent
    /// - googCandidatePair
    /// - googCertificate
    /// - datachannel           数据通道
    ///</summary>
    public const string  JsepStatsType = "JsepStatsType";

    ///<summary> 统计ID, 数据类型: UTF8字符串 </summary>
    public const string  JsepStatsId = "JsepStatsId";

    ///<summary> 统计时间截, 数据类型: double </summary>
    public const string  JsepStatsTimestamp = "JsepStatsTimestamp";

    ///<summary> 统计值, 数据类型: JSON串 </summary>
    public const string  JsepStats = "JsepStats";
#endregion
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#region API的函数指针
    private static readonly PFN_AddIceCandidate _AddIceCandidate;
    private static readonly PFN_AddLocalStream _AddLocalStream;
    private static readonly PFN_CloseDataChannel _CloseDataChannel;
    private static readonly PFN_CreateDataChannel _CreateDataChannel;
    private static readonly PFN_CreateAnswer _CreateAnswer;
    private static readonly PFN_CreateOffer _CreateOffer;
    private static readonly PFN_GetStats _GetStats;
    private static readonly PFN_InsertDtmf _InsertDtmf;
    private static readonly PFN_PublishRemoteStream _PublishRemoteStream;
    private static readonly PFN_RemoveLocalStream _RemoveLocalStream;
    private static readonly PFN_SetLocalDescription _SetLocalDescription;
    private static readonly PFN_SetRemoteDescription _SetRemoteDescription;
    private static readonly PFN_CreatePeerConnection _CreatePeerConnection;
    private static readonly PFN_ReleasePeerConnection _ReleasePeerConnection;
    private static readonly PFN_SendMessage _SendMessage;
    private static readonly PFN_LogRtcEvent _LogRtcEvent;
    private static readonly PFN_SetBitrate _SetBitrate;
    private static readonly PFN_DumpAudioProcessing _DumpAudioProcessing;

    private static readonly PFN_CreateWebSocket _CreateWebSocket;
    private static readonly PFN_CreateWebSocketServer _CreateWebSocketServer;
    private static readonly PFN_CreateIceSocket _CreateIceSocket;
    private static readonly PFN_CloseSocket _CloseSocket;
    private static readonly PFN_SendSocket _SendSocket;
    private static readonly PFN_AddSocketIceCandidate _AddSocketIceCandidate;
    private static readonly PFN_SetSocketIceParameters _SetSocketIceParameters;
#endregion
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#region 接口的代理类型
    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_AddIceCandidate(IntPtr iface, string candidate);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_AddLocalStream (IntPtr iface, string streamId, ref bool bAudio, ref bool bVideo, string constraints);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate void PFN_CloseDataChannel (IntPtr iface, string channelId);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate IntPtr PFN_CreatePeerConnection(string config, bool zmfAudioPump, bool isCaller, IntPtr userdata, _RTCSessionEventHandler observer);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_CreateDataChannel (IntPtr iface, string channelId, string constraints);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_CreateAnswer (IntPtr iface, string constraints);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_CreateOffer (IntPtr iface, string constraints);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_GetStats (IntPtr iface, string statsType, bool bDebug);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_InsertDtmf (IntPtr iface, string tones, int duration, int inter_tone_gap);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_PublishRemoteStream(IntPtr iface, string streamId, int renderOrCapturerBits, int videoTrackMask);

    [UnmanagedFunctionPointer(JSEP_CALL)]
    private delegate IntPtr PFN_ReleasePeerConnection (IntPtr iface);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate void PFN_RemoveLocalStream (IntPtr iface, string streamId);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_SendMessage (IntPtr iface, string channelId, byte[] buffer, int length);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_SetLocalDescription (IntPtr iface, string desc);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_SetRemoteDescription (IntPtr iface, string desc);

    [UnmanagedFunctionPointer(JSEP_CALL)]
    private delegate int PFN_LogRtcEvent(IntPtr iface, byte[] filename, int max_size_mb);

    [UnmanagedFunctionPointer(JSEP_CALL)]
    private delegate  int PFN_SetBitrate(IntPtr iface, int current_bitrate_bps, int max_bitrate_bps, int min_bitrate_bps);

    [UnmanagedFunctionPointer(JSEP_CALL)]
    private delegate int PFN_DumpAudioProcessing(byte[] filename, int max_size_mb);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate IntPtr PFN_CreateWebSocket(string wsURL, string rtcWebSocketInit, IntPtr userdata, _RTCSocketEventHandler observer);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate IntPtr PFN_CreateWebSocketServer(string wsURL, string rtcWebSocketInit, IntPtr userdata, _RTCSocketEventHandler observer);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate IntPtr PFN_CreateIceSocket(string rtcConfiguration, string rtcIceParameters, bool isCaller, IntPtr userdata, _RTCSocketEventHandler observer);

    [UnmanagedFunctionPointer(JSEP_CALL)]
    private delegate IntPtr PFN_CloseSocket(IntPtr iface);

    [UnmanagedFunctionPointer(JSEP_CALL)]
    private delegate int PFN_SendSocket(IntPtr iface, byte[] buffer, int length, int sendFlags);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_AddSocketIceCandidate(IntPtr iface, string candidate);

    [UnmanagedFunctionPointer(JSEP_CALL,CharSet = CharSet.Ansi)]
    private delegate int PFN_SetSocketIceParameters(IntPtr iface, string rtcIceParameters);

 #endregion
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    private const CallingConvention JSEP_CALL = CallingConvention.Cdecl;

    [DllImport("jsep.dll", EntryPoint = "JsepAPI", CallingConvention = JSEP_CALL)]
    private static extern IntPtr _JsepAPI(int apiLevel);

    [UnmanagedFunctionPointer(JSEP_CALL)]
    private delegate void _RTCSessionEventHandler(IntPtr user, int envetId, [MarshalAs(UnmanagedType.LPArray,SizeParamIndex=3)] byte[] json, int len);
    [UnmanagedFunctionPointer(JSEP_CALL)]
    private delegate void _RTCSocketEventHandler(IntPtr user, IntPtr iface, [MarshalAs(UnmanagedType.LPArray,SizeParamIndex=3)] byte[] utf8, int len, int eventId);
    private static Delegate[] OBSERVERS = new Delegate[256];
    private static Dictionary<IntPtr, RTCSocket> SOCKETS = new Dictionary<IntPtr, RTCSocket>();
    private static readonly _RTCSessionEventHandler RTCSessionCallback = (user, envetId, json, len) =>{
        RTCSessionObserver observer = (RTCSessionObserver)OBSERVERS[(int)user];
        if (observer != null)
            observer((RTCSessionEvent)envetId, Encoding.UTF8.GetString(json));
    };
       
    private static T JsepAPI<T>(int index) where T : class {
        Delegate api = Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(JSEP_API, index * IntPtr.Size), typeof(T));
        if (api == null) throw new NotImplementedException();
        return api as T;
    }

    static JSEP()
    {
        try {
            JSEP_API = _JsepAPI(1);
            if (JSEP_API == IntPtr.Zero) return;
            int i=0;
            // media stream
            _AddLocalStream = JsepAPI<PFN_AddLocalStream>(i++);
            _RemoveLocalStream = JsepAPI<PFN_RemoveLocalStream>(i++);
            _PublishRemoteStream = JsepAPI<PFN_PublishRemoteStream>(i++);

            // data channel
            _CreateDataChannel = JsepAPI<PFN_CreateDataChannel>(i++);
            _CloseDataChannel = JsepAPI<PFN_CloseDataChannel>(i++);
            _SendMessage = JsepAPI<PFN_SendMessage>(i++);

            // peer
            _CreatePeerConnection = JsepAPI<PFN_CreatePeerConnection>(i++);
            _ReleasePeerConnection = JsepAPI<PFN_ReleasePeerConnection>(i++);

            // negotiation
            _CreateAnswer = JsepAPI<PFN_CreateAnswer>(i++);
            _CreateOffer = JsepAPI<PFN_CreateOffer>(i++);
            _AddIceCandidate = JsepAPI<PFN_AddIceCandidate>(i++); 
            _SetLocalDescription = JsepAPI<PFN_SetLocalDescription>(i++);
            _SetRemoteDescription = JsepAPI<PFN_SetRemoteDescription>(i++);


            // dynamic setting
            _InsertDtmf = JsepAPI<PFN_InsertDtmf>(i++);
            _SetBitrate = JsepAPI<PFN_SetBitrate>(i++);

            // debug
            _GetStats = JsepAPI<PFN_GetStats>(i++);
            _LogRtcEvent = JsepAPI<PFN_LogRtcEvent>(i++);
            _DumpAudioProcessing = JsepAPI<PFN_DumpAudioProcessing>(i++);

            // socket
            _CreateWebSocket = JsepAPI<PFN_CreateWebSocket>(i++);
            _CreateWebSocketServer = JsepAPI<PFN_CreateWebSocketServer>(i++);
            _CreateIceSocket = JsepAPI<PFN_CreateIceSocket>(i++);
            _CloseSocket = JsepAPI<PFN_CloseSocket>(i++);
            _SendSocket = JsepAPI<PFN_SendSocket>(i++);

            // ice socket
            _AddSocketIceCandidate = JsepAPI<PFN_AddSocketIceCandidate>(i++);
            _SetSocketIceParameters = JsepAPI<PFN_SetSocketIceParameters>(i++);

        }
        catch{
            JSEP_API = IntPtr.Zero;
        }
    }
}
