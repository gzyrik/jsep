#ifndef __WEBRTC_H__
#define __WEBRTC_H__
#pragma once
#include <string>
/**
 *  使用JSEP库与网页WebRTC互通
 * 
 *  实现平台相关的窗口类,特别是UI线程回调机制.
 *      class MyWindow : public WebRTC::View {
 *          ......
 *          void _QueueUIThreadCallback(int msg_id, char* data){......}
 *      };
 *  基本流程:
 *      - 连接
 *        MyWindow& view = ...;
 *        WebRTC* wc = WebRTC::Connect(view, addr, id);
 *      - 等待连接打开事件
 *        WebRTC::View::OnOpen() 
 *      - 发起通话
 *        wc->Call(......);
 *      - 挂断通话
 *        wc->Hangup();
 *      - 断开连接
 *        wc->Close();
 *        wc = 0;
 */
class WebRTC {
public:
    class View {
    public:
        //连接已建立
        virtual void OnOpen() = 0;
        //连接已关闭
        virtual void OnClose() = 0;
        //收到呼叫
        virtual void OnCall(const std::string& fromId, bool p2pOnly) = 0;
        //被挂断
        virtual void OnHangup() = 0;

        /**
         * 收到消息
         *
         * @param[in] message 消息文本
         * @param[in] fromId  发送者
         */
        virtual void OnMessage(const std::string& message, const std::string& fromId) = 0;

        /**
         * 添加媒体流
         * @param[in] streamId  流ID
         * @param[in] type 流类型
         *
         * @remarks
         *       type 可选为
         *       - 'previewaudio', 视频预览
         *       - 'previewshare', 共享预览
         *       - 'localmedia', 本地音视频
         *       - 'localshare', 本地共享
         *       - 'peervideo', 对端视频
         *       - 'peeraudio', 对端音频
         *       - 'peershare', 对端共享
         *       - 'peerdata',  对端非媒体数据
         */
        virtual void OnAddStream(const std::string& streamId, const std::string& type) = 0;

        //删除媒体流
        virtual void OnRemoveStream(const std::string& streamId, const std::string& type) = 0;

        /**
         * 统计报告
         * 
         * @param[in] statsType 统计类型
         * @param[in] statsId 统计ID
         * @param[in] stats 详细数据,JSON格式
         * @param[in] timestamp 时间点
         * 
         * @remarks
         * 统计类型, statsType 数据类型: UTF8字符串, 可选值
         *      - googLibjingleSession  全局会话
         *      - transport             传输层
         *      - VideoBwe              视频带宽估计
         *      - remoteSsrc            对端RTP流
         *      - ssrc                  RTP流
         *      - googTrack             媒体流
         *      - localcandidate        本地ICE
         *      - remotecandidate       对端ICE
         *      - googComponent
         *      - googCandidatePair
         *      - googCertificate
         *      - datachannel           数据通道
         */
        virtual void OnStatsReport(const std::string& statsTyep, const std::string& statsId,
            const std::string& stats, const double timestamp) = 0;

        //内部打印
        virtual void Trace(const char* format, ...) = 0;

        //将数据转到UI线程后,再调用WebRTC::_UIThreadCallback()
        virtual void _QueueUIThreadCallback(int msg_id, char* data) = 0;
    protected:
        virtual ~View() {}
    };

public:
    /**
     * 关闭连接
     */
    virtual void Close() = 0;

    /**
     * 发送消息
     */
    virtual void Send(const std::string& message, const std::string& toId) = 0;


    /**
     * 发起P2P 连接
     *
     * @param[in] configuration  标准 RTCConfiguration 参数
     * @param[in] toId 目标用户名
     *
     * @return 失败返回false
     */
    virtual bool P2P(const std::string& configuration, const std::string& toId) = 0;

    /**
     * 发起通话
     *
     * @param[in] configuration  标准 RTCConfiguration 参数
     * @param[in] constraints  标准 MediaStreamConstraints 参数
     * @param[in] toId 呼叫目标用户名
     *
     * @return 失败返回false
     */
    virtual bool Call(const std::string& configuration, const std::string& constraints, const std::string& toId) = 0;

    /**
     * 更新本地媒体
     * @param[in] constraints  标准 MediaStreamConstraints 参数
     *
     * @return 失败返回false
     */
    virtual bool Update(const std::string& constraints) = 0;

    /**
     * 开关共享屏幕
     * @param[in] constraints  标准 MediaStreamConstraints 参数
     *
     * @return 失败返回false
     */
    virtual bool Share(const std::string& constraints) = 0;

    /*
     * 开始/停止记录通信事件
     *
     * @param[in] filename 日志文件名,空则停止记录
     */
    virtual void LogRtcEvent(const std::string& filename) = 0;

    /*
     * 开始/停止记录音频处理过程
     *
     * @param[in] filename 日志文件名,空则停止记录
     */
    virtual void DumpAudioProcessing(const std::string& filename) = 0;

    /**
     * 挂断通话
     */
    virtual void Hangup() = 0;

    /**
     * 发送DTMF
     *
     * @param[in] tones DMTF的音符,0-9,A-D或a-d,#,*. 忽略无法识别的字符.
     * @param[in] duration 每个音符的持续毫秒MS,不能超过6000或小于70
     * @param[in] inter_tone_gap 音符间隔,至少为50ms,但应尽可能短
     *
     * @return 成功返回true
     *
     * @remarks
     *  队列化发送DTMF 任务.
     *  ','表示延迟2秒处理下一个字符
     *  若调用时,上次仍在运行,则之前的任务将被取消
     */
    virtual bool InsertDtmf(const std::string& tones, int duration, int inter_tone_gap) = 0;

    /**
     * GetStats接口的参数
     * 用于细分或过滤统计结果
     */
    enum {
        /** 详细的, 调试级别的统计 */
        Debug = 1,
        /** 包含音频相关的统计 */
        Audio = 2,
        /** 包含音频相应的统计 */
        Video = 4,
    };

    /**
     * 获取统计
     *
     * @param[in] statsType 统计类别,""表示获取所有统计
     * @param[in] bDebug  是否更详细的调试级别
     *
     * @return 失败返回false,反之等待OnStatsReport 事件.
     */
    virtual bool GetStats(const std::string& statsType, int statsFlags) = 0;

    /**
     * 由View 在UI线程中回调
     *
     * msg_id, data 对应 View::_QueueUIThreadCallback()中参数.
     */
    virtual void _UIThreadCallback(int msg_id, char* data) = 0;

public:
    /**
     * 发起P2P 连接, 参数简化版
     *
     * @param[in] stunURL  STUN/TURN 服务器地址
     * @param[in] password  登入服务器的所需密码
     * @param[in] toId 目标用户名
     *
     * @return 失败返回false
     */
    virtual bool P2P(const std::string& stunURL, const std::string& password, const std::string& toId) = 0;

    /**
     * 发起通话, 参数简化版
     *
     * @param[in] stunURL  STUN/TURN 服务器地址
     * @param[in] password  登入服务器的所需密码
     * @param[in] video  是否视频
     * @param[in] toId 呼叫目标用户名
     *
     * @return 失败返回false
     */
    virtual bool Call(const std::string& stunURL, const std::string& password, bool video, const std::string& toId) = 0;

    /**
     * 更新本地媒体, 参数简化版
     * @param[in] video  是否视频
     *
     * @return 失败返回false
     */
    virtual bool Update(bool video) = 0;

    /**
     * 开关共享屏幕,参数简化版
     */
    virtual bool Share(bool share) = 0;

    /**
     * 连到WebSocket服务器
     *
     * @param[in] view 显示的宿主窗口
     * @param[in] server 服务器地址,格式类似"ws://192.168.0.240:7000"
     * @param[in] myId  用户名
     *
     * @return 失败返回NULL, 反之等待View::OnOpen()/OnClose事件
     */
    static WebRTC* Connect(View& view, const std::string& server, const std::string& myId);

protected:
    virtual ~WebRTC() {}
};
#endif
