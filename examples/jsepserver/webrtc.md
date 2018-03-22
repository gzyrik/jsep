# 设计目标
封装WebRTC API并与JusTalk Cloud 通信, 简化应用层开发

### 支持的浏览器
- Google Chrome 59.0.3071.115 及以上
- FireFox 54.0.1 (32 位) 及以上

### 集成方式
在HTML中添加如下代码, 将引入唯一的全局函数 WebRTC  
细节请参考样例网页.

        <script src="webrtc.min.js"></script>

# 返回版本号

        var version = WebRTC();

# 核心连接功能
建立网页与JusTalk Cloud 连接

### 打开连接
        
        var jc = new WebRTC(config, trace);
   
        trace 为可选的跟踪打印函数
        config 支持如下格式:
            {
                    'url':"http://192.168.0.66:8881",
                    'confProps': {
                        "roomId":"10210311", 
                        "regionId":"10",
                        "mediaType":"1",
                        "appKey":"6c06d1b0d9015e47ec144097",
                        "accountName": "1111111",
                        "password": "1",
                        "mtcConfQualityGradeKey":1,
                        "mtcConfSecurityKey":0,
                        "mtcConfPasswordKey":"123123",
                        "mtcConfCompositeModeKey":2,
                        "mtcConfStateKey":3,
                        "mtcConfCapacityKey":4
                     }
                    'id':id,
                    'getsourceid':'https:/.....'
            }
        其中：
            url 为WS服务器或HTTP服务器地址
            id  可选的网页标识,默认随机生成
            getsourceid 可选的Chrome屏幕共享扩展网页
            confProps 为会议相关参数
                - 必要参数
                    roomId          会议名
                    accountName     账号
                    password        账号密码
                    appKey          APPKEY
                - 可选参数
                    regionId        区域Id, 默认"0"
                    mediaType       媒体类型, 0音频，默认1视频
                    mtcConfPasswordKey          可选的会议密码,默认'123456'
                    mtcConfQualityGradeKey      会议视频尺寸级别
                    mtcConfSecurityKey          是否加密传输
                    mtcConfCompositeModeKey     收到视频的混合模式
                    mtcConfStateKey             加入时的初始状态
                    mtcConfRoleKey              加入时的初始角色
                    mtcConfCapacityKey          会议允许最大人数

### 发送控制指令
        
        jc.send(text, peer); 
        peer    可选的对端Id,http连接时被忽略
        text    具体的指令协议,参考文档下方说明
 
### 关闭连接

        jc.close();
        jc = null;

# 多媒体通话功能        
进行音视频通信

### 呼叫
        
        jc.call(configuration, constraints_stream, peer); 
        configuration       可选的标准 RTCConfiguration object,可以为null,
                            默认由服务器下发(由onopen事件返回)
        constraints_stream  标准 MediaStreamConstraints object 或 标准 MediaStream 实例
                            参考update()参数说明
        peer                可选的对端Id,http连接时被忽略    

### 通话中动态开关音频或视频
只控制音视频数据的是否发送,并不改变通话类型

        jc.mute(audio, video)
        audio   是否静音
        video   是否停止视频

### 动态切换成视频通话与音频通话
切换通话类型,将自动进行重新协商,因此不灵活,
请用上方的jc.mute()开关方式，或进下方切换自身状态来进行实时控制
    
        jc.update(constraints_stream) 
        constraints_stream    标准 MediaStreamConstraints object 或 标准 MediaStream 实例
                       按如下格式
                       {
                            "audio":true,
                            "video": {
                                "width": {"max": 640},
                                "height":{"max": 480}
                            }
                       }
                       max 可以限制视频最大尺寸, min 限制最小尺寸
 
### 通话中动态切换屏幕共享
切换屏幕共享,将自动进行重新协商

        jc.share(toggle, mediaSource)
        toggle          是否共享窗口或屏幕
        mediaSource     Firefox 特有的共享类型,可选值: "screen", "window", "application"

### 发送DTMF

        jc.dtmf(tones,duration,inter_tone_gap)
        tones DMTF的音符,0-9,A-D或a-d,#,*. 忽略无法识别的字符
        duration 可选的每个音符的持续毫秒,不能超过6000或小于70. 默认100ms 
        inter_tone_gap 可选的音符间隔,至少为50ms,但应尽可能短. 默认70ms

### 挂断
        
        jc.hangup(); 

# 事件回调
正常流程中,需要获知的反馈事件.

### 发生错误

        onerror(error) 
        error   错误对象
                应该关闭连接并重置界面

### 连接成功
        
        onopen(id, config) 
        id     本地Id
        config 配置信息
               .instanceId  为peer(对端Id)
               .iceServers  为媒体配置
               .confNum     会议号

### 连接关闭

        onclose()
        会话已关,应该关闭连接并重置界面

### 收到文本

        onmessage(text, peer) 
        text 消息, 注意以'\r\n'开头或{"cmd":cmd, "val":...}格式,
             为指令反馈,参考文档下方说明.
        peer 对端Id

### 被叫

        oncall(peer)
        peer 对端Id

        若正常发起呼叫,调用jc.call(...)

### 被挂

        onhangup()
        应该正常挂断呼叫, 调用jc.hangup()

### 媒体被移除
需要移除对应的DOM元素

        onremovestream(id, type)
        id      MediaStream的id
        type    参考文档下方说明

### 媒体已添加

        onaddstream(stream, type)    
        stream  为标准 MediaStream 实例,
                使用HTML中的video/audio标签呈现
        type 可选为
            - 'localvideo', 本地视频
            - 'localshare', 本地共享
            - 'peervideo',  对端视频
            - 'peeraudio',  对端音频
            - 'peershare',  对端共享
            - 'peerdata',   对端非媒体数据

### 协商状态改变
只有协商状态为‘stable’，才允许更新本地媒体(调用update/share)

        onsignalingstatechange(state)
        state 可选为
            - 'stable'  没有进行协商的稳定状态
            - 'have-local-offer'  协商进行中
            - 'have-remote-offer' 协商进行中
            - 'have-remote-pranswer' 协商进行中
            - 'have-local-pranswer'  协商进行中

### 连接状态改变
只要连接状态为‘failed’，则将不可恢复的通信中断

        oniceconnectionstatechange（state)
        state 可选为
            - 'new' 初始状态,
            - 'checking' 探测进行状态
            - 'connected' 成功连接
            - 'completed' 探测结束
            - 'disconnected' 断开连接
            - 'failed' 连接过程已失败,不可恢复
            - 'closed' 关闭连接

# 会议控制协议
网页是通过 ConfDelivery(即下方所谓的"自身") 加入的JusMeeting 会议系统中的.
其他业务上的控制，只能通过特定的控制协议，远程操作该代理完成。


有两种格式协议格式,
一种是@开头的明文格式,适合手工输入的,但无法处理特殊字符的情况,
另一种是BASE64编码的JSON串,适合由代码构造,可以处理特殊字符.

## @开头的明文格式

        '@'+peer+cmd+...
        //cmd 为指令,大小写不敏感, ...为相应参数

## BASE64编码的JSON格式

        {"cmd":cmd, ...}
        //cmd 为指令, ...为相应参数

## 指令反馈
执行后将自动收到onmessage的指令反馈, 如下格式

* 可能是相应的JSON结果

        {"cmd":cmd, "val":...}
        //cmd 为对应的指令,...为相应返回值

* 或者是以 "\r\n" 开头的大块反馈信息


### 常用控制指令
第一条为明文格式,第二条为JSON格式(需要BASE64编码)
* 罗列所有指令说明

        help  

* 设置自身或其他成员角色

        setrole <mask> <role> [target]
        {"cmd":"setrole", "mask":%d, "role":%d, "target":%s}

        //mask 掩码,表示操作的bit位
        //role 角色,按位解释. 设原状态为_ROLE_, 则
               _ROLE_ = (_ROLE_ & ~mask) | (mask & role);
        //target  可选的设置对象.缺省时为自身

* 设置自身或其他成员状态

        setstate <mask> <state> [target]
        {"cmd":"setstate", "mask":%d, "state":%d, "target":%s}

        //mask  掩码,表示操作的bit位
        //state 状态,按位解释. 设原状态为_STATE_, 则
               _STATE_ = (_STATE_ & ~mask) | (mask & state);
        //target  可选的设置对象.缺省时为自身

例如,
    - 关闭id0的音频的服务器转发

        setstate 0x2 0x0 id0

    - 关闭自身的音频发送
        
        setstate 0x8 0x0

    - 打开自身的音频发送

        setstate 0x8 0x8

* 设置自身或其他成员昵称

        setnick <str> [target]
        {"cmd":"setnick", "nick":"<str>", "target":%s}

        //str utf8编码的昵称
        //target 可选的设置对象.缺省时为自身

* 设置会场标题

        settitle <str>
        {"cmd":"settitle", "title":"<str>"}

        //str utf8编码的标题

* 设置对其他成员的订阅信息

        vsubscribe <target> <value>
        {"cmd":"vsubscribe","target":"%s","state":%d}
        //target 订阅对象
        //state  0 - 表示不订阅, 1 - 订阅视频

* 发送会议聊天文本

        sendtext <str> [target]
        {"cmd":"sendtext", "text":"<str>", "target":%s}

        //str utf8编码的文本
        //target 可选的目标对象.缺省时将广播

* 设置是否接收视频

        publish <bool>
        {”cmd":"publish", "video":<bool>}
        
        //bool 在布尔值,可选为true,false, 0, 1

* 设置收到视频的布局

        setmergemode <mergeMode>
        {”cmd":"setmergemode", "mergeMode":%d}

        //mergeMode 整型,布局模式
            1 平铺模式,所有视频均分平铺
            2 讲台模式,共享为大图,其他视频为小图
            3 演讲模式,共享为大图,共享者视频为小图,其他不显示
            4 自定义模式,由setlayout指令设置所有视频布局
            5 智能模式, 可用setmergemode_i,setmerge_i 调整相应的参数


* 实现自定义模式
确保收到视频的布局为自定义模式(@setmergemode 4), 否则要调用完之后要等, 设置为自定义模式,才能看到效果

            {"cmd":"setlayout","layout":[[<id0>,<ps> x, y, w, h],["id1",0x200, 0.1, 0.1, 1.0, 1.0], ...]

            //id0 成员URI或屏幕URI
            //ps  视频尺寸级别 0x100, 0x200, 0x300, 0x400
            //x,y,w,h 单位[0.0,1.0]的坐标和尺寸

* 调整智能模式的布局
确保收到视频的布局为智能模式(@setmergemode 5), 否则要调用完之后要等 设置为智能模式, 才能看到效果


        setmergemode_i <mode> <scsMode> [uri]

        //mode:智能分屏模式
                1   freeLayout
                2   rectLayout
                3   bigSmallX2
                4   bigSmallX3
                ....,
                11  bigSmallX10
                12  bigSmallTop
                13  bigSmallBottom
        //scsMode：智能分屏带屏幕共享模式
                1   screen
                2   platform
                3   platformCover
                4   speaker
        //uri: 大小屏默认放大的用户uri

例如, 设置智能模式为 自由模式， 带屏幕共享为 主持人模式， 大小屏放大用户默认为qqqq

        @setmergemode_i 1 3 [username:qqqq@sample.cloud.justalk.com]

* 调整智能模式的详细参数

        setmerge_i <width> <height> <mode> <scsMode> <fps> [uri] 
	    
        //width:宽
        //height：高
        //mode:智能分屏模式 参见上方setmergemode_i
        //scsMode：智能分屏带屏幕共享模式,参见上方setmergemode_iv
        fps:帧数 1-30
        uri: 大小屏默认放大的用户uri

例如, 设置宽为1600， 高为900， 智能模式为 自由模式， 带屏幕共享为 主持人模式， 帧数为24帧，大小屏放大用户默认为qqqq

        @setmerge_i 1600 900 1 3 24 [username:qqqq@sample.cloud.justalk.com]

# 订阅会议事件
发送订阅事件指令

        subscribe <hex>
        {"cmd":"subscribe", "mask":<hex>}

        //hex 事件位集合
            0x1 自身状态改变,    对应 onselfstate
            0x2 其他成员状态改变,对应 onactorstate/onactorleave
            0x4 会场属性改变,    对应 onconfstate
            0x8 聊天信息,        对应 onconfchat

将自动收到onmessage的指令反馈,同上格式

* 其他成员状态改变

        {"cmd":"onactorstate",
         "val":{"id0":{"nick":%s,"role":%d, "state":%d, "subscribe":%d},...}}
        //id0 是成员URI,全局唯一, nick 是UTF8编码的昵称,支持中文
        //role  是角色位整型:
                0x2 - 发送者
        //state 是状态位整型:
                0x1 - 转发视频
                0x2 - 转发音频
                0x4 - 打开视频
                0x8 - 打开音频
        //subscribe 是订阅信息
                1   - 订阅视频

* 成员离开

        {"cmd":"onactorleave", "val":["id0","id1",...]}

* 会场属性改变

        {"cmd":"onconfstate", "val":{"screen":%s, "title":%s, "sharer":%s}}
        //screen 是屏幕URI
        //title 是UTF8编码的标题,支持中文
        //sharer 是屏幕共享者URI

* 自身状态改变

        {"cmd":"onselfstate", "val":{"nick":%s,"role":%d, "state":%d}}

* 会场中聊天信息
 
        {"cmd":"onconfchat", "val":{"from":%s, "text":%s}}
        //from 是发言者URI
        //text 是UTF8编码的文本,支持中文

# 屏幕共享
- Google Chrome 必须安装 [额外扩展][ScreenCapturing]
- getsourceid 参数必须指定有效的https网址,缺省时需要公网访问能力


# 异常处理
onerror(error)处理中,必须关闭连接并重置界面
其中
    * error.message 是错误原因描述
    * error.number  可选的错误值

常见错误
* {"ret":999,"msg":"server internal error","detail":"full"}
  服务资源耗尽,ConfDelivery无法分配.

# 常见问题
* 开关摄像头的命令是啥
  没有该命令,只能通过_js.update(...),在通话中动态开关本地媒体实现,将引起重协商流程

* 音频发送状态不正确
  状态是按位运行,不要理解错误了.

        音频发送的指令是
            setstate 0x8 0x8
            //不要写成setstate 0x8 0x1
        音频关闭的指令是
            setstate 0x8 0x0

* 录制文件的命名规则和路径问题的文档描述

        录制文件的命名规则由会议号和可读日期时间组成，比如10316548_2017-10-30_14-15-21.mp4
        表示会议号是10316548,，2017年10月30日14点15分21秒开始录制。
        如果视频超过设置的SplitFileSize大小，就会分割成 会议号_日期时间 数字.文件后缀。

        存储路径在部署的时候设置好，代码里面写死，建议不要修改， 建议设个软连接到此目录上，
        如果修改的话，要同步修改自动清理脚本文件。

* Chrome的屏幕共享插件

        打开 https://www.justalkcloud.com/webrtc/
        点击左上角 “屏幕共享插件下载” 进行下载 屏幕共享插件

        解压下载的 屏幕共享插件

        打开 Chrome 输入 chrome://extensions/

        勾上 开发者模式， 点击加载已解压的扩展程序， 选择 刚刚解压的屏幕共享插件

----
[ScreenCapturing]: https://chrome.google.com/webstore/detail/screen-capturing/ajhifddimkapgcifgcodmmfdlknahffk "Chrome屏幕采集扩展"
