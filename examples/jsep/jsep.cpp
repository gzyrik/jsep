#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/api/test/fakeconstraints.h"
#include "webrtc/api/video/video_frame.h"
#include "webrtc/api/video/i420_buffer.h"
#include "webrtc/base/win32.h"
#include "webrtc/base/checks.h"
#include "webrtc/base/json.h"
#include "webrtc/base/base64.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/logsinks.h"
#include "webrtc/base/ssladapter.h"
#include "webrtc/base/win32socketinit.h"
#include "webrtc/base/win32socketserver.h"
#include "webrtc/base/socketadapters.h"
#include "webrtc/base/asyncinvoker.h"
#include "webrtc/media/engine/webrtcvideocapturerfactory.h"
#include "webrtc/modules/video_capture/video_capture_factory.h"
#include "webrtc/pc/webrtcsdp.h"
#include "webrtc/pc/iceserverparsing.h"
#include "webrtc/pc/peerconnectionfactory.h"
#include "webrtc/p2p/base/basicpacketsocketfactory.h"
#include "webrtc/p2p/base/icetransportinternal.h"
#include "webrtc/p2p/base/p2ptransportchannel.h"
#include "webrtc/p2p/base/p2pconstants.h"
#include "webrtc/p2p/client/basicportallocator.h"
#include "libyuv/convert.h"
#include <unordered_map>
#include <memory>
#include <limits>
#include <functional>
#include <mutex>
#include "jsep.h"
#include "audio_device_external.h"
#if defined _WIN32 || defined __CYGWIN__
#include <wincrypt.h>
#pragma warning(disable:4722)
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#endif
#ifdef __APPLE__
#include "webrtc/sdk/objc/Framework/Classes/VideoToolbox/videocodecfactory.h"
#endif
thread_local static std::string _lastErrorDescription;
typedef int(*ZmfAudioOutputCallback)(void* pUser, const char* outputId, int iSampleRateHz, int iChannels,
    unsigned char *buf, int len);
typedef void(*ZmfAudioInputCallback)(void* pUser, const char* inputId, int iSampleRateHz, int iChannels,
    unsigned char *buf, int len, int *micLevel,
    int playDelayMS, int recDelayMS, int clockDrift);
extern int(*Zmf_AudioInputGetCount)(void);
extern int(*Zmf_AudioInputGetName)(int iIndex, char acId[512], char acName[512]);
extern int(*Zmf_AudioOutputGetCount)(void);
extern int(*Zmf_AudioOutputGetName)(int iIndex, char acId[512], char acName[512]);
extern void(*Zmf_AudioOutputRequestStart)(const char *outputId, int iSampleRateHz, int iChannels);
extern void(*Zmf_AudioOutputRequestStop)(const char *outputId);
extern void(*Zmf_AudioInputRequestStart)(const char *inputId, int iSampleRateHz, int iChannels, int bAEC, int bAGC);
extern void(*Zmf_AudioInputRequestStop)(const char *inputId);
extern int(*Zmf_AudioOutputAddCallback)(void *pUser, ZmfAudioOutputCallback pfnCb);
extern int(*Zmf_AudioOutputRemoveCallback)(void *pUser);
extern int(*Zmf_AudioInputAddCallback)(void *pUser, ZmfAudioInputCallback pfnCb);
extern int(*Zmf_AudioInputRemoveCallback)(void *pUser);
struct ZmfVideoCaptureEncoder;
typedef int  (*ZmfVideoRenderCallback)(void* pUser, const char* renderId, int sourceType, int iAngle,
    int iMirror, int* iWidth, int* iHeight, unsigned char *buf,
    unsigned long timeStamp);
typedef void (*ZmfVideoCaptureCallback)(void* pUser, const char* captureId, int iFace, 
    int iImgAngle, int iCaptureOrient, int* iWidth, int* iHeight,
    unsigned char *buf, ZmfVideoCaptureEncoder* encoder);
static void (*Zmf_VideoCaptureRequestStop)    (const char *captureId);
static int  (*Zmf_VideoRenderRemoveCallback)  (void *pUser);
static int  (*Zmf_VideoCaptureRemoveCallback) (void *pUser);
static int  (*Zmf_VideoRenderAddCallback)     (void *pUser, ZmfVideoRenderCallback pfnCb);
static int  (*Zmf_VideoCaptureAddCallback)    (void *pUser, ZmfVideoCaptureCallback pfnCb);
static void (*Zmf_VideoCaptureRequestStart)   (const char *captureId, int iWidth, int iHeight, int iFps);
static void (*Zmf_OnVideoCaptureDidStop)      (const char *captureId);
static void (*Zmf_OnAudioInputDidStop)        (const char *inputId);
static void (*Zmf_OnAudioOutputDidStop)       (const char *outputId);
static void (*Zmf_OnAudioInput)               (const char *inputId, int sampleRateHz, int iChannels, unsigned char *buf, int len, int *micLevel, int playDelayMS, int recDelayMS, int clockDrift);
static void (*Zmf_OnAudioOutput)      (const char *outputId, int sampleRateHz, int iChannels, unsigned char *buf, int len);
static void (*Zmf_OnVideoRender)      (const char *renderId, int sourceType, int iAngle, int iMirror, int *iWidth, int *iHeight, unsigned char *bufI420, unsigned long timeStamp);
static void (*Zmf_OnVideoCapture)     (const char *captureId, int iFace, int iImgAngle, int iCamAngle, int *iWidth, int *iHeight, unsigned char *bufI420, ZmfVideoCaptureEncoder* encoder);
static bool IsZmfAudioSupport() { return Zmf_AudioInputRemoveCallback != nullptr; }
static bool IsZmfVideoSupport() { return Zmf_OnVideoCapture != nullptr; }
static int LUA_REGISTRYINDEX = -10000;//Lua 5.1
typedef struct lua_State lua_State;
typedef int (*lua_CFunction) (lua_State *L);
typedef ptrdiff_t lua_Integer;
struct luaL_Reg {
    const char* name;
    lua_CFunction func;
};
#define LUA_REFNIL          (-1)
#define LUA_TFUNCTION       6
#define lua_isfunction(L,n) (lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_pcall(L,n,r,f)  lua_pcallk(L, (n), (r), (f), nullptr, nullptr)
#define lua_pop(L,n)        lua_settop(L, -(n)-1)
static int (*lua_type) (lua_State *L, int idx);
static void (*lua_settop) (lua_State *L, int idx);
static int (*luaL_ref) (lua_State *L, int t);
static void (*luaL_unref) (lua_State *L, int t, int ref);
static void*(*lua_touserdata) (lua_State *L, int idx);
static const char* (*lua_tolstring) (lua_State *L, int idx, size_t *len);
static int (*lua_toboolean) (lua_State *L, int idx);
static lua_Integer (*lua_tointegerx) (lua_State *L, int idx, int *isnum);
static void (*lua_pushlstring) (lua_State *L, const char *s, size_t l);
static void (*lua_pushlightuserdata) (lua_State *L, void *p);
static void (*lua_pushinteger) (lua_State *L, lua_Integer n);
static void (*lua_createtable) (lua_State *L, int narr, int nrec);
static void (*lua_settable) (lua_State *L, int idx);
static void (*lua_rawgeti) (lua_State *L, int idx, int n);
static int  (*lua_pcallk) (lua_State *L, int nargs, int nresults, int errfunc, void* ctx, void* k);
static void (*lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);

#define FETCH_API2(func, name) do{\
    if (0 == (TRY_API(func, name))){ \
        LOG(LS_ERROR) <<"no api: "<<name;\
        goto clean;\
    }\
}while (0)
#define FETCH_API(func) FETCH_API2(func, #func)

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
static BOOL IsIdleMessage(MSG* pMsg) {
    static UINT nMsgLast;
    static POINT ptCursorLast;
    if (pMsg->message == WM_MOUSEMOVE || pMsg->message == WM_NCMOUSEMOVE) {
        if (ptCursorLast.x == pMsg->pt.x && ptCursorLast.y == pMsg->pt.y && pMsg->message == nMsgLast)
            return FALSE;

        ptCursorLast = pMsg->pt;  // remember for next time
        nMsgLast = pMsg->message;
		return TRUE;
    }

    // WM_PAINT and WM_SYSTIMER (caret blink)
    return pMsg->message != WM_PAINT && pMsg->message != 0x0118;
}
#define TRY_API(func, name) *(void**)&func = (void*)GetProcAddress(me32.hModule, name)
#else
#include <dlfcn.h>
#define TRY_API(func, name) *(void**)&func = (void*)dlsym(RTLD_DEFAULT, name)
#define CloseHandle(handle)
#endif
static bool LocateZmfVideo()
{
    if (IsZmfVideoSupport()) return true;
#ifdef _WIN32
    MODULEENTRY32 me32;
    me32.dwSize = sizeof (me32);
    HANDLE handle = CreateToolhelp32Snapshot (TH32CS_SNAPMODULE, 0);
    if (handle == (HANDLE)-1 || !Module32First (handle, &me32)){
        LOG(LS_ERROR) <<"enum first module";
        return false;
    }
    do {
        if (GetProcAddress (me32.hModule, "Zmf_OnVideoCapture")) {
#endif
            FETCH_API(Zmf_VideoCaptureRequestStop);
            FETCH_API(Zmf_VideoRenderRemoveCallback);
            FETCH_API(Zmf_VideoCaptureRemoveCallback);
            FETCH_API(Zmf_VideoRenderAddCallback);
            FETCH_API(Zmf_VideoCaptureAddCallback);
            FETCH_API(Zmf_VideoCaptureRequestStart);
            FETCH_API(Zmf_OnVideoCaptureDidStop);
            FETCH_API(Zmf_OnVideoRender);
            FETCH_API(Zmf_OnVideoCapture);
            CloseHandle (handle);
            return true;
#ifdef _WIN32
        }
    } while (Module32Next (handle, &me32));
#endif
clean:
    CloseHandle (handle);
    return false;
}
static bool LocateZmfAudio()
{
    if (IsZmfAudioSupport()) return true;
#ifdef _WIN32
    MODULEENTRY32 me32;
    me32.dwSize = sizeof (me32);
    HANDLE handle = CreateToolhelp32Snapshot (TH32CS_SNAPMODULE, 0);
    if (handle == (HANDLE)-1 || !Module32First (handle, &me32)){
        LOG(LS_ERROR) <<"enum first module";
        return false;
    }
    do {
        if (GetProcAddress (me32.hModule, "Zmf_OnVideoCapture")) {
retry:
#endif
            FETCH_API(Zmf_OnAudioInput);
            FETCH_API(Zmf_OnAudioOutput);
            FETCH_API(Zmf_OnAudioInputDidStop);
            FETCH_API(Zmf_OnAudioOutputDidStop);
            FETCH_API(Zmf_AudioInputGetCount);
            FETCH_API(Zmf_AudioInputGetName);
            FETCH_API(Zmf_AudioOutputGetCount);
            FETCH_API(Zmf_AudioOutputGetName);
            FETCH_API(Zmf_AudioOutputRequestStart);
            FETCH_API(Zmf_AudioOutputRequestStop);
            FETCH_API(Zmf_AudioInputRequestStart);
            FETCH_API(Zmf_AudioInputRequestStop);
            FETCH_API(Zmf_AudioOutputAddCallback);
            FETCH_API(Zmf_AudioOutputRemoveCallback);
            FETCH_API(Zmf_AudioInputAddCallback);
            FETCH_API(Zmf_AudioInputRemoveCallback);
            CloseHandle (handle);
            return true;
#ifdef _WIN32
        }
    } while (Module32Next (handle, &me32));
    {
        wchar_t buff[MAX_PATH];
        memset(buff, 0, sizeof(buff));
        ::GetModuleFileNameW((HINSTANCE)&__ImageBase, buff, MAX_PATH);
        wchar_t* p = wcsrchr(buff, '\\');
        if (!p) p = wcsrchr(buff, '/');
        *p = 0;
        wcscpy(p, L"\\zmf.dll");
        me32.hModule = ::LoadLibraryW(buff);
        if (me32.hModule) goto retry;
    }
#endif
clean:
    CloseHandle (handle);
    return false;
}
static bool LocateLua(lua_State *L)
{
    if (lua_pushcclosure) return true;
#ifdef _WIN32
    MODULEENTRY32 me32;
    me32.dwSize = sizeof (me32);
    HANDLE handle = CreateToolhelp32Snapshot (TH32CS_SNAPMODULE, 0);
    if (handle == (HANDLE)-1 || !Module32First (handle, &me32)){
        LOG(LS_ERROR) <<"enum first module";
        return false;
    }
    do {
        if (GetProcAddress (me32.hModule, "lua_pushcclosure")) {
#endif
            FETCH_API(lua_type);
            FETCH_API(lua_settop);
            FETCH_API(luaL_ref);
            FETCH_API(luaL_unref);
            FETCH_API(lua_touserdata);
            FETCH_API(lua_tolstring);
            FETCH_API(lua_toboolean);
            FETCH_API(lua_pushlstring);
            FETCH_API(lua_pushlightuserdata);
            FETCH_API(lua_pushinteger);
            FETCH_API(lua_createtable);
            FETCH_API(lua_settable);
            FETCH_API(lua_rawgeti);
            if (0 == (TRY_API(lua_tointegerx, "lua_tointegerx")))
                FETCH_API2(lua_tointegerx, "lua_tointeger");
            if (0 == (TRY_API(lua_pcallk, "lua_pcallk")))
                FETCH_API2(lua_pcallk, "lua_pcall");
            else
                LUA_REGISTRYINDEX = -1000000 - 1000;
            FETCH_API(lua_pushcclosure);
            CloseHandle (handle);
            return true;
#ifdef _WIN32
        }
    } while (Module32Next (handle, &me32));
#endif
clean:
    CloseHandle (handle);
    return false;
}
#undef CloseHandle
#undef FETCH_API2
#undef FETCH_API


#ifdef __ANDROID__
#define JSEP_PACKAGE_PATH "com.webrtc.jsep"
#include <jni.h>
namespace {
struct JContext
{
    enum {
        STRING,
        BOOLARRAY,
        INTARRAY,
        STRARRAY,
    };
    struct JValue{
        JValue(int t):type(t), value(0), objs(0){}
        const int type;
        void* value;
        jobject* objs;
    };
    typedef std::map<jobject, JValue> jvalmap;
    JNIEnv*& jenv;
    jvalmap jvals;
    JContext(JNIEnv*env):jenv(env){}
    ~JContext(){
        for (jvalmap::iterator iter = jvals.begin();
            iter != jvals.end(); ++iter) {
            JValue& val = iter->second;
            switch(iter->second.type) {
            case STRING:
                jenv->ReleaseStringUTFChars((jstring)iter->first, (const char*)val.value);
                break;
            case BOOLARRAY:
                jenv->ReleaseBooleanArrayElements((jbooleanArray)iter->first, (jboolean*)val.value, 0);
                break;
            case INTARRAY:
                jenv->ReleaseIntArrayElements((jintArray)iter->first, (jint*)val.value, 0);
                break;
            case STRARRAY:
                if (val.value && val.objs) {
                    const char** strs = (const char**)val.value;
                    for (int i=0;strs[i]; ++i) {
                        jenv->ReleaseStringUTFChars((jstring)val.objs[i], strs[i]);
                        jenv->DeleteLocalRef(val.objs[i]); 
                    }
                    free(val.value);
                    free(val.objs);
                }
                break;
            }
        }
    }
    const char* operator()(const jstring& str) {
        if (!str) return 0;
        jvalmap::iterator iter = jvals.find(str);
        if (iter != jvals.end())
            return (const char*)iter->second.value;
        JValue val(STRING);
        val.value= (void*)jenv->GetStringUTFChars(str, nullptr);
        jvals.insert(jvalmap::value_type((jobject)str, val));
        return (const char*)val.value;
    }
    jint* operator()(const jintArray& arr) {
        if (!arr) return 0;
        jvalmap::iterator iter = jvals.find(arr);
        if (iter != jvals.end())
            return (jint*)iter->second.value;
        JValue val(INTARRAY);
        val.value= (void*)jenv->GetIntArrayElements(arr, nullptr);
        jvals.insert(jvalmap::value_type((jobject)arr, val));
        return (jint*)val.value;
    }
    jboolean* operator()(const jbooleanArray& arr) {
        if (!arr) return 0;
        jvalmap::iterator iter = jvals.find(arr);
        if (iter != jvals.end())
            return (jboolean*)iter->second.value;
        JValue val(BOOLARRAY);
        val.value= (void*)jenv->GetBooleanArrayElements(arr, nullptr);
        jvals.insert(jvalmap::value_type((jobject)arr, val));
        return (jboolean*)val.value;
    }
    const char** operator()(const jobjectArray& arr) {
        if (!arr) return 0;
        jvalmap::iterator iter = jvals.find(arr);
        if (iter != jvals.end())
            return (const char**)iter->second.value;
        JValue val(STRARRAY);
        const int size = jenv->GetArrayLength(arr);
        if (size > 0) {
            const char ** strs = (const char**)calloc(size+1, sizeof(char*));
            jobject *strObj = (jobject*)calloc(size, sizeof(jobject));
            if (strs && strObj) {
                strs[size] = 0;
                for (int i=0;i<size;++i) {
                    strs[i] = 0;
                    strObj[i] = (jstring)jenv->GetObjectArrayElement(arr, i);
                    if (!strObj[i]) break;
                    strs[i] = (const char *)jenv->GetStringUTFChars((jstring)strObj[i], nullptr);
                    if (!strs[i]) break;
                }
                val.value = strs;
                val.objs = strObj;
            }
            else if(strs)
                free(strs);
            else if(strObj)
                free(strObj);
        }
        jvals.insert(jvalmap::value_type((jobject)arr, val));
        return (const char**)val.value;
    }
};
}
static JavaVM* _JavaVM;
static jmethodID _cbMethod;
static jfieldID _ptrField;
static void JSEP_CDECL_CALL jniRTCSessionCallback(RTCSessionObserver*userdata, enum RTCSessionEvent event, const char* json, int len)
{
    if (!userdata) return;
    JNIEnv *env = 0;
    bool attached = false;
    if (_JavaVM->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK) {
        jint res = _JavaVM->AttachCurrentThread(&env, 0);
        if (res < 0 || !env){
            LOG(LS_ERROR) << "attach JNIEnv failed";
            return;
        }
        attached = true;
    }
    if (env) {
        if (!_cbMethod) {
            jclass klass = env->GetObjectClass((jobject)userdata);
            if (!klass) {
                LOG(LS_ERROR) << "GetObjectClass failed";
                return;
            }
            if (!_cbMethod) {
                LOG(LS_ERROR) << "GetMethodID failed";
                return;
            }
            env->DeleteLocalRef(klass);
        }
        jstring strJson = env->NewStringUTF(json);
        env->CallVoidMethod((jobject)userdata, _cbMethod, (jint)event, strJson);
        env->DeleteLocalRef(strJson);
    }
    if (attached && _JavaVM->DetachCurrentThread() < 0)
        LOG(LS_WARNING) << "detach JVM failed";
}
static jint addIceCandidate(JNIEnv *env, jobject obj, jstring candidate) { JContext _(env);
    return JSEP_AddIceCandidate((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(candidate));
}
static jint addLocalStream(JNIEnv *env, jobject obj, jstring streamId, jintArray bAudioVideo, jstring constraints) { JContext _(env);
    jint* ptr = _(bAudioVideo);
    return JSEP_AddLocalStream((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(streamId), ptr, ptr+1, _(constraints));
}
static void closeChannel(JNIEnv *, jobject, jstring);
static jlong createPeerConnection(JNIEnv *env, jobject obj, jstring config, jstring constraints) {
    jlong ret=0;
    obj = env->NewGlobalRef(obj);
    if (obj){
        JContext _(env);
        ret = (jlong)JSEP_RTCPeerConnection(_(config), _(constraints), (RTCSessionObserver*)obj, jniRTCSessionCallback);
        if (!ret) env->DeleteGlobalRef(obj);
    }
    else 
        LOG(LS_ERROR) << "NewGlobalRef failed";
    return ret;
}
static jlong releasePeerConnection(JNIEnv*, jclass, jlong iface) {
    if (iface)
        env->DeleteGlobalRef((jobject)JSEP_Release((RTCPeerConnection*)iface));
    return 0;
}
static jint createChannel(JNIEnv*env, jobject obj, jstring channelId, jstring config){JContext _(env);
    return JSEP_CreateChannel((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(channelId), _(config));
}
static jint createAnswer(JNIEnv*env, jobject obj, jstring constraints){JContext _(env);
    return JSEP_CreateAnswer((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(constraints));
}
static jint createOffer(JNIEnv*env, jobject obj, jstring constraints){JContext _(env);
    return JSEP_CreateOffer((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(constraints));
}
static jint getStats(JNIEnv*env, jobject obj, jstring statsType, jboolean bDebug) {JContext _(env);
    return JSEP_GetStats((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(statsType), bDebug);
}
static jint insertDtmf(JNIEnv*env, jobject obj, jstring tones, jint duration, jint inter_tone_gap) {JContext _(env);
    return JSEP_InsertDtmf((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(tones), duration, inter_tone_gap);
}
static jint publishRemoteStream(JNIEnv*env, jobject obj, jstring streamId, jint renderOrCapturerBits, jint videoTrackMask){JContext _(env);
    return JSEP_PublishRemoteStream((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(streamId), renderOrCapturerBits, videoTrackMask);
}
static void removeLocalStream(JNIEnv*env, jobject obj, jstring streamId){JContext _(env);
    return JSEP_RemoveLocalStream((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(streamId));
}
static jint sendMessage(JNIEnv*env, jobject obj, jstring channelId, jstring buffer){JContext _(env);
    return JSEP_SendMessage((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(channelId), _(buffer));
}
static jint setLocalDescription(JNIEnv*env, jobject obj, jstring desc) {JContext _(env);
    return JSEP_SetLocalDescription((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(desc));
}
static jint setRemoteDescription(JNIEnv*env, jobject obj, jstring desc) {JContext _(env);
    return JSEP_SetRemoteDescription((RTCPeerConnection*)env->GetLongField(obj,_ptrField), _(desc));
}
static const JNINativeMethod _RTCPeerConnection_natives[] = {
    {"addIceCandidate", "(Ljava/lang/String;)I", (void*)addIceCandidate},
    {"addLocalStream", "(Ljava/lang/String;[ILjava/lang/String;)I", (void*)addLocalStream},
    {"closeChannel", "(Ljava/lang/String;)V", (void*)closeChannel},
    {"createPeerConnection", "(Ljava/lang/String;Ljava/lang/String;)J", (void*)createPeerConnection},
    {"releasePeerConnection", "(J)V", (void*)releasePeerConnection},
    {"createChannel", "(Ljava/lang/String;Ljava/lang/String;)I", (void*)createChannel},
    {"createAnswer", "(Ljava/lang/String;)I", (void*)createAnswer},
    {"createOffer", "(Ljava/lang/String;)I", (void*)createOffer},
    {"getStats", "(Ljava/lang/String;Z)I", (void*)getStats},
    {"insertDtmf", "(Ljava/lang/String;II)I", (void*)insertDtmf},
    {"publishRemoteStream", "(Ljava/lang/String;II)I", (void*)publishRemoteStream},
    {"removeLocalStream", "(Ljava/lang/String;)V", (void*)removeLocalStream},
    {"sendMessage", "(Ljava/lang/String;Ljava/lang/String;)I", (void*)sendMessage},
    {"setLocalDescription", "(Ljava/lang/String;)I", (void*)setLocalDescription},
    {"setRemoteDescription", "(Ljava/lang/String;)I", (void*)setRemoteDescription},
};
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/)
{
    const char* const kClassName = JSEP_PACKAGE_PATH"/RTCPeerConnection";
    JNIEnv* env = 0;
    if (vm->GetEnv(reinterpret_cast<void**> (&env),JNI_VERSION_1_4) < 0 || !env) {
        LOG(LS_ERROR) << "GetEnv failed";
        return -1;
    }
    jclass klass= env->FindClass(kClassName);
    if (!klass) return -2;
    if (env->RegisterNatives(klass, _RTCPeerConnection_natives,
            sizeof(_RTCPeerConnection_natives) /sizeof(_RTCPeerConnection_natives[0])) != JNI_OK)
        return -1;

    _JavaVM = vm;
    _cbMethod = env->GetMethodID(klass, "RTCSessionCallback", "(ILjava/lang/String;)V");
    _ptrField = env->GetFieldID(klass, "handle", "J");
    env->DeleteLocalRef(klass);
    if (!_cbMethod || !_ptrField) return -3;
    return JNI_VERSION_1_4;
}
#endif
namespace {
static const char kBuiltInAudioDevice[] = " __JSEP__";
static const char kCandidateSdpMidName[] = "sdpMid";
static const char kCandidateSdpMlineIndexName[] = "sdpMLineIndex";
static const char kCandidateSdpName[] = "candidate";
static const char kSessionDescriptionTypeName[] = "type";
static const char kSessionDescriptionSdpName[] = "sdp";
class SessionObserverProxy;
struct AudioPumpThread : public rtc::Thread
{
    virtual void Run() override {
        const int sampleRateHz = 16000;
        const int sleepTime = 30;
        const int maxSampleBufSize = sampleRateHz * sleepTime / 1000 * 2 * 10;
        unsigned char buf[maxSampleBufSize];
        unsigned int count;
        auto lastAudioSampleTime = rtc::TimeMillis();
        FILE* fInput=nullptr, *fOutput=nullptr;
        const char* dir = getenv("JSEP_AUDIO_PUMP_WRITE_TO_PATH");
        if (dir && dir[0]) {
            const std::string path(dir);
            if (path.back() == '/') {
                const std::string input(path + "input-16k.pcm"), output(path + "output-16k.pcm");
                fInput = fopen(input.c_str(), "wb+");
                fOutput = fopen(output.c_str(), "wb+");
            }
        }
        while (!IsQuitting()){
            const auto now = rtc::TimeMillis();
            const auto timeInterval = now - lastAudioSampleTime;
            count = sampleRateHz * timeInterval / 1000 * 2;
            if (count > maxSampleBufSize) count = maxSampleBufSize;
            if (count > 0) {
                Zmf_OnAudioOutput(kBuiltInAudioDevice, sampleRateHz, 1, buf, count);
                if (fInput) fwrite(buf, 1, count, fInput);
                Zmf_OnAudioInput(JSEP_AUDIO_PUMP, sampleRateHz, 1, buf, count, 0, 0, 0, 0);
                Zmf_OnAudioOutput(JSEP_AUDIO_PUMP, sampleRateHz, 1, buf, count);
                if (fOutput) fwrite(buf, 1, count, fOutput);
                Zmf_OnAudioInput(kBuiltInAudioDevice, sampleRateHz, 1, buf, count, 0, 0, 0, 0);
            }
            lastAudioSampleTime = now;
            rtc::Thread::SleepMs(sleepTime);
        }
        if (fInput) fclose(fInput);
        if (fOutput) fclose(fOutput);
        Zmf_OnAudioInput(JSEP_AUDIO_PUMP, 0, 0, 0, 0, 0, 0, 0, 0);
        Zmf_OnAudioInput(kBuiltInAudioDevice, 0, 0, 0, 0, 0, 0, 0, 0);
        Zmf_OnAudioOutputDidStop(JSEP_AUDIO_PUMP);
        Zmf_OnAudioInputDidStop(JSEP_AUDIO_PUMP);
        Zmf_OnAudioOutputDidStop(kBuiltInAudioDevice);
        Zmf_OnAudioInputDidStop(kBuiltInAudioDevice);
    }
};
struct ZmfVideoRender;
typedef std::vector<std::unique_ptr<ZmfVideoRender> > ZmfVideoRenderVector;
class MainThread : public 
#if defined _WIN32 || defined __CYGWIN__
                   rtc::Win32Thread
#else
                   rtc::Thread
#endif
{
    rtc::CriticalSection lock_;
    rtc::Thread netThread_;
    std::unique_ptr<rtc::NetworkManager> networkManager_;
    std::unique_ptr<rtc::PacketSocketFactory> socketFactory_;
    rtc::Thread workThread_;
    AudioPumpThread audioThread_;
    std::unordered_map<std::string, ZmfVideoRenderVector> videoSourceMap_;
    std::unordered_map<void*,std::shared_ptr<SessionObserverProxy> >  sessionObserverMap_;
    static bool SSL_VerificationCallback(unsigned char* x509_der, int bytes){
        return false; //return Instance().SSLVerifyCallback(x509_der, bytes);
    }
/*
#if defined _WIN32 || defined __CYGWIN__
    HCERTSTORE  certStore_;
#endif
    bool SSLVerifyCallback(unsigned char* x509_der, int bytes) {
#if defined _WIN32 || defined __CYGWIN__
        auto x509Cert = CertCreateCertificateContext(X509_ASN_ENCODING, x509_der, bytes);
        if (!x509Cert) {
            LOG(LS_ERROR) << "CertCreateCertificateContext(x509_der) failed";
            return false;
        }
        PCCERT_CONTEXT prevCA = nullptr;
        do {
            auto rootCA = CertFindCertificateInStore(certStore_, x509Cert->dwCertEncodingType, 0,
                CERT_FIND_ISSUER_NAME, (const void*)&(x509Cert->pCertInfo->Issuer), prevCA);\
            if (rootCA && CryptVerifyCertificateSignature((HCRYPTPROV_LEGACY)0,
                rootCA->dwCertEncodingType, x509_der, bytes, &(rootCA->pCertInfo->SubjectPublicKeyInfo))){
                CertFreeCertificateContext(x509Cert);
                CertFreeCertificateContext(rootCA);
                return true;
            }
            prevCA = rootCA;
        } while (prevCA);
        CertFreeCertificateContext(x509Cert);
#endif
        return false;
    }
*/
    MainThread()
#if defined _WIN32 || defined __CYGWIN__
        :rtc::Win32Thread(new rtc::Win32SocketServer)
#endif
        {
            SetName("Main", this);
            workThread_.SetName("Work", &workThread_);
            audioThread_.SetName("Audio", &audioThread_);
            netThread_.SetName("Net", &netThread_);
            if (!rtc::InitializeSSL(SSL_VerificationCallback))
                LOG(LS_ERROR) << "InitializeSSL failed";
#if defined _WIN32 || defined __CYGWIN__
            //certStore_ = CertOpenSystemStore((HCRYPTPROV_LEGACY)0, L"ROOT");
            rtc::ThreadManager::Instance()->SetCurrentThread(this);
#else
            Start();
#endif
            networkManager_.reset(new rtc::BasicNetworkManager());
            socketFactory_.reset(new rtc::BasicPacketSocketFactory(&netThread_));
            netThread_.Start();
        }
    ~MainThread(){
        Terminate(); 
        //fprintf(stderr, "** JSEP Exited.\n");
#if defined _WIN32 || defined __CYGWIN__
        _exit(0);
#endif
    }
    webrtc::PeerConnectionFactoryInterface *peerFactory_ = nullptr;
    webrtc::AudioDeviceModule* adm_ = nullptr;
    typedef std::function<void*(void)> ThreadFunctor;
    void* _RunAtWorkThread(const ThreadFunctor& fun){
        return fun();
    }
public:
    void RemoveVideoSource(webrtc::MediaStreamInterface* stream) {
        ZmfVideoRenderVector sources;
        const std::string& streamId = stream->label(); {
            rtc::CritScope _(&lock_);
            auto iter = videoSourceMap_.find(streamId);
            if (iter != videoSourceMap_.end()) {
                std::swap(sources, iter->second);
                videoSourceMap_.erase(iter);
            }
        }
    }
    void InsertVideoSource(const std::string& streamId, ZmfVideoRenderVector& sources) {
        rtc::CritScope _(&lock_);
        std::swap(sources, videoSourceMap_[streamId]);
    }
    std::shared_ptr<SessionObserverProxy> GetSessionObserver(void* peer);
    void AddSessionObserver(void*peer, const std::shared_ptr<SessionObserverProxy>& proxy) {
        rtc::CritScope _(&lock_);
        sessionObserverMap_[peer] = proxy;
    }
    std::shared_ptr<SessionObserverProxy> RemoveSessionObserver(void* peer){
        std::shared_ptr<SessionObserverProxy> proxy;
        rtc::CritScope _(&lock_);
        auto iter = sessionObserverMap_.find(peer);
        if (iter != sessionObserverMap_.end()) {
            proxy = iter->second;
            sessionObserverMap_.erase(iter);
        }
        return proxy;
    }
    virtual void Quit() override {
#ifdef _WIN32
        ::PostQuitMessage(0);
#endif
        rtc::Thread::Quit();
    }
    void MainLoop(const std::function<bool(int)>& onidle) {
        bool bIdle = true;
        int lIdleCount = 0;
        rtc::ThreadManager::Instance()->SetCurrentThread(this);
        if (!onidle) {
#ifdef _WIN32
            MSG msg;
            while(::GetMessage(&msg, nullptr,0,0)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }
#else
            rtc::Thread::Run();
#endif
        }
        for (;!IsQuitting();) {
#ifdef _WIN32
            MSG msg;
            while (bIdle && !::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
                if (!onidle(lIdleCount++)) bIdle = false;
            }
            do {
                if (!::GetMessage(&msg, nullptr, 0, 0)) break;
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (IsIdleMessage(&msg)) {
                    bIdle = true;
                    lIdleCount = 0;
                }
            } while (::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE));
#else
            rtc::Message msg;
            while (bIdle && !rtc::Thread::Peek(&msg)) {
                if (!onidle(lIdleCount++)) bIdle = false;
            }
            do {
                if (!rtc::Thread::ProcessMessages(1)) break;
            } while (rtc::Thread::Peek(&msg));
            bIdle = true;
            lIdleCount = 0;
#endif
        }
        Stop();
        Restart();//reset IsQuitting() if the thread is being restarted
    }
    void Terminate() {
        rtc::CritScope _(&lock_);
        Stop();
        videoSourceMap_.clear();
        sessionObserverMap_.clear();
        audioThread_.Stop();
        workThread_.Stop();
        netThread_.Stop();
        rtc::CleanupSSL();
#if defined _WIN32 || defined __CYGWIN__
        //CertCloseStore(certStore_, 0);
#endif
    }
    void* WorkThreadCall(const ThreadFunctor& func) {
        RTC_DCHECK(workThread_.RunningForTest());
        webrtc::MethodCall1<MainThread, void*, const ThreadFunctor&> call(this, &MainThread::_RunAtWorkThread, func);
        return call.Marshal(RTC_FROM_HERE, &workThread_);
    }
    rtc::Thread& NetThread() { return netThread_;}
    void* NetThreadCall(const ThreadFunctor& func) {
        RTC_DCHECK(netThread_.RunningForTest());
        webrtc::MethodCall1<MainThread, void*, const ThreadFunctor&> call(this, &MainThread::_RunAtWorkThread, func);
        return call.Marshal(RTC_FROM_HERE, &netThread_);
    }
    cricket::PortAllocator* CreatePortAllocator() {
        return new cricket::BasicPortAllocator(networkManager_.get(), socketFactory_.get());
    }
    webrtc::PeerConnectionFactoryInterface* AddRefFactory(bool zmfAudioPump) {
        if (!peerFactory_) {
            workThread_.Start();
            if (IsZmfAudioSupport()) {
                adm_ = (webrtc::AudioDeviceModule*)WorkThreadCall([zmfAudioPump]{
                    auto adm = webrtc::AudioDeviceModuleExternal::Create(0, zmfAudioPump ? kBuiltInAudioDevice : nullptr);
                    return  adm.release();
                });
                if (!adm_) {
                    LOG(LS_ERROR) << "AudioDeviceModule Create failed";
                    workThread_.Stop();
                    return nullptr;
                }
            }
            else
                zmfAudioPump = false;
            cricket::WebRtcVideoEncoderFactory* encoder_factory = nullptr;
            cricket::WebRtcVideoDecoderFactory* decoder_factory = nullptr;
#ifdef __APPLE__
            encoder_factory = new webrtc::VideoToolboxVideoEncoderFactory();
            decoder_factory = new webrtc::VideoToolboxVideoDecoderFactory();
#endif
            auto factory = webrtc::CreatePeerConnectionFactory(&netThread_, &workThread_, this, adm_, encoder_factory, decoder_factory);
            if (!factory){
                LOG(LS_ERROR) << "PeerConnectionFactory Create failed";
                ReleaseFactory();
                return nullptr;
            }
            if (zmfAudioPump) {
                const bool dummy = true;
                EnableBuiltInDSP (&dummy, &dummy, &dummy);
                audioThread_.Start();
            }
            else {
#if defined(OS_IOS) || defined(OS_ANDROID)
                const bool isBuiltIn = true;
#else
                const bool isBuiltIn = false;
#endif
                EnableBuiltInDSP (&isBuiltIn, &isBuiltIn, &isBuiltIn);
            }
            peerFactory_ = factory.release();
        }
        else 
            peerFactory_->AddRef();
        return peerFactory_;
    }
    webrtc::PeerConnectionFactoryInterface* GetFactory() const { return peerFactory_; }
    void ReleaseFactory(){
        rtc::CritScope _(&lock_);
        if (peerFactory_ && peerFactory_->Release() > 0) return;
        peerFactory_ = nullptr;
        audioThread_.Stop();
        if (adm_){
            auto adm = adm_;
            adm_ = (webrtc::AudioDeviceModule*)WorkThreadCall([adm]{
                while (adm->Release() > 0);
                return nullptr;
            });
        }
        workThread_.Stop();
    }
    void EnableBuiltInDSP(const bool* bAEC, const bool* bAGC, const bool* bNS) {
        if (!adm_) return;
        if (bAEC) adm_->EnableBuiltInAEC(*bAEC);
        if (bAGC) adm_->EnableBuiltInAGC(*bAGC);
        if (bNS) adm_->EnableBuiltInNS(*bNS);
    }
    static MainThread& Instance() { static MainThread _mainthread; return _mainthread; }
};
static std::string& TrimString(std::string& s) {
    if (s.empty()) return s;
    s.erase(0,s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}
static void SplitString(const std::string& str, std::set<std::string>& ret, char c) {
    std::istringstream f(str);
    std::string s(256, '\0');
    while (std::getline(f, s, c)) ret.insert(TrimString(s));
}
static bool ReadFileString(const std::string& fname, std::string* contents) {
    FILE* file = fopen(fname.c_str(), "rb");
    if (!file) {
        LOG(LS_ERROR) << fname << " open failed";
        return false;
    }
    if (contents) contents->clear();
    const size_t kBufferSize = 1 << 16;
    std::unique_ptr<char[]> buf(new char[kBufferSize]);
    size_t len;
    while ((len = fread(buf.get(), 1, kBufferSize, file)) > 0)
        if (contents) contents->append(buf.get(), len);
    fclose(file);
    return true;
}
class AsyncWebSocket : public rtc::BufferedReadAdapter {
public:
    enum {
        CONTINUATION    = 0x0,
        TEXT_FRAME      = 0x1,
        BINARY_FRAME    = 0x2,
        CLOSE           = 8,
        PING            = 9,
        PONG            = 0xa,
    };
    enum {
        CLOSING,
        CLOSED,
        CONNECTING,
        OPEN,
    };
    sigslot::signal2<const char*, size_t> SignalPacketEvent;
public:
    static bool VerifyURL(const std::string& wsURL, bool& ssl, std::string& host, int& port, std::string& path){
        if (wsURL.size() < 5)
            return false;
        else if (wsURL.compare(0, 5, "ws://") == 0)
            ssl = false;
        else if (wsURL.compare(0, 6, "wss://") == 0)
            ssl = true;
        else
            return false;
        const std::string& url = wsURL.substr(ssl ? 6: 5);
        auto n = url.rfind('/');
        if (n != url.npos)
            path = url.substr(n + 1);
        else
            path = "/";
        const std::string& addr = url.substr(0, n);
        n = addr.rfind(':');
        if (n != url.npos)
            port = atoi(url.substr(n + 1).c_str());
        else
            port = 80;
        host = url.substr(0, n);
        return host.size() > 0;
    }
    explicit AsyncWebSocket(const std::string& request, rtc::AsyncSocket* sock, rtc::SSLAdapter* ssl, const std::string& protocol) :
        rtc::BufferedReadAdapter(sock, 1024*1024), ssl_(ssl), 
        readyState_(CONNECTING), server_(request.empty()), wsRequest_(request)
    {
        if (server_ && protocol.size() > 0)
            SplitString(protocol, protocols_, ',');
        BufferInput(ssl == nullptr);
    }
    int readyState() const { return readyState_; }
    const std::string& protocol() const { return acceptProtocol_; }
    const std::string& path() const { return getPath_; }
    int ConnectServer(const std::string& hostname, const rtc::SocketAddress& addr) {
        RTC_CHECK(!server_);
        auto ret = rtc::BufferedReadAdapter::Connect(addr);
        if (ret == 0 && ssl_){
            ssl_->SetRole(rtc::SSL_CLIENT);
            ret = ssl_->StartSSL(hostname.c_str(), false);
        }
        return ret;
    }
    int AcceptClient(const std::string& hostname) {
        RTC_CHECK(server_);
        int ret = 0;
        if (ssl_){
            ssl_->SetRole(rtc::SSL_SERVER);
            ret = ssl_->StartSSL(hostname.c_str(), false);
        }
        return ret;
    }
    virtual int Close() override {
        RTC_CHECK(readyState_ != CLOSED && readyState_ != CLOSING);
        readyState_ = CLOSING;
        static const uint8_t bye[6] = { 0x88, 0x80, 0x00, 0x00, 0x00, 0x00 };
        return DirectSend(bye, 6);//wait for close msg
    }
    virtual int Send(const void* pv, size_t cb) override {
        return SendPacket(TEXT_FRAME, static_cast<const char*>(pv), cb, false);
    }
    int SendPacket(int type, const char* data, size_t size, bool useMask) {
        if (readyState_ != OPEN) return -1;
        std::vector<char> tmp(10+size);
        char* p = &tmp[0];
        static const char masking_key[4] = { 0x12, 0x34, 0x56, 0x78 };
        *p++ = 0x80 | type;
        if (size < 126)
            *p++ = (size & 0xff) | (useMask ? 0x80 : 0);
        else if (size < 65536) {
            *p++ = 126 | (useMask ? 0x80 : 0);
            *p++ = (size >> 8) & 0xff;
            *p++ = (size >> 0) & 0xff;
        }
        else {
            *p++ = 127 | (useMask ? 0x80 : 0);
            for (int k=56; k>=0; k -= 8)
                *p++ = (size >> k) & 0xff;
        }
        if (useMask) {
            *p++ = masking_key[0];
            *p++ = masking_key[1];
            *p++ = masking_key[2];
            *p++ = masking_key[3];
            for (size_t i=0;i<size;++i)
                p[i] = data[i] ^ masking_key[i & 0x3];
        }
        else
            memcpy(p, data, size);
        return DirectSend(&tmp[0], (p - &tmp[0]) + size);
    }
    void ForceClose(int err) {
        if (readyState_ != CLOSED) {
            LOG(LS_WARNING) << "Force Close";
            rtc::BufferedReadAdapter::Close();
            OnCloseEvent(this, err);
        }
    }
private:
    rtc::SSLAdapter* ssl_;
    std::string clientKey_, acceptProtocol_;
    std::vector<char> packet_;
    int readyState_;
    const bool server_;
    //only client used
    std::string wsRequest_, acceptKey_;
    //only sever used
    std::set<std::string> protocols_;
    std::string getPath_, clientProtocol_;

    static const std::string AcceptKey(const std::string& clientKey){
        char digest[20]={0};
        const std::string& input = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        rtc::ComputeDigest(rtc::DIGEST_SHA_1, input.data(), input.size(), digest, 20);
        std::string acceptKey;
        rtc::Base64::EncodeFromArray(digest, 20, &acceptKey);
        return acceptKey;
    }
    virtual void OnConnectEvent(rtc::AsyncSocket* socket) override {
        BufferInput(true);
        if (server_)  return;
        std::ostringstream oss;
        char bytes[16]={0};
        srand((unsigned)time(nullptr));
        for (int i=0;i<16;++i)
            bytes[i] = static_cast<char>(rand()*0xFF/RAND_MAX);
        rtc::Base64::EncodeFromArray(bytes, 16, &clientKey_);
        oss << wsRequest_ <<
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Version: 13\r\n";
        oss << "Sec-WebSocket-Key: "<<clientKey_ << "\r\n\r\n";
        std::string().swap(wsRequest_);
        const auto& head = oss.str();
        DirectSend(head.data(), head.size());
    }
    virtual void OnCloseEvent(rtc::AsyncSocket* socket, int err) override {
        readyState_ = CLOSED;
        rtc::BufferedReadAdapter::OnCloseEvent(socket, err);
    }
    const char* CheckHttpHead(const std::string& http, size_t& line) {
        size_t sp = 0;
        if (server_){
            if (http.size() < 5) return nullptr;
            sp = http.find(' ', 4);
            if (sp == http.npos || http.compare(0, 4, "GET ") != 0) return "HTTP GET mismatch";
            getPath_ = http.substr(4, sp-4);
            sp++;
        }
        if (http.size() - sp < 8) return nullptr;
        if (http.compare(sp, 8, "HTTP/1.1") != 0) return "HTTP version != HTTP/1.1";
        sp += 8;

        if (!server_){
            sp++;
            if (http.size() - sp < 3) return nullptr;
            if (http.compare(sp, 3, "101") != 0) return "HTTP status != 101";
            sp += 3;
        }
        sp = http.find('\r', sp);
        if (sp == http.npos || sp+1== http.size()) return nullptr;
        if (http[sp+1] != '\n') return "HTTP end line mismatch";
        line = sp;
        return nullptr;
    }
    const char* CheckDone(){
        if (server_) {
            if (clientKey_.empty()) return "No Sec-WebSocket-Key";

            if (clientProtocol_.size() > 0) {
                acceptProtocol_.clear();
                std::string s(256, '\0');
                std::istringstream f(clientProtocol_);
                while (std::getline(f,s, ',')) {
                    if (protocols_.find(TrimString(s)) != protocols_.end()) {
                        acceptProtocol_ = s;
                        break;
                    }
                }
            }

            std::ostringstream oss;
            oss <<
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " <<  AcceptKey(clientKey_) << "\r\n";
            if (!clientProtocol_.empty())
                oss << "Sec-WebSocket-Protocol: " << acceptProtocol_ << "\r\n";
            oss << "\r\n";
            const auto& response = oss.str();
            DirectSend(response.data(), response.size());
            std::string().swap(clientProtocol_);
            std::string().swap(clientKey_);
        }
        else {
            if (clientKey_.empty() || acceptKey_.empty()) return "No Sec-WebSocket-Key";
            std::string().swap(clientKey_);
            std::string().swap(acceptKey_);
        }
        readyState_ = OPEN;
        SetOption(rtc::Socket::OPT_NODELAY, 1);
        SignalConnectEvent(this);
        return nullptr;
    }
    bool CheckValue(const std::string& vals, const char* expect) {
        std::istringstream iss(vals);
        std::string val(256,'\0');
        while (std::getline(iss, val, ',')) {
            std::transform(val.begin(), val.end(), val.begin(), tolower);
            if (TrimString(val) == expect) return true;
        }
        return false;
    }
    const char* CheckKey(const std::string& data, size_t& line) { do {
        line += 2;
        auto pos = data.find("\r\n", line);
        if (pos == data.npos) return nullptr;
        if (pos == line) return CheckDone();

        auto colon = data.find(':', line);
        if (colon == data.npos || colon > pos)
            return "not colon";
        std::string key(256,'\0'), str(256, '\0');
        std::istringstream(data.substr(line, colon-line))>>key;
        auto vals = data.substr(colon+1, pos-colon-1);
        TrimString(vals);
        LOG(LS_INFO) << key << ':' << vals;

        if (key == "Upgrade"){
            if (!CheckValue(vals, "websocket")) return "Upgrade NOT include 'websocket'";
        }
        else if (key == "Connection"){
            if (!CheckValue(vals, "upgrade")) return "Connection NOT include 'upgrade'";
        }
        else if (server_) {
            if (key == "Sec-WebSocket-Protocol"){
                if (!clientProtocol_.empty()) clientProtocol_.push_back(',');
                clientProtocol_.append(vals);
            }
            else if (key == "Sec-WebSocket-Key"){
                std::istringstream(vals)>>str;
                if (str.size() != 24) return "Sec-WebSocket-Key must 24 bytes";
                clientKey_ = str;
            }
            else if (key == "Sec-WebSocket-Version") {
                std::istringstream(vals)>>str;
                if (str != "13") return "Sec-WebSocket-Version != 13";
            }
        }
        else {
            if (key == "Sec-WebSocket-Protocol")
                acceptProtocol_ = vals;
            else if (key == "Sec-WebSocket-Accept"){
                std::istringstream(vals)>>str;
                if (AcceptKey(clientKey_) != str) return "Sec-WebSocket-Accept mismatch";
                acceptKey_ = str;
            }
        }
        line = pos;
    } while (true); }

    virtual void ProcessInput(char* data, size_t* len) override {
        if (readyState_ == CLOSED || readyState_ == CLOSING){
            *len = 0;
            return;
        }
        if (readyState_ == CONNECTING) {
            std::string buf(data, *len);
            size_t line = buf.npos;
            auto failed = CheckHttpHead(buf, line);
            if (!failed && line != buf.npos) {
                failed = CheckKey(buf, line);
                if (!failed && readyState_ == OPEN) {
                    line += 2;//skip '\r\n';
                    *len -= line;
                    if (*len > 0) memmove(data, data + line, *len);
                }
            }
            if (failed){
                LOG(LS_ERROR) << failed;
                return ForceClose(-1);
            }
        }
        while (readyState_ == OPEN && *len >= 2) {//Need at least 2
            const bool fin = (data[0] & 0x80) == 0x80;
            const int opcode = data[0] & 0x0f;
            const bool mask = (data[1] & 0x80) == 0x80;
            size_t N = (data[1] & 0x7f);
            const size_t header_size = 2 + (N == 126 ? 2 : 0) + (N == 127 ? 8 : 0) + (mask ? 4 : 0);
            if (*len < header_size)
                return; 

            if (N == 126)
                N = (((uint8_t)data[2]) << 8) | (size_t)((uint8_t) data[3]);
            else if (N == 127) {
                N = 0;
                for (int k=0; k< 8; ++k) 
                    N = (N << 8) | (uint8_t)data[2+k];
            }
            if (*len < header_size+N)
                return; 

            if (mask){
                char* masking_key = data + header_size - 4;
                for (size_t i = 0; i != N; ++i)
                    data[i+header_size] ^= masking_key[i&0x3];
            }

            if (opcode == TEXT_FRAME 
                || opcode ==BINARY_FRAME
                || opcode == CONTINUATION) {
                packet_.insert(packet_.end(), data+header_size, data+header_size + N);
                if (fin){
                    packet_.push_back('\0');
                    SignalPacketEvent(&packet_[0], packet_.size()-1);
                    packet_.clear();
                }
            }
            else if (opcode == PING)
                SendPacket(PONG, data+header_size, N, false);
            else if (opcode == CLOSE){ 
                return ForceClose(0);
            }
            else if (opcode != PONG)
                LOG(LS_ERROR) << "Got unexpected WebSocket message" << opcode;

            *len -= header_size+N;
            if (*len > 0)
                memmove(data, data + header_size + N, *len);
        }
    }
};
typedef void (JSEP_CDECL_CALL *RTCSessionCallback)(RTCSessionObserver*userdata, enum RTCSessionEvent event, const char* json, int len);
typedef void (JSEP_CDECL_CALL *SocketCallback)(RTCSocketObserver* userdata, RTCSocket* rs, const char* message, int length, enum RTCSocketEvent event);
static std::string EscapeJSONString(const std::string& str) {
    std::ostringstream oss;
    for (const auto ch : str) {
        switch (ch) {
        case '"':
            oss << "\\\"";
            break;
        case '\b':
            oss << "\\b";
            break;
        case '\f':
            oss << "\\f";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        case '\\':
            oss << "\\\\";
            break;
        default:
            oss << ch;
            break;
        }
    }
    return oss.str();
}
static std::string ToString(const webrtc::IceCandidateInterface* iface)
{
    std::string sdp;
    if (!iface || !iface->ToString(&sdp)){
        LOG(LS_ERROR) << "ice to string failed";
        return "";
    }
    Json::StyledWriter writer;
    Json::Value jmessage;

    jmessage[kCandidateSdpMidName] = iface->sdp_mid();
    jmessage[kCandidateSdpMlineIndexName] = iface->sdp_mline_index();
    jmessage[kCandidateSdpName] = sdp;
    return writer.write(jmessage);
}
static std::string ToString(const webrtc::SessionDescriptionInterface* iface)
{
    std::string sdp;
    if (!iface || !iface->ToString(&sdp)){
        LOG(LS_ERROR) << "sdp to string failed";
        return "";
    }
    Json::StyledWriter writer;
    Json::Value jmessage;
    jmessage[kSessionDescriptionTypeName] = iface->type();
    jmessage[kSessionDescriptionSdpName] = sdp;
    return writer.write(jmessage);
}
static webrtc::IceCandidateInterface* NewIceCandidate(const char* message)
{
    Json::Value json;
    {
        Json::Reader reader;
        if (!reader.parse(message, json, false)) {
            LOG(LS_ERROR) << "Received unknown message. " << message;
            return nullptr;
        }
    }
    std::string sdp_mid;
    int sdp_mlineindex = 0;
    std::string sdp;
    if (!rtc::GetStringFromJsonObject(json, kCandidateSdpMidName, &sdp_mid) ||
        !rtc::GetIntFromJsonObject(json, kCandidateSdpMlineIndexName, &sdp_mlineindex) ||
        !rtc::GetStringFromJsonObject(json, kCandidateSdpName, &sdp)) {
        LOG(LS_ERROR) << "Can't parse received ice message.";
        return nullptr;
    }
    webrtc::SdpParseError error;
    auto candidate = webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error);
    if (!candidate) {
        LOG(LS_ERROR) << "Can't parse received candidate message. "
            << "SdpParseError was: " << error.description;
        return nullptr;
    }
    return candidate;
}
static webrtc::SessionDescriptionInterface* NewSessionDescription(const char* message)
{
    Json::Value json;
    {
        Json::Reader reader;
        if (!reader.parse(message, json, false)) {
            LOG(LS_ERROR) << "Received unknown message. " << message;
            return nullptr;
        }
    }
    std::string type;
    std::string sdp;
    if (!rtc::GetStringFromJsonObject(json, kSessionDescriptionTypeName, &type) ||
        !rtc::GetStringFromJsonObject(json, kSessionDescriptionSdpName, &sdp)) {
        LOG(LS_ERROR) << "Can't parse received sdp message.";
        return nullptr;
    }
    webrtc::SdpParseError error;
    auto desc = webrtc::CreateSessionDescription(type, sdp, &error);
    if (!desc) {
        LOG(LS_ERROR) << "Can't parse received session description message. "
            << "SdpParseError was: " << error.description;
        return nullptr;
    }
    return desc;
}
struct ZmfVideoRender : public rtc::VideoSinkInterface<webrtc::VideoFrame>
{
    rtc::scoped_refptr<webrtc::VideoTrackInterface> track_;
    int sourceType_;
    std::string renderId_;
    std::unique_ptr<uint8_t[]> i420_;
    int i420Len_;
    const bool asCapture_;
    int croppedWidth_;
    int croppedHeight_;
    ZmfVideoRender(webrtc::VideoTrackInterface* iface, const int type, const std::string& render_id, const bool asCapture) :
        track_(iface), sourceType_(type), renderId_(render_id), i420Len_(0), asCapture_(asCapture) {
            track_->AddOrUpdateSink(this, rtc::VideoSinkWants());
        }
    virtual ~ZmfVideoRender() {
        track_->RemoveSink(this);
        if (asCapture_)
            Zmf_OnVideoCaptureDidStop(renderId_.c_str());
        else
            Zmf_OnVideoRender (renderId_.c_str(), sourceType_, 0, 0, 0, 0, 0, 0);
    }
    virtual void OnFrame(const webrtc::VideoFrame& video_frame) override {
        rtc::scoped_refptr<webrtc::I420BufferInterface> buffer(
            video_frame.video_frame_buffer()->ToI420());
        const int imgAngle = video_frame.rotation();
        const int width = buffer->width();
        const int height = buffer->height();
        const int size = width * height * 3 / 2;
        if (size != i420Len_) {
            i420Len_ = size;
            i420_.reset(new uint8[size]);
            croppedWidth_  = width & ~7;
            croppedHeight_ = height& ~7;
        }
        const int dx = ((width - croppedWidth_)>>1)  & ~3;
        const int dy = ((height - croppedHeight_)>>1)& ~3;
        const int uv_dx = dx / 2;
        const int uv_dy = dy / 2;
        const uint8_t* y_plane = buffer->DataY() + buffer->StrideY()* dy + dx;
        const uint8_t* u_plane = buffer->DataU() + buffer->StrideU() * uv_dy + uv_dx;
        const uint8_t* v_plane = buffer->DataV() + buffer->StrideV() * uv_dy + uv_dx;
        uint8_t* dst_y = i420_.get();
        uint8_t* dst_u = dst_y + croppedWidth_ * croppedHeight_;
        uint8_t* dst_v = dst_u + croppedWidth_ * croppedHeight_ / 4;
        const int dst_pitch_y = croppedWidth_;
        const int dst_pitch_u = dst_pitch_y / 2;
        const int dst_pitch_v = dst_pitch_u;
        if (libyuv::I420Copy(
                y_plane, buffer->StrideY(),
                u_plane, buffer->StrideU(),
                v_plane, buffer->StrideV(),
                dst_y, dst_pitch_y, dst_u, dst_pitch_u, dst_v, dst_pitch_v,
                croppedWidth_, croppedHeight_) == 0){
            if (asCapture_)
                Zmf_OnVideoCapture (renderId_.c_str(), 1/*ZmfVideoFaceFront*/, imgAngle, 0, &croppedWidth_, &croppedHeight_, dst_y, 0);
            else
                Zmf_OnVideoRender(renderId_.c_str(), sourceType_, imgAngle, 0, &croppedWidth_, &croppedHeight_, dst_y, 0);
        }
    }
};
struct MediaConstraints : public webrtc::MediaConstraintsInterface
{
    MediaConstraints(const Json::Value& json) { AddJson(json); }
    MediaConstraints(const char* constraints=nullptr) {
        if (!constraints || !constraints[0]) return;
        Json::Value json;
        Json::Reader reader;
        if (!reader.parse(constraints, json, false) || !json.isObject()) {
            LOG(LS_ERROR) << "unknown constraints. " << constraints;
            return;
        }
        AddJson(json);
    }
    void AddJson(const Json::Value& json) {
        if (!json.isObject()) return;
        auto keys = json.getMemberNames();
        for (const auto& key : json.getMemberNames()) {
            std::string out;
            if (rtc::GetStringFromJson(json[key], &out))
                mandatory_.emplace_back(key, out);
        }
    }
    virtual const Constraints& GetMandatory() const override { return mandatory_; }
    virtual const Constraints& GetOptional() const override  { return optional_; }
    Constraints& GetMandatory() { return mandatory_; }
    Constraints& GetOptional() { return optional_; }
    Constraints mandatory_;
    Constraints optional_;
    void AddReceiveOptional() {
        optional_.emplace_back(webrtc::MediaConstraintsInterface::kOfferToReceiveAudio, "true");
        optional_.emplace_back(webrtc::MediaConstraintsInterface::kOfferToReceiveVideo, "true");
        optional_.emplace_back(webrtc::MediaConstraintsInterface::kVoiceActivityDetection, "true");
        optional_.emplace_back(webrtc::MediaConstraintsInterface::kUseRtpMux, "true");
    }
};
class SessionObserverProxy :
    public webrtc::PeerConnectionObserver,
    public webrtc::DtmfSenderObserverInterface,
    public rtc::MessageHandler
{
private:
    RTCSessionCallback callback_;
    RTCSessionObserver *observer_;
    rtc::CriticalSection lock_;
    rtc::scoped_refptr<webrtc::DtmfSenderInterface> dtmfSender_;
    struct ChannelObserverProxy: public webrtc::DataChannelObserver {
        SessionObserverProxy* proxy_;
        const std::string channelId_;
        rtc::scoped_refptr<webrtc::DataChannelInterface> iface_;
        ChannelObserverProxy(SessionObserverProxy* proxy, rtc::scoped_refptr<webrtc::DataChannelInterface> iface) :
            proxy_(proxy), channelId_(iface->label()), iface_(iface){
                iface->RegisterObserver(this);
                OnStateChange();
            }
        virtual ~ChannelObserverProxy() {
            iface_->UnregisterObserver();
        }
        virtual void OnStateChange() override {
            auto state = iface_->state();
            if (state == iface_->kOpen){
                auto observer = proxy_->GetObserver();
                if (!observer.first && !observer.second) return;

                char config[1024];
                sprintf(config,
                    "{\"ordered\":%s,\"maxPacketLifeTime\":%d,\"negotiated\":%s,\"maxRetransmits\":%d,\"protocol\":\"%s\"}",
                    iface_->ordered()? "true":"false",
                    iface_->maxRetransmitTime(),
                    iface_->negotiated() ? "true":"false",
                    iface_->maxRetransmits(),
                    iface_->protocol().c_str());

                if (observer.first) {
                    std::ostringstream oss; {
                        oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_DataChannelOpen
                            <<",\""<<JsepChannelId<<"\":\""<<channelId_
                            <<"\",\""<<JsepChannelConfig<<"\":"<<config
                            <<"}";
                    }
                    const std::string& json = oss.str();
                    observer.first(observer.second, RTCSessionEvent_DataChannelOpen, json.data(), (int)json.size());
                }
                else if (observer.second)
                    observer.second->OnDataChannelOpen(channelId_.c_str(), config);
            }
            else if (state == iface_->kClosing){
                auto observer = proxy_->GetObserver();
                static const char* reason = "closing";
                if (observer.first) {
                    std::ostringstream oss;
                    oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_DataChannelClose
                        <<",\""<<JsepChannelId<<"\":\""<<channelId_
                        <<"\",\""<<JsepReason<<"\":\""<<reason
                        <<"\"}";

                    const std::string& json = oss.str();
                    observer.first(observer.second, RTCSessionEvent_DataChannelClose, json.data(), (int)json.size());
                }
                else if (observer.second)
                    observer.second->OnDataChannelClose(channelId_.c_str(), reason);
            }
            else if (state == iface_->kClosed)
                proxy_->CloseChannel(channelId_);
        }

        virtual void OnMessage(const webrtc::DataBuffer& buffer) override {
            auto observer = proxy_->GetObserver();
            if (observer.first) {
                std::ostringstream oss;
                oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_DataChannelMessage
                    <<",\""<<JsepChannelId<<"\":\""<<channelId_
                    <<"\",\""<<JsepMessage<<"\":\""
                    <<EscapeJSONString(std::string((const char*)buffer.data.data(), buffer.size()))
                    <<"\"}";

                const std::string& json = oss.str();
                observer.first(observer.second, RTCSessionEvent_DataChannelMessage, json.data(), (int)json.size());
            }
            else if (observer.second)
                observer.second->OnDataChannelMessage(channelId_.c_str(), (const char*)buffer.data.data(), (int)buffer.size());
        }
    };
    typedef std::unordered_map<std::string, std::shared_ptr<ChannelObserverProxy> >  ChannelMap;
    ChannelMap  dataChannels_;
public:
    std::pair<RTCSessionCallback, RTCSessionObserver*> GetObserver() {
        rtc::CritScope _(&lock_);
        return std::make_pair(callback_, observer_);
    }
    const bool is_caller_;
    webrtc::PeerConnectionInterface::SignalingState signalingState_;
    webrtc::SessionDescriptionInterface* offer_desc_;
    bool renegotiation_needed_;
    bool negotiating_;
    SessionObserverProxy(RTCSessionCallback callback, RTCSessionObserver* observer, bool is_caller) :
        callback_(callback), observer_(observer), is_caller_(is_caller), offer_desc_(nullptr),renegotiation_needed_(false),negotiating_(false){
            signalingState_ = is_caller_ ? decltype(signalingState_)::kStable : decltype(signalingState_)::kClosed;
        }
    rtc::scoped_refptr<webrtc::DataChannelInterface> GetChannel(const std::string& channelId) {
        rtc::CritScope _(&lock_);
        auto iter = dataChannels_.find(channelId);
        return iter != dataChannels_.end() ? iter->second->iface_ : nullptr;
    }
    void AddChannel(webrtc::DataChannelInterface* data_channel) {
        auto label = data_channel->label();
        auto proxy = std::make_shared<ChannelObserverProxy>(this, data_channel); {
            rtc::CritScope _(&lock_);
            dataChannels_.emplace(label, proxy);
        }
    }
    void CloseChannel(const std::string& channelId) {
        rtc::CritScope _(&lock_);
        dataChannels_.erase(channelId);
    }
    void SetDtmfSender(webrtc::DtmfSenderInterface* iface) {
        if (iface) iface->RegisterObserver(this);
        if (dtmfSender_)dtmfSender_->UnregisterObserver();
        rtc::CritScope _(&lock_);
        dtmfSender_ = iface;
    }
    rtc::scoped_refptr<webrtc::DtmfSenderInterface> GetDtmfSender() {
        rtc::CritScope _(&lock_);
        return dtmfSender_;
    }
    int CreateOffer(webrtc::PeerConnectionInterface* iface, const char* constraints); 
    int CreateAnswer(webrtc::PeerConnectionInterface* iface, const char* constraints);
    int SetRemoteDescription(webrtc::PeerConnectionInterface* iface, const char* sdp);
    int SetLocalDescription(webrtc::PeerConnectionInterface* iface, const char* sdp);
    RTCSessionObserver* Close() {
        MainThread::Instance().Clear(this);
        rtc::CritScope _(&lock_);
        renegotiation_needed_ = false;
        RTCSessionObserver* userdata = observer_;
        observer_ = nullptr;
        callback_ = nullptr;
        if (dtmfSender_) dtmfSender_->UnregisterObserver();
        dataChannels_.clear();
        dtmfSender_ = nullptr;
        delete offer_desc_;
        offer_desc_ = nullptr;
        return userdata;
    }
    virtual void OnToneChange(const std::string& tone) override {
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_ToneChange
                <<",\""<<JsepTone<<"\":\""<<tone
                <<"\"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_ToneChange, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnToneChange(tone.c_str());
    }
    virtual void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_AddRemoteStream
                <<",\""<<JsepStreamId<<"\":\""<<stream->label()
                <<"\",\""<<JsepAudioTrackCount<<"\":"<<stream->GetAudioTracks().size()
                <<",\""<<JsepVideoTrackCount<<"\":"<<stream->GetVideoTracks().size()
                <<"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_AddRemoteStream, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnAddRemoteStream(stream->label().c_str(), (int)stream->GetAudioTracks().size(), (int)stream->GetVideoTracks().size());
    }
    virtual void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {
        MainThread::Instance().RemoveVideoSource(stream);
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_RemoveRemoteStream
                <<",\""<<JsepStreamId<<"\":\""<<stream->label()
                <<"\"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_RemoveRemoteStream, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnRemoveRemoteStream(stream->label().c_str());
    }
    virtual void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override {
        AddChannel(data_channel);
    }
    virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        auto observer = GetObserver();
        if (!observer.first && !observer.second) return;

        const std::string& ice = ToString(candidate);
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_IceCandidate
                <<",\""<<JsepIceCandidate<<"\":"<<ice
                <<"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_IceCandidate, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnIceCandidate(ice.c_str());
    }
    virtual void OnIceGatheringChange(
        webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
    virtual void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override {
        if (signalingState_ == new_state) return;
        static const char* kSignalingStateNames[] = {
            "stable",
            "have-local-offer",
            "have-local-pranswer",
            "have-remote-offer",
            "have-remote-pranswer",
            "closed",
        };
        signalingState_ = new_state;
        MainThread::Instance().Clear(this);
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_SignalingChange
                <<",\""<<JsepSignalingState<<"\":\""<<kSignalingStateNames[new_state]<<"\"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_SignalingChange, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnSignalingChange(kSignalingStateNames[new_state]);

        if (new_state == decltype(signalingState_)::kStable){
            negotiating_ = false;
            if (renegotiation_needed_) MainThread::Instance().Post(RTC_FROM_HERE, this);
        }
    }
    virtual void OnRenegotiationNeeded() override {
        MainThread::Instance().Clear(this);
        renegotiation_needed_ = true;
        if (signalingState_ == decltype(signalingState_)::kStable)
            MainThread::Instance().Post(RTC_FROM_HERE, this);
    }
    virtual void OnMessage(rtc::Message* msg) override {
        if (renegotiation_needed_) _OnRenegotiationNeeded();
    }
    void _OnRenegotiationNeeded() {
        renegotiation_needed_ = false;
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_RenegotiationNeeded
                <<"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_RenegotiationNeeded, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnRenegotiationNeeded();
    }
    virtual void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override {
        static const char* kIceStateNames[] = {
            "new",
            "checking",
            "connected",
            "completed",
            "failed",
            "disconnected",
            "closed"
        };

        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_IceConnectionStateChange
                <<",\""<<JsepIceConnectionState<<"\":\""<<kIceStateNames[new_state]<<"\"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_IceConnectionStateChange, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnIceConnectionStateChange(kIceStateNames[new_state]);
    }
    void OnCreateDescriptionSuccess(const std::string& type, webrtc::SessionDescriptionInterface* desc) {
        auto observer = GetObserver();
        if (!observer.first && !observer.second) return;

        const std::string& sdp = ToString(desc);
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_CreateDescriptionSuccess
                <<",\""<<JsepSdpType<<"\":\"" <<type
                <<"\",\""<<JsepSdp<<"\":"<<sdp
                <<"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_CreateDescriptionSuccess, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnCreateDescriptionSuccess(type.c_str(), sdp.c_str());
    }
    void OnCreateDescriptionFailure(const std::string& type, const std::string& error) {
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_CreateDescriptionFailure
                <<",\""<<JsepSdpType<<"\":\"" <<type
                <<"\",\""<<JsepReason<<"\":\""<<error
                <<"\"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_CreateDescriptionFailure, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnCreateDescriptionFailure(type.c_str(), error.c_str());
    }
    void OnSetDescriptionSuccess(const std::string& type, const std::string& locate, const std::string& sdp) {
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_SetDescriptionSuccess
                <<",\""<<JsepSdpType<<"\":\"" <<type
                <<"\",\""<<JsepSdpLocation<<"\":\""<<locate
                <<"\",\""<<JsepSdp<<"\":"<<sdp
                <<"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_SetDescriptionSuccess, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnSetDescriptionSuccess(type.c_str(), locate.c_str(), sdp.c_str());
    }
    void OnSetDescriptionFailure(const std::string& type, const std::string& locate, const std::string& error) {
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_SetDescriptionFailure
                <<",\""<<JsepSdpType<<"\":\"" <<type
                <<"\",\""<<JsepSdpLocation<<"\":\""<<locate
                <<"\",\""<<JsepReason<<"\":\""<<error
                <<"\"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_SetDescriptionFailure, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnSetDescriptionFailure(type.c_str(), locate.c_str(), error.c_str());
    }
    void OnStatsReport(const char* type, const std::string& id, const std::string& stats, const double timestamp) {
        auto observer = GetObserver();
        if (observer.first) {
            std::ostringstream oss;
            oss <<"{\""<<JespEvent<<"\":"<<RTCSessionEvent_StatsReport
                <<",\""<<JsepStatsType<<"\":\"" <<type
                <<"\",\""<<JsepStatsId<<"\":\""<<id
                <<"\",\""<<JsepStatsTimestamp<<"\":"<<timestamp
                <<",\""<<JsepStats<<"\":"<<stats
                <<"}";

            const std::string& json = oss.str();
            observer.first(observer.second, RTCSessionEvent_StatsReport, json.data(), (int)json.size());
        }
        else if (observer.second)
            observer.second->OnStatsReport(type, id.c_str(), stats.c_str(), timestamp);
    }
};
std::shared_ptr<SessionObserverProxy> MainThread::GetSessionObserver(void* peer) {
    rtc::CritScope _(&lock_);
    auto iter = sessionObserverMap_.find(peer);
    if (iter != sessionObserverMap_.end()) {
        auto observer = iter->second->GetObserver();
        if (observer.first || observer.second)
            return iter->second;
    }
    return nullptr;
}
struct CreateSDPObserver : public webrtc::CreateSessionDescriptionObserver
{
    webrtc::PeerConnectionInterface* iface_;
    const std::string type;
    CreateSDPObserver(webrtc::PeerConnectionInterface* iface, const std::string& type)
        :iface_(iface), type(type) {}
    virtual void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        auto proxy = MainThread::Instance().GetSessionObserver(iface_);
        if (proxy) proxy->OnCreateDescriptionSuccess(desc->type(), desc);
    }
    virtual void OnFailure(const std::string& error) override {
        auto proxy = MainThread::Instance().GetSessionObserver(iface_);
        if (proxy) proxy->OnCreateDescriptionFailure(type, error);
    }
};
struct SetSDPObserver : public webrtc::SetSessionDescriptionObserver
{
    webrtc::PeerConnectionInterface* iface_;
    const std::string type,locate,sdp;
    SetSDPObserver(webrtc::PeerConnectionInterface* iface,
        const std::string& type, const std::string& locate, const std::string& desc)
        :iface_(iface), type(type), locate(locate), sdp(desc) {}
    virtual void OnSuccess() override {
        if (!sdp.size()) return;
        auto proxy = MainThread::Instance().GetSessionObserver(iface_);
        if (proxy) proxy->OnSetDescriptionSuccess(type, locate, sdp);
    }
    virtual void OnFailure(const std::string& error) override {
        auto proxy = MainThread::Instance().GetSessionObserver(iface_);
        if (proxy) proxy->OnSetDescriptionFailure(type, locate, error);
    }
};
int SessionObserverProxy::CreateOffer(webrtc::PeerConnectionInterface* iface, const char* constraints) {
    if (negotiating_) {
        LOG(LS_ERROR) << "can't create offer when negotiating";
        return RTCSessionError_InvalidOperation;
    }
    else {
        delete offer_desc_;
        offer_desc_ = nullptr;
        negotiating_ = true;
        renegotiation_needed_ = false;
    }
    MediaConstraints fake(constraints);
    fake.AddReceiveOptional();
    iface->CreateOffer(new rtc::RefCountedObject<CreateSDPObserver>(iface, "offer"), &fake);
    return 0;
}
int SessionObserverProxy::CreateAnswer(webrtc::PeerConnectionInterface* iface, const char* constraints) {
    if (negotiating_) {
        LOG(LS_ERROR) << "can't create answer when negotiating";
        return RTCSessionError_InvalidOperation;
    }
    else {
        delete offer_desc_;
        offer_desc_ = nullptr;
        renegotiation_needed_ = false;
    }
    MediaConstraints fake(constraints);
    fake.AddReceiveOptional();
    iface->CreateAnswer(new rtc::RefCountedObject<CreateSDPObserver>(iface, "answer"),&fake);
    return 0;
}
int SessionObserverProxy::SetRemoteDescription(webrtc::PeerConnectionInterface* iface, const char* sdp) {
    auto desc = NewSessionDescription(sdp);
    if (!desc) {
        LOG(LS_ERROR) << "invalid remote sdp format";
        return RTCSessionError_InvalidArgument;
    }
    if (negotiating_) {
        const bool isOffer = desc->type() == webrtc::SessionDescriptionInterface::kOffer;
        if (is_caller_) {
            RTC_CHECK(!offer_desc_);
            if (isOffer){
                LOG(LS_WARNING)<<"give up setting remote offer desc";
                delete desc;
                return RTCSessionError_InvalidOperation;
            }
        }
        else {
            if (isOffer){//giveup
                LOG(LS_WARNING) << "give up setting local offer desc to delay renegotiation";
                delete offer_desc_;
                negotiating_ = false;
                renegotiation_needed_ = true;
            }
            else if (offer_desc_){
                iface->SetLocalDescription(
                    new rtc::RefCountedObject<SetSDPObserver>(iface, offer_desc_->type(), "local", ""),
                    offer_desc_);
            }
            offer_desc_ = nullptr;
        }
    }
    iface->SetRemoteDescription(new rtc::RefCountedObject<SetSDPObserver>(iface, desc->type(), "remote", sdp), desc);
    return 0;
}
int SessionObserverProxy::SetLocalDescription(webrtc::PeerConnectionInterface* iface, const char* sdp){
    auto desc = NewSessionDescription(sdp);
    if (!desc) {
        LOG(LS_ERROR) << "invalid local sdp format";
        return RTCSessionError_InvalidArgument;
    }
    if (desc->type() == webrtc::SessionDescriptionInterface::kOffer) {
        if (!negotiating_) {
            LOG(LS_WARNING) << "give up setting local offer desc";
            return RTCSessionError_InvalidOperation;
        }
        if (!is_caller_) {
            RTC_CHECK(!offer_desc_);
            offer_desc_ = desc;
            OnSetDescriptionSuccess(desc->type(), "local", sdp);
            return 0;
        }
    }
    iface->SetLocalDescription(new rtc::RefCountedObject<SetSDPObserver>(iface, desc->type(), "local", sdp), desc);
    return 0;
}

struct GetStatsObserver : public webrtc::StatsObserver
{
    webrtc::PeerConnectionInterface* iface_;
    std::set<std::string> statstypes_;
    GetStatsObserver(webrtc::PeerConnectionInterface* iface, const std::string& statstype)
        :iface_(iface)
    {
        if (!statstype.empty())
            SplitString(statstype, statstypes_, ',');
    }
    virtual void OnComplete(const webrtc::StatsReports& reports) override {
        for (auto stats : reports) {
            if (stats->empty()) continue;
            if (statstypes_.size() > 0 && statstypes_.find(stats->TypeToString()) == statstypes_.end()) continue;

            std::ostringstream oss;
            oss << '{';
            for (auto& it : stats->values()){
                auto& v = it.second;
                const auto type = v->type();
                oss << '\"' << v->display_name();
                if (type == v->kInt || type ==  v->kInt64 || type == v->kFloat || type == v->kBool)
                    oss << "\":" << it.second->ToString();
                else
                    oss << "\":\""<< EscapeJSONString(it.second->ToString())<< '\"';
                oss << ',';
            }
            oss.seekp(-1, std::ios::cur) << '}';
            {
                auto proxy = MainThread::Instance().GetSessionObserver(iface_);
                if (!proxy) return;
                proxy->OnStatsReport(stats->TypeToString(), stats->id()->ToString(), oss.str(), stats->timestamp());
            }
        }
    }
};
struct ZmfVideoCapturer: public cricket::VideoCapturer
{
    const bool is_render_;
    bool running_;
    rtc::VideoSinkWants wants_;
    ZmfVideoCapturer(const std::string& id, bool isRender):is_render_(isRender), running_(false){
        SetId(id);
        const int kVideoSizes[][2]={
            {128, 96}, {160, 120}, {176, 144},
            {320, 180}, {320, 240}, {352, 288}, {480, 270},
            {640, 360}, {640, 480}, {720, 576}, {800, 600}, {960, 540},
            {1024, 768}, {1280, 640}, {1280, 720}, {1440, 1152},
            {1920, 960}, {1920, 1080},
            {0,0}
        };
        std::vector<cricket::VideoFormat> supported;
        for (int i=0;kVideoSizes[i][0];++i){
            supported.emplace_back(kVideoSizes[i][0], kVideoSizes[i][1], cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420);
            supported.emplace_back(kVideoSizes[i][0], kVideoSizes[i][1], cricket::VideoFormat::FpsToInterval(10), cricket::FOURCC_I420);
        }
        SetSupportedFormats(supported);
    }
    virtual bool GetPreferredFourccs(std::vector<uint32>* fourccs) override {
        if (!fourccs) return false;
        fourccs->push_back(cricket::FOURCC_I420);
        return true;
    }
    virtual void OnSinkWantsChanged(const rtc::VideoSinkWants& wants) override {
        cricket::VideoCapturer::OnSinkWantsChanged(wants);
        wants_ = wants;
    }
    void FrameCallback(int* iWidth, int* iHeight, unsigned char *buf, int iImgAngle) {
        const int origin_width = *iWidth;
        const int origin_height = *iHeight;
        const int area = origin_width*origin_height;
        rtc::scoped_refptr<webrtc::I420Buffer> i420Buf;
        if (wants_.black_frames) {
            if (wants_.max_pixel_count >= area)
                i420Buf = webrtc::I420Buffer::Create(origin_width, origin_height);
            else {
                const float scale = sqrtf(float(wants_.max_pixel_count) / float(area));
                i420Buf = webrtc::I420Buffer::Create((int)(origin_width* scale)&(~7), (int)(origin_height* scale)&(~7));
            }
            webrtc::I420Buffer::SetBlack(i420Buf);
        }
        else {
            const int pitch_y = origin_width;
            const int pitch_u = origin_width / 2;
            const int pitch_v = pitch_u;
            const uint8_t* data_y = (uint8_t*)buf;
            const uint8_t* data_u = data_y + area;
            const uint8_t* data_v = data_u + area / 4;
            i420Buf = webrtc::I420Buffer::Copy(origin_width, origin_height, data_y, pitch_y, data_u, pitch_u, data_v, pitch_v);
            if (wants_.max_pixel_count < area) {
                const float scale = sqrtf(float(wants_.max_pixel_count) / float(area));
                auto scaledBuf = webrtc::I420Buffer::Create((int)(origin_width* scale)&(~7), (int)(origin_height* scale)&(~7));
                scaledBuf->CropAndScaleFrom(*i420Buf);
                std::swap(i420Buf, scaledBuf);
            }
        }
        OnFrame(webrtc::VideoFrame(i420Buf, (webrtc::VideoRotation)iImgAngle, rtc::TimeMicros()), origin_width, origin_height);
    }
    virtual bool IsRunning() override { return running_; }
    static void CaptureCallback(void* pUser, const char* captureId, int iFace, 
        int iImgAngle, int iCaptureOrient, int* iWidth, int* iHeight,
        unsigned char *buf, ZmfVideoCaptureEncoder* encoder) {
        if (!captureId || !captureId[0] || !buf || !iWidth || !iHeight)
            return;
        ZmfVideoCapturer* self = (ZmfVideoCapturer*)pUser;
        if (!self->running_ || self->GetId() != captureId)
            return;
        self->FrameCallback(iWidth, iHeight, buf, iImgAngle);
    }
    static int RenderCallback(void* pUser, const char* renderId, int sourceType, int iAngle,
        int iMirror, int* iWidth, int* iHeight, unsigned char *buf, unsigned long timeStamp) {
        if (sourceType != 0 /* ZmfVideoSourcePeer */ 
            || !renderId || !renderId[0] || !buf || !iWidth || !iWidth[0] || !iHeight || !iHeight[0])
            return 0;
        ZmfVideoCapturer* self = (ZmfVideoCapturer*)pUser;
        if (!self->running_ || self->GetId() != renderId)
            return 0;
        self->FrameCallback(iWidth, iHeight, buf, iAngle);
        return 1;
    }
    virtual cricket::CaptureState Start(const cricket::VideoFormat& capture_format) override {
        if (!running_) {
            running_ = true;
            if (is_render_) 
                Zmf_VideoRenderAddCallback(this, RenderCallback);
            else {
                Zmf_VideoCaptureAddCallback(this, CaptureCallback);
                Zmf_VideoCaptureRequestStart(GetId().c_str(), capture_format.width, capture_format.height, capture_format.framerate());
            }
        }
        return cricket::CS_RUNNING;
    }
    virtual void Stop() override {
        if (running_) {
            running_ = false;
            if (is_render_)
                Zmf_VideoRenderRemoveCallback(this);
            else {
                Zmf_VideoCaptureRemoveCallback(this);
                Zmf_VideoCaptureRequestStop(GetId().c_str());
            }
        }
    }
    virtual bool IsScreencast() const { return false; }
};
static void ToDataChannelInit(webrtc::DataChannelInit& init, Json::Value& json)
{
    if (json.isMember("ordered"))
        init.ordered = json["ordered"].asBool();
    if (json.isMember("maxPacketLifeTime"))
        init.maxRetransmitTime = json["maxPacketLifeTime"].asInt();
    if (json.isMember("maxRetransmits"))
        init.maxRetransmits = json["maxRetransmits"].asInt();
    if (json.isMember("negotiated"))
        init.negotiated = json["negotiated"].asBool();
    if (json.isMember("protocol"))
        init.protocol = json["protocol"].asString();
}
}
namespace webrtc {
cricket::PortAllocator* CreatePortAllocator(){
    return MainThread::Instance().CreatePortAllocator(); }
}
static int API_LEVEL_1_CreateDataChannel (RTCPeerConnection* peer,
    const char* channelId, const char* constraints)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface || !channelId){
        LOG(LS_ERROR) << "JSEP_CreateChannel invalid param";
        return RTCSessionError_InvalidArgument;
    }
    webrtc::DataChannelInit init;
    if (constraints && constraints[0]) {
        Json::Value json; {
            Json::Reader reader;
            if (!reader.parse(constraints, json, false)) {
                LOG(LS_ERROR) << "constraints invalid JSON. " << constraints;
                return RTCSessionError_InvalidArgument;
            }
        }
        ToDataChannelInit(init, json);
    }
    auto proxy = MainThread::Instance().GetSessionObserver(peer);
    proxy->AddChannel(iface->CreateDataChannel(channelId, &init));
    return 0;
}

static void  API_LEVEL_1_CloseDataChannel(RTCPeerConnection* iface, const char* channelId)
{
    if (iface || !channelId) return;
    auto proxy = MainThread::Instance().GetSessionObserver(iface);
    if (proxy) proxy->CloseChannel(channelId);
}
static  bool ParseRTCConfiguration(const std::string& config,
    webrtc::PeerConnectionInterface::RTCConfiguration& rtc_config,
    webrtc::PeerConnectionFactoryInterface::Options& rtc_options, bool& has_options){
    Json::Value json; {
        Json::Reader reader;
        if (!reader.parse(config, json, false)) return false;
    }
    for (auto& it : json["iceServers"]) {
        webrtc::PeerConnectionInterface::IceServer server;
        bool hasURL = false;
        if (it.isMember("urls")) {
            Json::Value& urls = it["urls"];
            if (urls.isArray())
                hasURL = rtc::JsonArrayToStringVector(urls, &server.urls);
            else
                hasURL = rtc::GetStringFromJson(urls, &server.uri);
        }
        else
            hasURL = rtc::GetStringFromJsonObject(it, "url", &server.uri);
        if (!hasURL) return false;

        rtc::GetStringFromJsonObject(it, "username", &server.username);
        rtc::GetStringFromJsonObject(it, "credential", &server.password);
        rtc_config.servers.push_back(server);
    }
    if (json.isMember("constraints")) {
        auto& options =json["constraints"];
        MediaConstraints constraints(options);
        webrtc::CopyConstraintsIntoRtcConfiguration(&constraints, &rtc_config);
        if (options.isMember("minPort") || options.isMember("maxPort")){
            if (!rtc::GetIntFromJsonObject(options, "minPort", &rtc_config.min_port)
                || !rtc::GetIntFromJsonObject(options, "maxPort", &rtc_config.max_port)
                || rtc_config.min_port > rtc_config.max_port)
                return false;
        }
    }
    has_options = json.isMember("options");
    if (has_options){
        auto& options =json["options"];
        rtc::GetIntFromJsonObject(options, "networkIgnoreMask", &rtc_options.network_ignore_mask);
        rtc::GetBoolFromJsonObject(options, "disableEncryption", &rtc_options.disable_encryption);
    }
    return true;
}
static bool ParseRTCIceParameters(const std::string& config, cricket::IceParameters& ice_param){
    Json::Value json; {
        Json::Reader reader;
        if (!reader.parse(config, json, false)) return false;
    }
    if (!rtc::GetStringFromJsonObject(json, "ufrag", &(ice_param.ufrag))
        && !rtc::GetStringFromJsonObject(json, "username", &(ice_param.ufrag))
        && !rtc::GetStringFromJsonObject(json, "usernameFragment", &(ice_param.ufrag)))
        return false;
    if (!rtc::GetStringFromJsonObject(json, "password", &(ice_param.pwd)))
        return false;
    if (!rtc::GetBoolFromJsonObject(json, "renomination", &(ice_param.renomination)))
        rtc::GetBoolFromJsonObject(json, "iceLite", &(ice_param.renomination));

    if ((int)ice_param.ufrag.size() < cricket::ICE_UFRAG_LENGTH)
        return false;
    if ((int)ice_param.pwd.size() < cricket::ICE_PWD_LENGTH)
        return false;
    return true;
}
#ifdef __APPLE__
extern "C" void objcRTCSessionCallback(struct RTCSessionObserver*userdata, enum RTCSessionEvent event, const char* json, int len);
extern "C" void objcRTCSocketCallback(struct RTCSocketObserver* userdata, RTCSocket* rs, const char* message, int length, enum RTCSocketEvent event);
#endif
static RTCPeerConnection* API_LEVEL_1_CreatePeerConnection(const char* config, int zmfAudioPump, int isCaller, RTCSessionObserver* userdata,
    void(JSEP_CDECL_CALL*observer)(RTCSessionObserver*userdata, enum RTCSessionEvent event, const char* json, int len))
{
#ifdef __APPLE__
    if (!observer&&!userdata) observer = objcRTCSessionCallback;
#endif
    if (!config || (!observer&&!userdata)){
        LOG(LS_ERROR) << "JSEP_RTCPeerConnection invalid param";
        return nullptr;
    }
    auto factory = MainThread::Instance().AddRefFactory(zmfAudioPump != 0);
    if (!factory) return nullptr;

    webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
    webrtc::PeerConnectionFactoryInterface::Options rtc_options = static_cast<webrtc::PeerConnectionFactory*>(factory)->options();
    bool has_options = false;
    if (!ParseRTCConfiguration(config, rtc_config, rtc_options, has_options)){
        LOG(LS_ERROR) << "JSEP_RTCPeerConnection invalid config: " << config;
        MainThread::Instance().ReleaseFactory();
        return nullptr;
    }
    std::shared_ptr<SessionObserverProxy> proxy(new SessionObserverProxy(observer, userdata, isCaller != 0));
    if (has_options) factory->SetOptions(rtc_options);
    auto peer = factory->CreatePeerConnection(rtc_config, nullptr, nullptr, nullptr, proxy.get());
    if (peer)
        MainThread::Instance().AddSessionObserver(peer, proxy);
    else
        MainThread::Instance().ReleaseFactory();
    return (RTCPeerConnection*)peer.release();
}

static int API_LEVEL_1_AddIceCandidate(RTCPeerConnection* peer, const char* candidate)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface || !candidate){
        LOG(LS_ERROR) << "invalid AddIceCandidate param";
        return RTCSessionError_InvalidArgument;
    }
    std::unique_ptr<webrtc::IceCandidateInterface> ice(NewIceCandidate(candidate));
    if (!ice) {
        LOG(LS_ERROR) << "invalid candidate format";
        return RTCSessionError_InvalidArgument;
    }
    return iface->AddIceCandidate(ice.get()) ? 0 : RTCSessionError_InvalidOperation;
}

static int API_LEVEL_1_CreateOffer(RTCPeerConnection* peer, const char* constraints)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface){
        LOG(LS_ERROR) << "JSEP_CreateOffer invalid param";
        return RTCSessionError_InvalidArgument;
    }
    auto proxy = MainThread::Instance().GetSessionObserver(iface);
    if (!proxy) return RTCSessionError_InvalidArgument;

    return proxy->CreateOffer(iface, constraints);
}

static int API_LEVEL_1_CreateAnswer(RTCPeerConnection* peer, const char* constraints)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface){
        LOG(LS_ERROR) << "JSEP_CreateAnswer invalid param";
        return RTCSessionError_InvalidArgument;
    }
    auto proxy = MainThread::Instance().GetSessionObserver(iface);
    if (!proxy) return RTCSessionError_InvalidArgument;

    return proxy->CreateAnswer(iface, constraints);
}

static int API_LEVEL_1_SetRemoteDescription(RTCPeerConnection* peer, const char* sdp)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface || !sdp || !sdp[0]){
        LOG(LS_ERROR) << "SetRemoteDescription invalid param";
        return RTCSessionError_InvalidArgument;
    }
    auto proxy = MainThread::Instance().GetSessionObserver(iface);
    if (!proxy) return RTCSessionError_InvalidArgument;

    return proxy->SetRemoteDescription(iface, sdp);
}

static int API_LEVEL_1_SetLocalDescription(RTCPeerConnection* peer, const char* sdp)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface || !sdp || !sdp[0]){
        LOG(LS_ERROR) << "JSEP_SetLocalDescription invalid param";
        return RTCSessionError_InvalidArgument;
    }
    auto proxy = MainThread::Instance().GetSessionObserver(iface);
    if (!proxy) return RTCSessionError_InvalidArgument;

    return proxy->SetLocalDescription(iface, sdp);
}

static int API_LEVEL_1_PublishRemoteStream(RTCPeerConnection* peer, const char* streamId,
    int renderOrCapturerBits, int videoTrackMask)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface || !streamId || !streamId[0] || !IsZmfVideoSupport()) return RTCSessionError_InvalidArgument;
    if (!videoTrackMask) return 0;

    auto streams = iface->remote_streams();
    const size_t n = streams->count();
    for (size_t i=0; i<n;++i){
        auto stream = streams->at(i);
        if (stream->label() != streamId) continue;

        auto tracks = stream->GetVideoTracks();
        const int videoCount = (int)tracks.size();
        if (!videoCount) return 0;

        ZmfVideoRenderVector sources;
        char render_id[512];
        for (int i = 0; i<videoCount; ++i) {
            if (i > 0) sprintf(render_id, "%s%d", streamId, i);
            if ((videoTrackMask >> i) & 1)
                sources.emplace_back(
                    new ZmfVideoRender(tracks[i], 0/*ZmfVideoSourcePeer*/, (i > 0 ? render_id : streamId), ((renderOrCapturerBits >>i) & 1) != 0));
        }
        MainThread::Instance().InsertVideoSource(streamId, sources);
        return 0;
    }
    return RTCSessionError_InvalidOperation;
}

static void API_LEVEL_1_RemoveLocalStream (RTCPeerConnection* peer, const char* streamId)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface) return;

    auto streams = iface->local_streams();
    const size_t n = streams->count();
    for (size_t i=0; i<n;++i){
        auto stream = streams->at(i);
        const auto& label = stream->label();
        if (!streamId || !streamId[0])
            iface->RemoveStream(stream);
        else if (label == streamId)
            return iface->RemoveStream(stream);
    }
}

static RTCSessionObserver* API_LEVEL_1_ReleasePeerConnection(RTCPeerConnection* peer)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface) return nullptr;
    auto proxy = MainThread::Instance().RemoveSessionObserver(peer);
    RTCSessionObserver* userdata = nullptr;
    if (proxy) userdata = proxy->Close();
    iface->Close();
    {
        auto streams = iface->remote_streams();
        const size_t n = streams->count();
        for (size_t i=0; i<n;++i)
            MainThread::Instance().RemoveVideoSource(streams->at(i));
    }

    iface->Release();
    MainThread::Instance().ReleaseFactory();
    return userdata;
}

static bool ExpectAudioConstraints(Json::Value& jsonAudio, int& dtmfTrackIndex)
{
    if (jsonAudio.isBool())
        return jsonAudio.asBool();
    bool b;
    if (rtc::GetBoolFromJsonObject(jsonAudio, "DTMF", &b)){
        if (!b) dtmfTrackIndex = -1;
        jsonAudio.removeMember("DTMF");
    }
    return true;
}

static bool ExpectVideoConstraints(Json::Value& jsonVideo, std::vector<std::string>& captureIds, int& renderIndex)
{
    if (jsonVideo.isMember("zmfCapture")){
        const Json::Value& capture = jsonVideo["zmfCapture"];
        if (capture.isArray())
            rtc::JsonArrayToStringVector(capture, &captureIds);
        else if (capture.isString())
            captureIds.push_back(capture.asString());
        jsonVideo.removeMember("zmfCapture");
    }
    if (jsonVideo.isMember("zmfRender")){
        renderIndex = (int)captureIds.size();
        const Json::Value& render = jsonVideo["zmfRender"];
        std::vector<std::string> renderIds;
        if (render.isArray())
            rtc::JsonArrayToStringVector(render, &renderIds);
        else if (render.isString())
            renderIds.push_back(render.asString());
        for (const auto& r : renderIds)
            captureIds.push_back(r);
        jsonVideo.removeMember("zmfRender");
    }
    return true;
}

static int API_LEVEL_1_AddLocalStream(RTCPeerConnection* peer, 
    const char* streamId, int* audio, int *video,
    const char* constraints)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface || !streamId){
        LOG(LS_ERROR) << "JSEP_AddStream invalid param";
        return RTCSessionError_InvalidArgument;
    }
    {
        const auto& streams = iface->local_streams();
        for (int i = 0; i < (int)streams->count(); ++i) {
            if (streams->at(i)->label() == streamId){
                LOG(LS_ERROR) << "JSEP_AddStream duplicate streamId";
                return RTCSessionError_InvalidOperation;
            }
        }
    }
    auto stream = MainThread::Instance().GetFactory()->CreateLocalMediaStream(streamId);
    if (!stream) {
        LOG(LS_ERROR) << "CreateLocalMediaStream failed";
        return RTCSessionError_InvalidOperation;
    }
    int dtmfTrackIndex = 0;
    std::vector<std::string> captureIds;
    int renderIndex = std::numeric_limits<int>::max();
    Json::Value json;
    if (constraints && constraints[0]) {
        Json::Reader reader;
        if (!reader.parse(constraints, json, false) && !json.isObject()) {
            LOG(LS_ERROR) << "Received unknown constraints. " << constraints;
            return RTCSessionError_InvalidArgument;
        }
        if (audio && *audio && json.isMember("audio"))
            *audio = ExpectAudioConstraints(json["audio"], dtmfTrackIndex);
        if (video && *video && json.isMember("video"))
            *video = ExpectVideoConstraints(json["video"], captureIds, renderIndex);
    }
    char label[1024];
    if (audio && *audio) {
        MediaConstraints fake;
        if (json.isMember("audio")) {
            const Json::Value& audioJoin = json["audio"];
            fake.AddJson(audioJoin);
            bool value;
            bool AEC_AGC_NS[3];
            bool *bAEC=&AEC_AGC_NS[0], *bAGC=&AEC_AGC_NS[1], *bNS=&AEC_AGC_NS[2];
            if (rtc::GetBoolFromJsonObject(audioJoin, webrtc::MediaConstraintsInterface::kEchoCancellation, &value))
                *bAEC = !value;
            else
                bAEC = nullptr;
            if (rtc::GetBoolFromJsonObject(audioJoin, webrtc::MediaConstraintsInterface::kAutoGainControl, &value))
                *bAGC = !value;
            else
                bAGC = nullptr;
            if (rtc::GetBoolFromJsonObject(audioJoin, webrtc::MediaConstraintsInterface::kNoiseSuppression, &value))
                *bNS = !value;
            else
                bNS = nullptr;
            MainThread::Instance().EnableBuiltInDSP(bAEC, bAGC, bNS);
        }
        ::sprintf(label, "%s_audio", streamId);
        auto track = MainThread::Instance().GetFactory()->CreateAudioTrack(label, MainThread::Instance().GetFactory()->CreateAudioSource(&fake));
        if (!track || !stream->AddTrack(track)) {
            LOG(LS_WARNING) << "Add Audio Track failed";
            dtmfTrackIndex = -1;
            *audio = false;
        }
    }
    if (video && *video){
        if (!captureIds.size() || !IsZmfVideoSupport()) *video = false;
    }
    if (video && *video) {
        MediaConstraints fake(json["video"]);
        for (int i=0;i<(int)captureIds.size(); ++i){
            auto source = MainThread::Instance().GetFactory()->CreateVideoSource(new ZmfVideoCapturer(captureIds[i], renderIndex <= i), &fake);
            if (!source) {
                LOG(LS_ERROR) << "CreateVideoSource failed";
                return RTCSessionError_InvalidOperation;
            }
            ::sprintf(label, "%s_video_%d", streamId, i);
            auto track = MainThread::Instance().GetFactory()->CreateVideoTrack(label, source);
            if (!track || !stream->AddTrack(track)) {
                LOG(LS_ERROR) << "Add Video Track failed";
                return RTCSessionError_InvalidOperation;
            }
        }
    }
    if (!iface->AddStream(stream))
        return RTCSessionError_InvalidOperation;
    if (audio && *audio) {
        auto proxy = MainThread::Instance().GetSessionObserver(iface);
        if (!proxy) return RTCSessionError_InvalidOperation;

        if (dtmfTrackIndex >= 0) {
            auto sender = iface->CreateDtmfSender(stream->GetAudioTracks().at(dtmfTrackIndex));
            if (sender)
                proxy->SetDtmfSender(sender);
            else 
                LOG(LS_WARNING) << "CreateDtmfSender failed";
        }
    }
    return 0;
}

static int API_LEVEL_1_InsertDtmf (RTCPeerConnection* iface, const char* tones, int duration, int inter_tone_gap)
{
    if (!iface || !tones){
        LOG(LS_ERROR) << "InsertDtmf invalid param";
        return RTCSessionError_InvalidArgument;
    }
    auto proxy = MainThread::Instance().GetSessionObserver(iface);
    if (!proxy) return RTCSessionError_InvalidArgument;

    auto ch = proxy->GetDtmfSender();
    if (!ch) return RTCSessionError_InvalidOperation;

    return ch->InsertDtmf(tones, duration,inter_tone_gap) ? 0 : RTCSessionError_InvalidOperation;
}

static int API_LEVEL_1_SendMessage(RTCPeerConnection* iface,
    const char* channelId, const char* buffer, int length)
{
    if (!iface || !channelId || !buffer){
        LOG(LS_ERROR) << "JSEP_SendMessage invalid param";
        return RTCSessionError_InvalidArgument;
    }
    auto proxy = MainThread::Instance().GetSessionObserver(iface);
    if (!proxy) return RTCSessionError_InvalidArgument;

    auto ch = proxy->GetChannel(channelId);
    if (!ch) return RTCSessionError_InvalidArgument;

    if (length <= 0) {
        length = (int)strlen(buffer);
        if (!length) return 0;
    }
    return ch->Send(webrtc::DataBuffer(std::string(buffer, length))) ? 0 : RTCSessionError_InvalidOperation;
}

static int API_LEVEL_1_GetStats(RTCPeerConnection* peer,
    const char* statsType, int statsFlags)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface){
        LOG(LS_ERROR) << "JSEP_GetStats invalid param";
        return RTCSessionError_InvalidArgument;
    }
    const auto bDebug = (statsFlags&RTCStatsFlag_Debug) ? iface->kStatsOutputLevelDebug : iface->kStatsOutputLevelStandard;
    if (statsType && statsType[0] && (statsFlags&(RTCStatsFlag_Audio|RTCStatsFlag_Video)) != 0){
        webrtc::MediaStreamInterface* target = nullptr;
        auto streams = iface->remote_streams();
        size_t n = streams->count();
        for (size_t i=0; i<n && !target;++i){
            auto stream = streams->at(i);
            if (stream->label() == statsType)
                target = stream;
        }
        if (!target) {
            streams = iface->local_streams();
            n = streams->count();
            for (size_t i=0; i<n && !target;++i){
                auto stream = streams->at(i);
                if (stream->label() == statsType)
                    target = stream;
            }
        }
        if (!target) {
            LOG(LS_ERROR) << "JSEP_GetStats invalid streamId:" << statsType;
            return RTCSessionError_InvalidArgument;
        }
        if (statsFlags&RTCStatsFlag_Audio){
            for (auto& audio : target->GetAudioTracks()){
                if (!iface->GetStats(new rtc::RefCountedObject<GetStatsObserver>(iface, "ssrc"), audio, bDebug))
                    return RTCSessionError_InvalidOperation;
            }
        }
        if (statsFlags&RTCStatsFlag_Video){
            for (auto& video : target->GetVideoTracks()){
                if (!iface->GetStats(new rtc::RefCountedObject<GetStatsObserver>(iface, "ssrc"), video, bDebug))
                    return RTCSessionError_InvalidOperation;
            }
        }
    }
    else {
        if (!statsType) statsType = "";
        if (!iface->GetStats(new rtc::RefCountedObject<GetStatsObserver>(iface, statsType), nullptr, bDebug))
            return RTCSessionError_InvalidOperation;
    }
    return 0;
}
struct RTCSocket
{
    RTCSocketObserver* observer_; 
    SocketCallback  callback_;
    RTCSocket(RTCSocketObserver* observer, SocketCallback callback):observer_(observer), callback_(callback){}
    virtual int Send(const char* message, int length) = 0;
    virtual void Close() = 0;
    virtual ~RTCSocket(){}
};

struct WebSocket : public RTCSocket, public sigslot::has_slots<>, public rtc::MessageHandler {
    enum {
        MSG_CLOSE_SELF,
        MSG_FORCE_CLOSE,
    };
    AsyncWebSocket socket_;
    sigslot::signal1<WebSocket*> SignalCloseEvent;
    void OnConnectEvent(rtc::AsyncSocket* socket) {
        MainThread::Instance().Clear(this, MSG_FORCE_CLOSE);
        const std::string state = socket_.protocol().size() > 0 ? "open " + socket_.protocol() : "open";
        if (callback_)
            callback_(observer_, this, state.data(), state.size(), RTCSocketEvent_StateChange);
        else if (observer_)
            observer_->OnSocketStateChange(this, state.c_str());
    }
    void OnPacketEvent(const char* data, size_t length) {
        if (callback_)
            callback_(observer_, this, data, (int)length, RTCSocketEvent_Message);
        else if (observer_)
            observer_->OnSocketMessage(this, data, (int)length);
    }
    virtual void OnMessage(rtc::Message* msg) override {
        if (msg->message_id == MSG_FORCE_CLOSE)
            return socket_.ForceClose(-1);
        if (callback_)
            callback_(observer_, this, "closed", 6, RTCSocketEvent_StateChange);
        else if (observer_)
            observer_->OnSocketStateChange(this, "closed");
        else
            delete this;
    }
    void OnCloseEvent(rtc::AsyncSocket* socket, int err) {
        RTC_CHECK(socket_.readyState() == AsyncWebSocket::CLOSED);
        MainThread::Instance().Clear(this);
        MainThread::Instance().Post(RTC_FROM_HERE, this);
    }
    ~WebSocket(){
        MainThread::Instance().Clear(this);
        LOG(INFO) << "WebSocket destroyed";
    }
    WebSocket(const std::string& wsHead, const std::string& protocol, rtc::AsyncSocket* sock, rtc::SSLAdapter* ssl, RTCSocketObserver* observer, SocketCallback callback)
        : RTCSocket(observer, callback), socket_(wsHead, sock, ssl, protocol)
    {
        socket_.SignalConnectEvent.connect(this, &WebSocket::OnConnectEvent);
        socket_.SignalCloseEvent.connect(this, &WebSocket::OnCloseEvent);
        socket_.SignalPacketEvent.connect(this, &WebSocket::OnPacketEvent);
    }
    int Connect(const std::string& hostname, const rtc::SocketAddress& addr){
        return socket_.ConnectServer(hostname, addr);
    }
    int Accept(const std::string& hostname, int cmsDelay) {
        if (cmsDelay > 0)
            MainThread::Instance().PostDelayed(RTC_FROM_HERE, cmsDelay, this, MSG_FORCE_CLOSE);
        return socket_.AcceptClient(hostname);
    }
    virtual int Send(const char* message, int length) override {
        return socket_.Send(message, length);
    }
    virtual void Close() override { 
        callback_ = nullptr; 
        observer_ = nullptr;  
        SignalCloseEvent(this);
        if (socket_.readyState() == AsyncWebSocket::CLOSED)
            delete this;
        else
            socket_.Close();
    }
};

struct WebSocketServer : public RTCSocket, public sigslot::has_slots<> {
    std::unique_ptr<rtc::AsyncSocket> listener_;
    std::unique_ptr<rtc::SSLIdentity> identity_;
    std::set<WebSocket*> children_;
    Json::Value json_;
    void OnChildCloseEvent(WebSocket* ws) { children_.erase(ws); }
    WebSocketServer(rtc::AsyncSocket* sock, rtc::SSLIdentity* id, const Json::Value& json, RTCSocketObserver* observer, SocketCallback callback)
        : RTCSocket(observer, callback), listener_(sock), identity_(id), json_(json) {}
    bool Listen(int backlog, const rtc::SocketAddress& address) {
        listener_->SignalReadEvent.connect(this, &WebSocketServer::OnReadEvent);
        if (listener_->Bind(address) == 0 && listener_->Listen(backlog) == 0){
            return true;
        }
        LOG(LS_ERROR) << "WebSocketServer Listen failed: " << listener_->GetError();
        return false;
    }
    void OnReadEvent(rtc::AsyncSocket* socket) {
        RTC_DCHECK(socket == listener_.get());
        auto sock = listener_->Accept(nullptr);
        if (!sock)  return;
        rtc::SSLAdapter* sslsock = nullptr;
        if (identity_) {
            sslsock = rtc::SSLAdapter::Create(sock);
            if (!sslsock) {
                delete sock;
                LOG(LS_ERROR) << "WebSocketServer create SSLAdapter failed";
                return;
            }
            sslsock->SetIdentity(identity_->GetReference());
            std::string str;
            bool bval;
            int ival;
            if (rtc::GetBoolFromJsonObject(json_, "ignore_bad_cert", &bval))
                sslsock->set_ignore_bad_cert(bval);
            if (rtc::GetStringFromJsonObject(json_, "ca_certs", &str))
                sslsock->LoadVerifyLocation(str.c_str());
            if (rtc::GetIntFromJsonObject(json_, "cert_reqs", &ival))
                sslsock->SetVerifyMode(ival);
            sock = sslsock;
        }
        std::string protocols;
        rtc::GetStringFromJsonObject(json_, "protocols", &protocols);
        auto rs = new WebSocket("", protocols, sock, sslsock, observer_, callback_);
        std::string hostname;
        rtc::GetStringFromJsonObject(json_, "hostname", &hostname);
        int timeout;
        if (!rtc::GetIntFromJsonObject(json_, "timeout", &timeout))
            timeout = sslsock ? 0 : 2000;
        if (rs->Accept(hostname, timeout) != 0){
            delete rs;
            LOG(LS_ERROR) << "WebSocket Accept failed";
            return;
        }
        rs->SignalCloseEvent.connect(this, &WebSocketServer::OnChildCloseEvent);
        children_.insert(rs);
        const std::string state("new");
        if (callback_)
            callback_(observer_, rs, state.data(), state.size(), RTCSocketEvent_StateChange);
        else if (observer_)
            observer_->OnSocketStateChange(rs, state.c_str());
    }
    virtual int Send(const char* message, int length) override {
        for (auto ws : children_) ws->Send(message, length);
        return 0;
    }
    virtual void Close() override {
        listener_->Close();
        for (auto ws : children_) {
            ws->SignalCloseEvent.disconnect(this);
            ws->Close();
        }
        LOG(INFO) << "WebSocketServer destroyed";
        delete this;
    }
};

struct IceSocket : public RTCSocket, public sigslot::has_slots<>, public rtc::MessageHandler 
{
    std::unique_ptr<cricket::PortAllocator> port_;
    std::unique_ptr<cricket::IceTransportInternal> ice_;
    rtc::PacketOptions packetOptions_;
    std::string state_;
    void NotifyStateChange(const std::string& state) {
        if (state_ == state) return;
        state_ = state;
        if (callback_)
            callback_(observer_, this, state_.data(), (int)state_.size(), RTCSocketEvent_StateChange);
        else if (observer_)
            observer_->OnSocketStateChange(this, state_.c_str());
    }
    enum {
        MSG_CLOSE_SELF,
        MSG_SEND_STRING,
        MSG_ADD_CANDIDATE,
        MSG_SET_PARAMETERS,
    };

    void OnGatheringState(cricket::IceTransportInternal* ice){}
    void OnCandidateGathered(cricket::IceTransportInternal* ice, const cricket::Candidate& candidate){
        auto str = webrtc::SdpSerializeCandidate(candidate);
        if (callback_)
            callback_(observer_, this, str.data(), (int)str.size(), RTCSocketEvent_IceCandidate);
        else if (observer_)
            observer_->OnSocketIceCandidate(this, str.c_str());
    }
    void OnCandidatesRemoved(cricket::IceTransportInternal* ice, const cricket::Candidates& candidate){}
    void OnSelectedCandidatePairChanged(cricket::IceTransportInternal* ice, cricket::CandidatePairInterface* pair, int last_sent_packet_id, bool ready_to_send){
        if (pair && ready_to_send) {
            auto local_addr = webrtc::SdpSerializeCandidate(pair->local_candidate());
            auto remote_addr = webrtc::SdpSerializeCandidate(pair->remote_candidate());
            NotifyStateChange("connected local="+local_addr+";remote="+remote_addr);
        }
    }
    void OnRoleConflict(cricket::IceTransportInternal*ice){}
    void OnStateChanged(cricket::IceTransportInternal* ice){
        static const char* kIceStateNames[] = {
            "new",
            "checking",
            "completed",
            "failed",
        };
        auto state = static_cast<int>(ice->GetState());
        NotifyStateChange(kIceStateNames[state]);
    }
    void OnDestroyed(cricket::IceTransportInternal* ice) { NotifyStateChange("closed"); }
    void OnReadPacket(rtc::PacketTransportInternal* pt, const char* data, size_t length, const rtc::PacketTime& time, int flags){
        if (callback_)
            callback_(observer_, this, data, (int)length, RTCSocketEvent_Message);
        else if (observer_)
            observer_->OnSocketMessage(this, data, (int)length);
    }
    virtual void OnMessage(rtc::Message* msg) override {
        switch(msg->message_id) {
        case MSG_SEND_STRING:{
            auto str = (std::string*)msg->pdata;
            ice_->SendPacket(str->data(), str->size(), packetOptions_);
            delete str;
        } return;
        case MSG_ADD_CANDIDATE:{
            auto candidate = (cricket::Candidate*)msg->pdata;
            ice_->AddRemoteCandidate(*candidate);
            delete candidate;
        } return;
        case MSG_SET_PARAMETERS:{
            auto * param = (cricket::IceParameters*)msg->pdata;
            ice_->SetRemoteIceParameters(*param);
            delete param;
        } return;
        case MSG_CLOSE_SELF: {
            ice_.reset();
            port_.reset();
            delete this;
        } return;
        default:
            RTC_NOTREACHED();
        }
    }
    IceSocket(RTCSocketObserver* observer, SocketCallback callback)
        :RTCSocket(observer,callback)
    {}
    int AddRemoteCandidate(const cricket::Candidate& candidate) {
        if (MainThread::Instance().NetThread().IsCurrent())
            ice_->AddRemoteCandidate(candidate);
        else
            MainThread::Instance().NetThread().Post(RTC_FROM_HERE, this, MSG_ADD_CANDIDATE,
                (rtc::MessageData*)new cricket::Candidate(candidate));
        return 0;
    }
    int SetRemoteIceParameters(const cricket::IceParameters& ice_param){
        if (MainThread::Instance().NetThread().IsCurrent())
            ice_->SetRemoteIceParameters(ice_param);
        else
            MainThread::Instance().NetThread().Post(RTC_FROM_HERE, this, MSG_SET_PARAMETERS,
                (rtc::MessageData*)new cricket::IceParameters(ice_param));
        return 0;
    }
    virtual int Send(const char* message, int length) override {
        if (MainThread::Instance().NetThread().IsCurrent())
            return ice_->SendPacket(message, length, packetOptions_);

        MainThread::Instance().NetThread().Post(RTC_FROM_HERE, this, MSG_SEND_STRING,
            (rtc::MessageData*)new std::string(message, length));
        return length;
    }
    virtual void Close() override {
        if (MainThread::Instance().NetThread().IsCurrent()){
            ice_.reset();
            port_.reset();
            delete this;
        }
        else
            MainThread::Instance().NetThread().Post(RTC_FROM_HERE, this, MSG_CLOSE_SELF);
    }
};
static int API_LEVEL_1_SendSocket(RTCSocket* rs, const char* message, int length)
{
    if (!rs || !message) return RTCSessionError_InvalidArgument;
    if (length <= 0) {
        length = (int)strlen(message);
        if (!length) return 0;
    }
    return rs->Send(message, length);
}
static RTCSocketObserver* API_LEVEL_1_CloseSocket(RTCSocket* rs)
{
    if (!rs) return nullptr;
    auto ret = rs->observer_;
    rs->Close();
    return ret;
}
static RTCSocket* API_LEVEL_1_CreateWebSocketServer(const char* wsURL, const char* rtcWebSocketInit, RTCSocketObserver* userdata,
    void (JSEP_CDECL_CALL *observer)(RTCSocketObserver* userdata, RTCSocket* rs, const char* message, int length, enum RTCSocketEvent event))
{
    if (!wsURL) {
        LOG(LS_ERROR) << "CreateWebSocket invalid wsURL";
        return nullptr;
    }
#ifdef __APPLE__
    if (!observer&&!userdata) observer = objcRTCSocketCallback;
#endif
    if (!observer && !userdata) {
        LOG(LS_ERROR) << "CreateWebSocket invalid observer or callback";
        return nullptr;
    }
    Json::Value json;
    if (rtcWebSocketInit && rtcWebSocketInit[0]){
        Json::Reader reader;
        if (!reader.parse(rtcWebSocketInit, json, false)) {
            LOG(LS_ERROR) << "CreateWebSocket invalid RtcWebSocketInit: " << rtcWebSocketInit;
            return nullptr;
        }
    }

    int port=80; bool ssl=false;
    std::string host, path;
    if (!AsyncWebSocket::VerifyURL(wsURL, ssl, host, port, path)){
        LOG(LS_ERROR) << "CreateWebSocket invalid wsURL";
        return nullptr;
    }
    rtc::SocketAddress addr;
    addr.SetPort(port);
    if (host != "*" && host != ""){
        rtc::IPAddress ip;
        if (!rtc::IPFromString(host, &ip)){
            LOG(LS_ERROR) << "invalid ip address: " << host;
            return nullptr;
        }
        addr.SetIP(ip);
    }
    else {
        bool bval;
        if (rtc::GetBoolFromJsonObject(json, "ipv6", &bval) && bval)
            addr.SetIP(rtc::IPAddress(in6addr_any));
        else
            addr.SetIP(rtc::IPAddress(INADDR_ANY));
    }
    rtc::SSLIdentity* id = nullptr;
    if (ssl){
        std::string certfile, keyfile;
        if (rtc::GetStringFromJsonObject(json, "keyfile", &keyfile)
            && rtc::GetStringFromJsonObject(json, "certfile", &certfile)) {
            if (ReadFileString(keyfile, &keyfile) && ReadFileString(certfile, &certfile))
                id = rtc::SSLIdentity::FromPEMStrings(keyfile, certfile);
            if (!id) {
                LOG(LS_ERROR) << "CreateWebSocket create SSLIdentity failed";
                return nullptr;
            }
        }
    }
    auto ss = MainThread::Instance().socketserver();
    auto sock = ss->CreateAsyncSocket(addr.family(), SOCK_STREAM);
    if (!sock) {
        LOG(LS_ERROR) << "CreateWebSocket create SOCK_STREAM failed";
        return nullptr;
    }
    auto* ws = new WebSocketServer(sock, id, json, userdata, observer);
    int backlog=100;
    rtc::GetIntFromJsonObject(json, "backlog", &backlog);
    if (!ws->Listen(backlog, addr)){
        delete ws;
        return nullptr;
    }
    ss->WakeUp();
    return ws;
}
static RTCSocket* API_LEVEL_1_CreateWebSocket(const char* wsURL, const char* rtcWebSocketInit, RTCSocketObserver* userdata,
    void (JSEP_CDECL_CALL *observer)(RTCSocketObserver* userdata, RTCSocket* rs, const char* message, int length, enum RTCSocketEvent event))
{
    if (!wsURL) {
        LOG(LS_ERROR) << "CreateWebSocket invalid wsURL";
        return nullptr;
    }
#ifdef __APPLE__
    if (!observer&&!userdata) observer = objcRTCSocketCallback;
#endif
    if (!observer && !userdata) {
        LOG(LS_ERROR) << "CreateWebSocket invalid observer or callback";
        return nullptr;
    }
    Json::Value json;
    if (rtcWebSocketInit && rtcWebSocketInit[0]){
        Json::Reader reader;
        if (!reader.parse(rtcWebSocketInit, json, false)) {
            LOG(LS_ERROR) << "CreateWebSocket invalid RtcWebSocketInit: " << rtcWebSocketInit;
            return nullptr;
        }
    }
    int port=80; bool ssl=false;
    std::string host, path;
    if (!AsyncWebSocket::VerifyURL(wsURL, ssl, host, port, path)){
        LOG(LS_ERROR) << "CreateWebSocket invalid wsURL";
        return nullptr;
    }
    rtc::IPAddress ip;
    if (!rtc::IPFromString(host, &ip)){
        LOG(LS_ERROR) << "invalid ip address: " << host;
        return nullptr;
    }
    auto ss = MainThread::Instance().socketserver();
    auto sock = ss->CreateAsyncSocket(ip.family(), SOCK_STREAM);
    if (!sock) {
        LOG(LS_ERROR) << "CreateWebSocket create SOCK_STREAM failed";
        return nullptr;
    }
    std::string str;
    std::ostringstream oss;
    oss << "GET " << path << " HTTP/1.1\r\n";
    oss << "Host: " << host;  if (port != 80) oss << ":" << port; oss << "\r\n";

    if (rtc::GetStringFromJsonObject(json, "origin", &str))
        oss << "Origin: " << str << "\r\n";
    if (rtc::GetStringFromJsonObject(json, "protocols", &str))
        oss << "Sec-WebSocket-Protocol: " << str << "\r\n";

    rtc::SSLAdapter* sslsock = nullptr;
    if (ssl) {
        sslsock = rtc::SSLAdapter::Create(sock);
        if (!sslsock) {
            delete sock;
            LOG(LS_ERROR) << "CreateWebSocket create SSLAdapter failed";
            return nullptr;
        }
        std::string keyfile, certfile;
        if (rtc::GetStringFromJsonObject(json, "keyfile", &keyfile)
            && rtc::GetStringFromJsonObject(json, "certfile", &certfile)) {
            rtc::SSLIdentity* id = nullptr;
            if (ReadFileString(keyfile, &keyfile) && ReadFileString(certfile, &certfile))
                id = rtc::SSLIdentity::FromPEMStrings(keyfile, certfile);
            if (!id) {
                delete sslsock;
                LOG(LS_ERROR) << "CreateWebSocket create SSLIdentity failed";
                return nullptr;
            }
            sslsock->SetIdentity(id);
        }
        bool bval;
        int ival;
        if (rtc::GetBoolFromJsonObject(json, "ignore_bad_cert", &bval))
            sslsock->set_ignore_bad_cert(bval);
        if (rtc::GetStringFromJsonObject(json, "ca_certs", &str))
            sslsock->LoadVerifyLocation(str.c_str());
        if (rtc::GetIntFromJsonObject(json, "cert_reqs", &ival))
            sslsock->SetVerifyMode(ival);
        if (rtc::GetStringFromJsonObject(json, "hostname", &str))
            host = str;
        sock = sslsock;
    }
    auto* ws = new WebSocket(oss.str(), "", sock, sslsock, userdata, observer);
    if (ws->Connect(host, rtc::SocketAddress(ip, port)) < 0){
        delete ws;
        LOG(LS_ERROR) << "CreateWebSocket Connect failed";
        return nullptr;
    }
    ss->WakeUp();
    return ws;
}
static uint32_t ConvertIceTransportTypeToCandidateFilter(webrtc::PeerConnectionInterface::IceTransportsType type)
{
    switch (type) {
    case webrtc::PeerConnectionInterface::kNone:
        return cricket::CF_NONE;
    case webrtc::PeerConnectionInterface::kRelay:
        return cricket::CF_RELAY;
    case webrtc::PeerConnectionInterface::kNoHost:
        return (cricket::CF_ALL & ~cricket::CF_HOST);
    case webrtc::PeerConnectionInterface::kAll:
        return cricket::CF_ALL;
    default:
        RTC_NOTREACHED();
    }
    return cricket::CF_NONE;
}
static cricket::IceConfig ParseIceConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& config)
{
    cricket::ContinualGatheringPolicy gathering_policy;
    // TODO(honghaiz): Add the third continual gathering policy in
    // PeerConnectionInterface and map it to GATHER_CONTINUALLY_AND_RECOVER.
    switch (config.continual_gathering_policy) {
    case webrtc::PeerConnectionInterface::GATHER_ONCE:
        gathering_policy = cricket::GATHER_ONCE;
        break;
    case webrtc::PeerConnectionInterface::GATHER_CONTINUALLY:
        gathering_policy = cricket::GATHER_CONTINUALLY;
        break;
    default:
        RTC_NOTREACHED();
        gathering_policy = cricket::GATHER_ONCE;
    }
    cricket::IceConfig ice_config;
    ice_config.receiving_timeout = config.ice_connection_receiving_timeout;
    ice_config.prioritize_most_likely_candidate_pairs =
        config.prioritize_most_likely_ice_candidate_pairs;
    ice_config.backup_connection_ping_interval =
        config.ice_backup_candidate_pair_ping_interval;
    ice_config.continual_gathering_policy = gathering_policy;
    ice_config.presume_writable_when_fully_relayed =
        config.presume_writable_when_fully_relayed;
    ice_config.ice_check_min_interval = config.ice_check_min_interval;
    return ice_config;
}
static RTCSocket* API_LEVEL_1_CreateIceSocket(const char* config, const char* params, int isCaller, RTCSocketObserver* userdata,
    void (JSEP_CDECL_CALL *observer)(RTCSocketObserver* userdata, RTCSocket* rs, const char* message, int length, enum RTCSocketEvent event))
{
    cricket::IceParameters ice_param;
    webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
    webrtc::PeerConnectionFactoryInterface::Options rtc_options;
    bool has_options = false;
    if (!ParseRTCConfiguration(config, rtc_config, rtc_options, has_options)){
        LOG(LS_ERROR) << "CreateIceSocket invalid config: " << config;
        return nullptr;
    }

    cricket::ServerAddresses stun_servers;
    std::vector<cricket::RelayServerConfig> turn_servers;
    if (webrtc::ParseIceServers(rtc_config.servers, &stun_servers, &turn_servers) != webrtc::RTCErrorType::NONE) {
        LOG(LS_ERROR) << "CreateIceSocket invalid RTCConfiguration";
        return nullptr;
    }

    if (!ParseRTCIceParameters(params, ice_param)){
        LOG(LS_ERROR) << "CreateIceSocket invalid RTCIceParameters";
        return nullptr;
    }

    std::unique_ptr<cricket::PortAllocator> port(MainThread::Instance().CreatePortAllocator());
    port->Initialize();

    // To handle both internal and externally created port allocator, we will
    // enable BUNDLE here.
    int portallocator_flags = port->flags();
    portallocator_flags |= cricket::PORTALLOCATOR_ENABLE_SHARED_SOCKET |
        cricket::PORTALLOCATOR_ENABLE_IPV6 |
        cricket::PORTALLOCATOR_ENABLE_IPV6_ON_WIFI;
    // If the disable-IPv6 flag was specified, we'll not override it
    // by experiment.
    if (rtc_config.disable_ipv6) {
        portallocator_flags &= ~(cricket::PORTALLOCATOR_ENABLE_IPV6);
    } /*else if (webrtc::field_trial::FindFullName("WebRTC-IPv6Default").find("Disabled") == 0) {
        portallocator_flags &= ~(cricket::PORTALLOCATOR_ENABLE_IPV6);
    }*/

    if (rtc_config.disable_ipv6_on_wifi) {
        portallocator_flags &= ~(cricket::PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);
        LOG(LS_INFO) << "IPv6 candidates on Wi-Fi are disabled.";
    }

    if (rtc_config.tcp_candidate_policy == webrtc::PeerConnectionInterface::kTcpCandidatePolicyDisabled) {
        portallocator_flags |= cricket::PORTALLOCATOR_DISABLE_TCP;
        LOG(LS_INFO) << "TCP candidates are disabled.";
    }

    if (rtc_config.candidate_network_policy == webrtc::PeerConnectionInterface::kCandidateNetworkPolicyLowCost) {
        portallocator_flags |= cricket::PORTALLOCATOR_DISABLE_COSTLY_NETWORKS;
        LOG(LS_INFO) << "Do not gather candidates on high-cost networks";
    }

    port->set_flags(portallocator_flags);
    // No step delay is used while allocating ports.
    port->set_step_delay(cricket::kMinimumStepDelay);
    port->set_candidate_filter(ConvertIceTransportTypeToCandidateFilter(rtc_config.type));
    if (has_options) {
        port->SetNetworkIgnoreMask(rtc_options.network_ignore_mask);
    }

    //  rik.gong: add port range
    port->SetPortRange(rtc_config.min_port, rtc_config.max_port);

    // Call this last since it may create pooled allocator sessions using the
    // properties set above.
    if (!port->SetConfiguration(stun_servers, turn_servers, rtc_config.ice_candidate_pool_size, rtc_config.prune_turn_ports)){
        return nullptr;
    }

    //cricket::IceParameters ice_param(rtc::CreateRandomString(cricket::ICE_UFRAG_LENGTH),
    //    rtc::CreateRandomString(cricket::ICE_PWD_LENGTH), false);
    auto is = new IceSocket(userdata, observer);
    auto ice_config = ParseIceConfig(rtc_config);
    auto p2p = MainThread::Instance().NetThreadCall([&]{
        auto p2p = new cricket::P2PTransportChannel("", 0, port.get());
        p2p->SignalCandidateGathered.connect(is, &IceSocket::OnCandidateGathered);
        p2p->SignalStateChanged.connect(is, &IceSocket::OnStateChanged);
        p2p->SignalDestroyed.connect(is, &IceSocket::OnDestroyed);
        p2p->SignalReadPacket.connect(is, &IceSocket::OnReadPacket);
        p2p->SignalSelectedCandidatePairChanged.connect(is, &IceSocket::OnSelectedCandidatePairChanged);
        p2p->SetIceRole(isCaller != 0 ? cricket::ICEROLE_CONTROLLING : cricket::ICEROLE_CONTROLLED);
        p2p->SetIceConfig(ice_config);
        p2p->SetIceParameters(ice_param);
        p2p->MaybeStartGathering();
        return p2p;
    });
    is->ice_.reset((cricket::IceTransportInternal*)p2p);
    is->port_ = std::move(port);
    return is;
}
static int API_LEVEL_1_AddSocketIceCandidate(RTCSocket*rs, const char* message)
{
    if (!rs || !message || !message[0]) {
        LOG(LS_ERROR) << "AddSocketIceCandidate invalid param";
        return RTCSessionError_InvalidArgument;
    }
    webrtc::SdpParseError error;
    cricket::Candidate candidate;
    if (!webrtc::SdpDeserializeCandidate("", message, &candidate, &error))
        return RTCSessionError_InvalidArgument;
    return static_cast<IceSocket*>(rs)->AddRemoteCandidate(candidate);
}
static int API_LEVEL_1_SetSocketIceParameters(RTCSocket* rs, const char* rtcIceParameters)
{
    if (!rs || !rtcIceParameters) return RTCSessionError_InvalidArgument;
    cricket::IceParameters ice_param;
    if (!ParseRTCIceParameters(rtcIceParameters, ice_param))
        return RTCSessionError_InvalidArgument;
    return static_cast<IceSocket*>(rs)->SetRemoteIceParameters(ice_param);
}
static int API_LEVEL_1_LogRtcEvent(RTCPeerConnection* peer, const char* filename, int max_size_mb)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface){
        LOG(LS_ERROR) << "JSEP_LogRtcEvent invalid param";
        return RTCSessionError_InvalidArgument;
    }
    if (filename && filename[0]) {
        auto file = rtc::CreatePlatformFile(std::string(filename));
        if (!file ) {
            LOG(LS_ERROR) << "JSEP_LogRtcEvent invalid param";
            return RTCSessionError_InvalidArgument;
        }
        return iface->StartRtcEventLog(file, int64_t(max_size_mb)*1024*1024) ? 0 : -1;
    }
    iface->StopRtcEventLog();
    return 0;
}
static int API_LEVEL_1_DumpAudioProcessing(const char* filename, int max_size_mb)
{
    auto factory = MainThread::Instance().GetFactory();
    if (!factory) {
        LOG(LS_ERROR) << "JSEP_DumpAudioProcessing invalid state";
        return RTCSessionError_InvalidOperation;
    }
    if (filename && filename[0]) {
        auto file = rtc::CreatePlatformFile(std::string(filename));
        if (!file ) {
            LOG(LS_ERROR) << "JSEP_DumpAudioProcessing invalid param";
            return RTCSessionError_InvalidArgument;
        }
        return factory->StartAecDump(file, int64_t(max_size_mb)*1024*1024) ? 0 : -1;
    }
    factory->StopAecDump();
    return 0;
}
static int API_LEVEL_1_SetBitrate(RTCPeerConnection* peer, int current_bitrate_bps, int max_bitrate_bps, int min_bitrate_bps)
{
    auto iface = (webrtc::PeerConnectionInterface*)peer;
    if (!iface || (current_bitrate_bps <= 0 && max_bitrate_bps <=0 && min_bitrate_bps <= 0)){
        LOG(LS_ERROR) << "SetBitrate invalid param";
        return RTCSessionError_InvalidArgument;
    }
    webrtc::PeerConnectionInterface::BitrateParameters bitrate;
    if (current_bitrate_bps > 0) bitrate.current_bitrate_bps.emplace(current_bitrate_bps);
    if (max_bitrate_bps > 0) bitrate.max_bitrate_bps.emplace(max_bitrate_bps);
    if (min_bitrate_bps > 0) bitrate.min_bitrate_bps.emplace(min_bitrate_bps);
    auto ret = iface->SetBitrate(bitrate);
    return ret.ok() ? 0 : -1;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * JSON parser. Contains an array of token blocks available. Also stores
 * the string being parsed now and current position in that string
 */
typedef struct {
    /*< private >*/
    int pos; /* offset in the JSON string */
    int toknext; /* next token to allocate */
    int toksuper1; /* superior token node, e.g parent object or array, start from 1, 0 is no parent */
} json_parser;
/**
 * Allocates a fresh unused token from the token pull.
 */
static JsonValue *json_alloc_token(json_parser *parser, JsonValue *tokens, int num_tokens)
{
    JsonValue *tok;
    if (parser->toknext >= num_tokens) {
        return nullptr;
    }
    tok = &tokens[parser->toknext++];
    tok->json = nullptr;
    tok->n_json = -1;
    tok->n_child = 0;
    tok->parent = -1;
    return tok;
}

/**
 * Fills token type and boundaries.
 */
static void json_fill_token(JsonValue *token, enum JsonForm type, int start, int end, const char* js)
{
    token->type = type;
    token->json = js + start;
    token->n_json = end-start;
    token->n_child = 0;
}

/**
 * Fills next available token with JSON primitive.
 */
static int json_parse_primitive(json_parser *parser, const char *js,
                                int len, JsonValue *tokens, int num_tokens)
{
    JsonValue *token;
    int start;

    start = parser->pos;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        switch (js[parser->pos]) {
#ifndef JSON_STRICT
            /* In strict mode primitive must be followed by "," or "}" or "]" */
        case ':':
#endif
        case '\t' : case '\r' : case '\n' : case ' ' :
        case ','  : case ']'  : case '}' :
            goto found;
        }
        if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
            parser->pos = start;
            return JsonError_Invalid;
        }
    }
#ifdef JSON_STRICT
    /* In strict mode primitive must be followed by a comma/object/array */
    parser->pos = start;
    return JsonError_Partial;
#endif

found:
    if (!tokens) {
        parser->pos--;
        return 0;
    }
    token = json_alloc_token(parser, tokens, num_tokens);
    if (!token) {
        parser->pos = start;
        return JsonError_Insufficient;
    }
    json_fill_token(token, JsonForm_Primitive, start, parser->pos, js);
    token->parent = parser->toksuper1-1;
    parser->pos--;
    return 0;
}

/**
 * Filsl next token with JSON string.
 */
static int json_parse_string(json_parser *parser, const char *js,
                             int len, JsonValue *tokens, int num_tokens)
{
    JsonValue *token;

    int start = parser->pos;

    parser->pos++;

    /* Skip starting quote */
    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        char c = js[parser->pos];

        /* Quote: end of string */
        if (c == '\"') {
            if (!tokens) {
                return 0;
            }
            token = json_alloc_token(parser, tokens, num_tokens);
            if (!token) {
                parser->pos = start;
                return JsonError_Insufficient;
            }
            json_fill_token(token, JsonForm_String, start+1, parser->pos, js);
            token->parent = parser->toksuper1-1;
            return 0;
        }

        /* Backslash: Quoted symbol expected */
        if (c == '\\' && parser->pos + 1 < len) {
            int i;
            parser->pos++;
            switch (js[parser->pos]) {
                /* Allowed escaped symbols */
            case '\"': case '/' : case '\\' : case 'b' :
            case 'f' : case 'r' : case 'n'  : case 't' :
                break;
                /* Allows escaped symbol \uXXXX */
            case 'u':
                parser->pos++;
                for (i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++) {
                    /* If it isn't a hex character we have an error */
                    if(!((js[parser->pos] >= 48 && js[parser->pos] <= 57) || /* 0-9 */
                         (js[parser->pos] >= 65 && js[parser->pos] <= 70) || /* A-F */
                         (js[parser->pos] >= 97 && js[parser->pos] <= 102))) { /* a-f */
                        parser->pos = start;
                        return JsonError_Invalid;
                    }
                    parser->pos++;
                }
                parser->pos--;
                break;
                /* Unexpected symbol */
            default:
                parser->pos = start;
                return JsonError_Invalid;
            }
        }
    }
    parser->pos = start;
    return JsonError_Partial;
}
static int API_LEVEL_1_ParseJson(const char *js, int len, JsonParser *p, JsonValue* tokens, int num_tokens)
{
    int r;
    int i;
    JsonValue *token;
    json_parser oncep = {0};
    json_parser *parser = p ?  (json_parser*)p : &oncep;
    int count = parser->toknext;
    if (count == 0 && tokens)
        memset(tokens, 0, sizeof(JsonValue)*num_tokens);
    if (!js) return 0;
    if (len <= 0) len = strlen(js);

    for (; parser->pos < len; parser->pos++) {
        char c;
        int type;

        c = js[parser->pos];
        switch (c) {
        case '{': case '[':
            count++;
            if (!tokens) {
                break;
            }
            token = json_alloc_token(parser, tokens, num_tokens);
            if (!token)
                return JsonError_Insufficient;
            if (parser->toksuper1 != 0) {
                tokens[parser->toksuper1-1].n_child++;
                token->parent = parser->toksuper1-1;
            }
            token->type = (c == '{' ? JsonForm_Object : JsonForm_Array);
            token->json = js + parser->pos;
            parser->toksuper1 = parser->toknext;
            break;
        case '}': case ']':
            if (!tokens) break;
            type = (c == '}' ? JsonForm_Object : JsonForm_Array);
            if (parser->toknext < 1) return JsonError_Invalid;
            token = &tokens[parser->toknext - 1];
            for (;;) {
                if (token->json && token->n_json == -1) {
                    if (token->type != type) return JsonError_Invalid;
                    token->n_json = js + parser->pos + 1 - token->json;
                    parser->toksuper1 = token->parent + 1;
                    break;
                }
                if (token->parent == -1) {
                    break;
                }
                token = &tokens[token->parent];
            }
            break;
        case '\"':
            r = json_parse_string(parser, js, len, tokens, num_tokens);
            if (r < 0) return r;
            count++;
            if (parser->toksuper1 != 0 && tokens)
                tokens[parser->toksuper1-1].n_child++;
            break;
        case '\t' : case '\r' : case '\n' : case ' ':
            break;
        case ':':
            parser->toksuper1 = parser->toknext;
            break;
        case ',':
            if (tokens && parser->toksuper1 &&
                tokens[parser->toksuper1-1].type != JsonForm_Array &&
                tokens[parser->toksuper1-1].type != JsonForm_Object) {
                parser->toksuper1 = tokens[parser->toksuper1-1].parent+1;
            }
            break;
#ifdef JSON_STRICT
            /* In strict mode primitives are: numbers and booleans */
        case '-': case '0': case '1' : case '2': case '3' : case '4':
        case '5': case '6': case '7' : case '8': case '9':
        case 't': case 'f': case 'n' :
            /* And they must not be keys of the object */
            if (tokens) {
                JsonValue *t = &tokens[parser->toksuper1-1];
                if (t->type == JsonForm_Object ||
                    (t->type == JsonForm_String && t->n_child != 0)) {
                    return JsonError_Invalid;
                }
            }
#else
            /* In non-strict mode every unquoted value is a primitive */
        default:
#endif
            r = json_parse_primitive(parser, js, len, tokens, num_tokens);
            if (r < 0) return r;
            count++;
            if (parser->toksuper1 != 0 && tokens)
                tokens[parser->toksuper1-1].n_child++;
            break;

#ifdef JSON_STRICT
            /* Unexpected char in strict mode */
        default:
            return JsonError_Invalid;
#endif
        }
    }

    for (i = parser->toknext - 1; i >= 0; i--) {
        /* Unmatched opened object or array */
        if (tokens[i].json && tokens[i].n_json == -1) {
            return JsonError_Partial;
        }
    }
    if(tokens && count != parser->toknext)
        return JsonError_Invalid;
    return count;
}
static const JsonValue* json_next(const JsonValue *t)
{
    const int size = t->n_child;
    const int type = t->type;
    ++t;
    for (int i=0;i<size;++i) {
        if (type == JsonForm_Object) ++t;
        t = json_next(t);
    }
    return t;
}
static int API_LEVEL_1_CompareJson(const JsonValue* token, const char* str, int n_str)
{
    if (n_str <= 0) n_str = strlen(str);
    if (token->type != JsonForm_String) {
        if (token->n_json != n_str)
            return token->n_json - n_str;
        else
            return strncmp(token->json, str, n_str);
    }
    int i=0,j=0;
    for (; i<token->n_json && j<n_str; ++i, ++j) {
        char ch = token->json[i];
        if (token->json[i] == '\\') { ++i;
            switch(token->json[i]) {
            case '\\':ch = '\\'; break;
            case '"': ch = '\"'; break;
            case 'f': ch = '\f'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case '0': ch = '\0'; break;
            }
        }
        if (ch != str[j]) return ch - str[j];
    }
    return token->n_json - i + j - n_str;
}
static const JsonValue* API_LEVEL_1_ChildJson(const JsonValue* jsonValue, int index, const char* key, int n_key)
{
    static JsonValue NULL_JSON = {(enum JsonForm)0, "", 0 , 0, -1};
    if (!jsonValue || jsonValue->n_child <= 0) return &NULL_JSON;
    if (key) {
        if (jsonValue->type != JsonForm_Object) return &NULL_JSON;
        if (n_key <=0) n_key = strlen(key);
        index = jsonValue->n_child;
        for (++jsonValue; index > 0; --index) {
            if (!API_LEVEL_1_CompareJson(jsonValue, key, n_key))
                return jsonValue+1;
            jsonValue = json_next(jsonValue);
        }
    }
    else {
        if (jsonValue->type != JsonForm_Array && jsonValue->type != JsonForm_Object)
            return &NULL_JSON;
        while (index < 0) index += jsonValue->n_child;
        while (index > jsonValue->n_child) index -= jsonValue->n_child;
        for (++jsonValue; index > 0; --index)
            jsonValue = json_next(jsonValue);
        return jsonValue;
    }
    return &NULL_JSON;
}
static int API_LEVEL_1_UnescapeJson(const JsonValue* token, char* buf)
{
    if (token->type != JsonForm_String) {
WHOLE:
        memcpy(buf, token->json, token->n_json);
        buf[token->n_json] = '\0';
        return token->n_json;
    }
    else {
        int i=0, j=0;
        while (i<token->n_json && token->json[i]!='\\') ++i;
        if (i == token->n_json) goto WHOLE;
        if (i > 0) {
            memcpy(buf, token->json, i);
            j=i;
        }
        do {
            char ch = token->json[i++];
            if (ch == '\\') {
                switch(token->json[i++]) {
                case '\\':ch = '\\'; break;
                case '"': ch = '\"'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case '0': ch = '\0'; break;
                }
            }
            buf[j++] = ch;
        } while ( i < token->n_json );
        buf[j] = '\0';
        return j;
    }
}
static const char* API_LEVEL_1_LastErrorDescription() { return _lastErrorDescription.c_str(); }
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static lua_State* _L = nullptr;
static void InitOnce() {
#if defined(_WIN32)
    if (IsDebuggerPresent()) rtc::LogMessage::LogToDebug(rtc::LS_VERBOSE);
#endif
    const char* level = getenv("JSEP_LOG_TO_DEBUG");
    if (level && level[0]) {
        if (!strcmp(level, "INFO"))
            rtc::LogMessage::LogToDebug(rtc::LS_INFO);
        else if (!strcmp(level, "WARN"))
            rtc::LogMessage::LogToDebug(rtc::LS_WARNING);
        else if (!strcmp(level, "ERROR"))
            rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
        else if (!strcmp(level, "VERBOSE"))
            rtc::LogMessage::LogToDebug(rtc::LS_VERBOSE);
        else if (!strcmp(level, "SENSITIVE"))
            rtc::LogMessage::LogToDebug(rtc::LS_SENSITIVE);
    }
    const char* dirPath = getenv("JSEP_LOG_DIR_PATH");
    if (dirPath && dirPath[0]) {
        static rtc::CallSessionFileRotatingLogSink sink(dirPath, 10 * 1024 * 1024);
        if (sink.Init())
            rtc::LogMessage::AddLogToStream(&sink, rtc::LS_SENSITIVE);
        else
            LOG(LS_ERROR) << "Invalid JSEP_LOG_DIR_PATH: " << dirPath;
    }
    if (!LocateZmfAudio())
        LOG(LS_ERROR) << "attach ZmfAudio failed";
    if (!LocateZmfVideo())
        LOG(LS_ERROR) << "attach ZmfVideo failed";
    if (_L)
        LOG(LS_INFO) << "LUA_REGISTRYINDEX = " << LUA_REGISTRYINDEX;
}
struct JsepApiContext {
    int apiLevel;
    JSEP_API api;
    JsepApiContext():apiLevel(0){}
};
const JSEP_API* JsepAPI(int apiLevel)
{
    static std::once_flag _init;
    std::call_once(_init, InitOnce);
    if (apiLevel  < 1|| apiLevel > _BUILD_JSEP_API_LEVEL__) {
        LOG(LS_ERROR) <<"Invalid JSEP_API_LEVEL: " << apiLevel;
        return nullptr;
    }
    thread_local static JsepApiContext ctx;
    JSEP_API *api = &ctx.api;
    if (ctx.apiLevel != apiLevel) {
        ctx.apiLevel = apiLevel;
        api->AddIceCandidate = API_LEVEL_1_AddIceCandidate;
        api->AddLocalStream = API_LEVEL_1_AddLocalStream;
        api->CloseDataChannel = API_LEVEL_1_CloseDataChannel;
        api->CreatePeerConnection = API_LEVEL_1_CreatePeerConnection;
        api->CreateDataChannel = API_LEVEL_1_CreateDataChannel;
        api->CreateAnswer = API_LEVEL_1_CreateAnswer;
        api->CreateOffer = API_LEVEL_1_CreateOffer;
        api->GetStats = API_LEVEL_1_GetStats;
        api->InsertDtmf = API_LEVEL_1_InsertDtmf;
        api->PublishRemoteStream = API_LEVEL_1_PublishRemoteStream;
        api->ReleasePeerConnection = API_LEVEL_1_ReleasePeerConnection;
        api->RemoveLocalStream = API_LEVEL_1_RemoveLocalStream;
        api->SendMessage = API_LEVEL_1_SendMessage;
        api->SetLocalDescription = API_LEVEL_1_SetLocalDescription;
        api->SetRemoteDescription = API_LEVEL_1_SetRemoteDescription;
        api->LogRtcEvent = API_LEVEL_1_LogRtcEvent;
        api->SetBitrate = API_LEVEL_1_SetBitrate;
        api->DumpAudioProcessing = API_LEVEL_1_DumpAudioProcessing;
        api->LastErrorDescription = API_LEVEL_1_LastErrorDescription;

        api->CreateWebSocket = API_LEVEL_1_CreateWebSocket;
        api->CreateWebSocketServer = API_LEVEL_1_CreateWebSocketServer;
        api->CreateIceSocket = API_LEVEL_1_CreateIceSocket;
        api->CloseSocket = API_LEVEL_1_CloseSocket;
        api->SendSocket = API_LEVEL_1_SendSocket;
        api->AddSocketIceCandidate = API_LEVEL_1_AddSocketIceCandidate;
        api->SetSocketIceParameters = API_LEVEL_1_SetSocketIceParameters;

        api->ParseJson = API_LEVEL_1_ParseJson;
        api->ChildJson = API_LEVEL_1_ChildJson;
        api->CompareJson = API_LEVEL_1_CompareJson;
        api->UnescapeJson = API_LEVEL_1_UnescapeJson;
    }
    return api;
}
static void l_ws_observer(RTCSocketObserver* userdata, RTCSocket* rtcsocket, const char* message, int length, enum RTCSocketEvent event)
{
    lua_rawgeti(_L, LUA_REGISTRYINDEX, (size_t)userdata);
    lua_pushlightuserdata(_L, rtcsocket);
    lua_pushlstring(_L, message, length);
    lua_pushinteger(_L, event);
    if (lua_pcall(_L, 3, 0, 0) != 0){
        const char* err = lua_tolstring(_L, -1, nullptr);
        fprintf(stderr, "Lua:%s\n", err);
        LOG(LS_ERROR) << err;
        lua_pop(_L, 1);
    }
}
static void l_pc_observer(RTCSessionObserver*userdata, enum RTCSessionEvent event, const char* json, int length)
{
    lua_rawgeti(_L, LUA_REGISTRYINDEX, (size_t)userdata);
    lua_pushinteger(_L, event);
    lua_pushlstring(_L, json, length);
    if (lua_pcall(_L, 2, 0, 0) != 0){
        const char* err = lua_tolstring(_L, -1, nullptr);
        fprintf(stderr, "Lua:%s\n", err);
        LOG(LS_ERROR) << err;
        lua_pop(_L, 1);
    }
}
static int l_pc(lua_State *L)
{
    const char* rtcConfiguration = lua_tolstring(L, 1, nullptr);
    bool zmfAudioPump = lua_toboolean(L, 2) != 0;
    bool isCaller = lua_toboolean(L, 3) != 0;
    if (!rtcConfiguration || !lua_isfunction(L, -1)) return 0;

    int funref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (funref == LUA_REFNIL){
        LOG(LS_ERROR) <<"luaL_ref failed";
        return 0;
    }

    auto pc = JSEP_RTCPeerConnection(rtcConfiguration, zmfAudioPump, isCaller,  (RTCSessionObserver*)(size_t)funref, l_pc_observer);
    if (!pc) {
        luaL_unref(L, LUA_REGISTRYINDEX, funref);
        return 0;
    }
    lua_pushlightuserdata(L, pc);
    return 1;
}
static int l_addlocalstream(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* streamId = lua_tolstring(L, 2, nullptr);
    RTCBoolean bAudio = lua_toboolean(L, 3) != 0;
    RTCBoolean bVideo = lua_toboolean(L, 4) != 0;
    const char* constraints = lua_tolstring(L, 5, nullptr);
    lua_pushinteger(L, JSEP_AddLocalStream(pc, streamId, &bAudio, &bVideo, constraints));
    lua_pushinteger(L, bAudio);
    lua_pushinteger(L, bVideo);
    return 3;
}
static int l_removelocalstream(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* streamId = lua_tolstring(L, 2, nullptr);
    JSEP_RemoveLocalStream(pc, streamId);
    return 0;
}
static int l_publishremotestream(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* streamId = lua_tolstring(L, 2, nullptr);
    int renderOrCapturerBits = lua_tointegerx(L, 3, nullptr);
    int videoTrackMask = lua_tointegerx(L, 4, nullptr);
    lua_pushinteger(L, JSEP_PublishRemoteStream(pc, streamId, renderOrCapturerBits, videoTrackMask));
    return 1;
}
static int l_addicecandidate(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* rtcIceCandidate = lua_tolstring(L, 2, nullptr);
    lua_pushinteger(L, JSEP_AddIceCandidate(pc, rtcIceCandidate));
    return 1;
}
static int l_createoffer(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* rtcOfferOptions = lua_tolstring(L, 2, nullptr);
    lua_pushinteger(L,JSEP_CreateOffer(pc, rtcOfferOptions));
    return 1;
}
static int l_setlocaldescription(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* rtcSessionDescription = lua_tolstring(L, 2, nullptr);
    lua_pushinteger(L,JSEP_SetLocalDescription(pc, rtcSessionDescription));
    return 1;
}
static int l_setremotedescription(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* rtcSessionDescription = lua_tolstring(L, 2, nullptr);
    lua_pushinteger(L,JSEP_SetRemoteDescription(pc, rtcSessionDescription));
    return 1;
}
static int l_createanswer(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* rtcAnswerOptions = lua_tolstring(L, 2, nullptr);
    lua_pushinteger(L,JSEP_CreateAnswer(pc, rtcAnswerOptions));
    return 1;
}
static int l_createdatachannel(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* channelId = lua_tolstring(L, 2, nullptr);
    const char* rtcDataChannelInit = lua_tolstring(L, 3, nullptr);
    lua_pushinteger(L,JSEP_CreateDataChannel(pc, channelId, rtcDataChannelInit));
    return 1;
}
static int l_closedatachannel(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* channelId = lua_tolstring(L, 2, nullptr);
    JSEP_CloseDataChannel(pc, channelId);
    return 0;
}
static int l_sendmessage(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* channelId = lua_tolstring(L, 2, nullptr);
    size_t len;
    const char* str = lua_tolstring(L, 3, &len);
    lua_pushinteger(L,JSEP_SendMessage(pc, channelId, str, len));
    return 1;
}
static int l_insertdtmf(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* tones = lua_tolstring(L, 2, nullptr);
    int duration_ms = lua_tointegerx(L, 3, nullptr);
    int inter_tone_gap = lua_tointegerx(L, 4, nullptr);
    lua_pushinteger(L,JSEP_InsertDtmf(pc, tones, duration_ms, inter_tone_gap));
    return 1;
}
static int l_setbitrate(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    int current_bitrate_bps = lua_tointegerx(L, 2, nullptr);
    int max_bitrate_bps = lua_tointegerx(L, 3, nullptr);
    int min_bitrate_bps = lua_tointegerx(L, 4, nullptr);
    lua_pushinteger(L,JSEP_SetBitrate(pc,current_bitrate_bps,max_bitrate_bps,min_bitrate_bps));
    return 1;
}
static int l_lasterrordescription(lua_State *L)
{
    lua_pushlstring(L, _lastErrorDescription.c_str(), _lastErrorDescription.size());
    return 1;
}
static int l_getstats(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* statsType = lua_tolstring(L, 2, nullptr);
    int statsFlags = lua_tointegerx(L, 3, nullptr);
    lua_pushinteger(L,JSEP_GetStats(pc, statsType, statsFlags));
    return 1;
}
static int l_logrtcevent(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    const char* filename = lua_tolstring(L, 2, nullptr);
    int max_size_mb = lua_tointegerx(L, 3, nullptr);
    lua_pushinteger(L,JSEP_LogRtcEvent(pc, filename, max_size_mb));
    return 1;
}
static int l_dumpaudioprocessing(lua_State *L)
{
    const char* filename = lua_tolstring(L, 1, nullptr);
    int max_size_mb = lua_tointegerx(L, 2, nullptr);
    lua_pushinteger(L,JSEP_DumpAudioProcessing(filename, max_size_mb));
    return 1;
}
static int l_release(lua_State *L)
{
    RTCPeerConnection* pc = (RTCPeerConnection*)lua_touserdata(L, 1);
    if (pc) luaL_unref(L, LUA_REGISTRYINDEX, (size_t)JSEP_Release(pc));
    return 0;
}

static int l_ws_connect(lua_State *L)
{
    const char* wsURL = lua_tolstring(L, 1, nullptr);
    const char* rtcWebSocketInit = lua_tolstring(L, 2, nullptr);
    if (!wsURL || !rtcWebSocketInit || !lua_isfunction(L, -1)) return 0;

    int funref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (funref == LUA_REFNIL){
        LOG(LS_ERROR) <<"luaL_ref failed";
        return 0;
    }

    auto ws = WebSocket_Connect(wsURL, rtcWebSocketInit, (RTCSocketObserver*)(size_t)funref, l_ws_observer);
    if (!ws) {
        luaL_unref(L, LUA_REGISTRYINDEX, funref);
        return 0;
    }
    lua_pushlightuserdata(L, ws);
    return 1;
}
static int l_ws_listen(lua_State *L)
{
    const char* wsURL = lua_tolstring(L, 1, nullptr);
    const char* rtcWebSocketInit = lua_tolstring(L, 2, nullptr);
    if (!wsURL || !rtcWebSocketInit || !lua_isfunction(L, -1)) return 0;

    int funref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (funref == LUA_REFNIL) {
        LOG(LS_ERROR) <<"luaL_ref failed";
        return 0;
    }

    auto ws = WebSocket_Listen(wsURL, rtcWebSocketInit, (RTCSocketObserver*)(size_t)funref, l_ws_observer);
    if (!ws) {
        luaL_unref(L, LUA_REGISTRYINDEX, funref);
        return 0;
    }
    lua_pushlightuserdata(L, ws);
    return 1;
}
static int l_rs_close(lua_State *L)
{
    RTCSocket* rs = (RTCSocket*)lua_touserdata(L, 1);
    if (rs) luaL_unref(L, LUA_REGISTRYINDEX, (size_t)RTCSocket_Close(rs));
    return 0;
}
static int l_rs_send(lua_State *L)
{
    size_t len;
    RTCSocket* rs = (RTCSocket*)lua_touserdata(L, 1);
    if (!rs) return 0;
    const char* str = lua_tolstring(L, 2, &len);
    lua_pushinteger(L, RTCSocket_Send(rs, str, len));
    return 1;
}
static int l_mainloop(lua_State* L)
{
    int funref = LUA_REFNIL;
    std::function<bool(int)> func;
    if (lua_isfunction(L, -1)) {
        funref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (funref == LUA_REFNIL){
            LOG(LS_ERROR) <<"luaL_ref failed";
            return 0;
        }
        func = [=](int lIdleCount){
            lua_rawgeti(_L, LUA_REGISTRYINDEX, funref);
            lua_pushinteger(L, lIdleCount);
            bool done = false;
            if (lua_pcall(L, 1, 1, 0) != 0){
                const char* err = lua_tolstring(_L, -1, nullptr);
                fprintf(stderr, "Lua:%s\n", err);
                LOG(LS_ERROR) << err;
            }
            else
                done = lua_toboolean(L, -1) != 0;
            lua_pop(_L, 1);
            return done;//return TRUE if more idle processing
        };
    }
    MainThread::Instance().MainLoop(func);
    if (funref != LUA_REFNIL) luaL_unref(L, LUA_REGISTRYINDEX, funref);
    return 0;
}
static int l_mainquit(lua_State* L)
{
    MainThread::Instance().Quit();
    return 0;
}
static int l_terminate(lua_State* L)
{
    MainThread::Instance().Terminate();
    return 0;
}
typedef std::vector<JsonValue>::const_iterator json_t;
static json_t l_obj(lua_State *L, json_t t, json_t e);
static json_t l_map(lua_State *L, json_t t, json_t e, int size)
{
    lua_createtable(L, 0, size);
    for(int i=0; i<size && t < e; ++i){
        lua_pushlstring(L, t->json, t->n_json);
        t = l_obj(L, t+1, e);
        lua_settable (L, -3);
    }
    return t;
}
static json_t l_vec(lua_State *L, json_t t, json_t e, int size)
{
    lua_createtable(L, size, 0);
    for(int i=0;i<size && t < e;++i) {
        lua_pushinteger(L, i+1);/* Array starting with 1 */
        t = l_obj(L, t, e);
        lua_settable (L, -3);
    }
    return t;
}
static json_t l_obj(lua_State *L, json_t t, json_t e)
{
    switch(t->type) {
    case JsonForm_Object:
        return l_map(L, t+1, e, t->n_child);
    case JsonForm_Array:
        return l_vec(L, t+1, e, t->n_child);
    case JsonForm_Primitive:
    case JsonForm_String:
        lua_pushlstring(L, t->json, t->n_json);
        return t+1;
    default:
        return e;
    }
}
static int
l_table(lua_State *L, const std::vector<JsonValue>& vals)
{
    if (vals.empty()) return 0;
    auto t = vals.begin();
    auto e = vals.end(); 
    if (t->type != JsonForm_Object && t->n_child <= 1) {
        auto pColon = strchr(t->json, ':');
        auto pComma = strchr(t->json, ',');
        if (!pColon) pColon = t->json+t->n_json;
        if (!pComma) pComma = t->json+t->n_json;
        if (pColon < pComma)
            t = l_map(L, t, e, (int)vals.size());
        else 
            t = l_vec(L, t, e, (int)vals.size());
    }
    else 
        t = l_obj(L, t, e);
    if (t != e) return 0;
    return 1;
}

static int l_json2table(lua_State *L)
{
    return l_table(L, JsonValue::parse(lua_tolstring(L, 1, nullptr)));
}

template<typename T> struct luaL_Var {
    const char* name;
    T           value;
};
JSEP_PUBLIC int luaopen_jsep (lua_State *L)
{
    if (!LocateLua(L))
        return 0;
    MainThread::Instance().Stop();
    MainThread::Instance().Restart();//reset IsQuitting() if the thread is being restarted
    const char* regidx = getenv("LUA_REGISTRYINDEX");
    if (regidx && regidx[0]){
        char* end_ptr;
        errno = 0;
        long val = strtol(regidx, &end_ptr, 10);  // NOLINT
        bool ret = (end_ptr != regidx && *end_ptr == '\0' && !errno && val >= INT_MIN && val <= INT_MAX);
        if (!ret)
            LOG(LS_ERROR) << "Invalid LUA_REGISTRYINDEX: " << regidx;
        else
            LUA_REGISTRYINDEX = val;
    }
    const struct luaL_Var<const char*> str_vars[] =  {
        {"_VERSION", "0.0.1"},
        {"JSEP_AUDIO_PUMP", JSEP_AUDIO_PUMP},
        {"_COPYRIGHT", "Copyright (C) 2018 rik.gong"},
        {"_DESCRIPTION","WebRtc Native"},
        {nullptr, 0}
    };
    const struct luaL_Var<int> int_vars[] =  {
        {"JSEP_API_LEVEL", _BUILD_JSEP_API_LEVEL__},
        {"RTCSocketEvent_Message", RTCSocketEvent_Message},
        {"RTCSocketEvent_StateChange", RTCSocketEvent_StateChange},
        {"RTCSocketEvent_IceCandidate", RTCSocketEvent_IceCandidate},

        {"RTCStatsFlag_Debug", RTCStatsFlag_Debug},
        {"RTCStatsFlag_Audio", RTCStatsFlag_Audio},
        {"RTCStatsFlag_Video", RTCStatsFlag_Video},

        {"RTCSessionEvent_RenegotiationNeeded", RTCSessionEvent_RenegotiationNeeded},
        {"RTCSessionEvent_CreateDescriptionSuccess", RTCSessionEvent_CreateDescriptionSuccess},
        {"RTCSessionEvent_CreateDescriptionFailure", RTCSessionEvent_CreateDescriptionFailure},
        {"RTCSessionEvent_SetDescriptionSuccess", RTCSessionEvent_SetDescriptionSuccess},
        {"RTCSessionEvent_SetDescriptionFailure", RTCSessionEvent_SetDescriptionFailure},
        {"RTCSessionEvent_IceCandidate", RTCSessionEvent_IceCandidate},
        {"RTCSessionEvent_IceConnectionStateChange", RTCSessionEvent_IceConnectionStateChange},
        {"RTCSessionEvent_SignalingChange", RTCSessionEvent_SignalingChange},
        {"RTCSessionEvent_AddRemoteStream", RTCSessionEvent_AddRemoteStream},
        {"RTCSessionEvent_RemoveRemoteStream", RTCSessionEvent_RemoveRemoteStream},
        {"RTCSessionEvent_ToneChange", RTCSessionEvent_ToneChange},
        {"RTCSessionEvent_StatsReport", RTCSessionEvent_StatsReport},
        {"RTCSessionEvent_DataChannelOpen", RTCSessionEvent_DataChannelOpen},
        {"RTCSessionEvent_DataChannelMessage", RTCSessionEvent_DataChannelMessage},
        {"RTCSessionEvent_DataChannelClose", RTCSessionEvent_DataChannelClose},

        {nullptr, 0}
    };
    const struct luaL_Reg funcs[] = {
        {"MainLoop", l_mainloop},
        {"MainQuit", l_mainquit},
        {"Terminate", l_terminate},
        {"Json2Table", l_json2table},

        {"WebSocket_Connect", l_ws_connect},
        {"WebSocket_Listen", l_ws_listen},
        {"RTCSocket_Close", l_rs_close},
        {"RTCSocket_Send", l_rs_send},


        {"JSEP_AddLocalStream", l_addlocalstream},
        {"JSEP_RemoveLocalStream", l_removelocalstream},
        {"JSEP_PublishRemoteStream", l_publishremotestream},

        {"JSEP_CreateDataChannel", l_createdatachannel},
        {"JSEP_CloseDataChannel", l_closedatachannel},
        {"JSEP_SendMessage", l_sendmessage},

        {"JSEP_RTCPeerConnection", l_pc},
        {"JSEP_Release", l_release},

        {"JSEP_CreateOffer", l_createoffer},
        {"JSEP_CreateAnswer", l_createanswer},
        {"JSEP_AddIceCandidate", l_addicecandidate},
        {"JSEP_SetLocalDescription", l_setlocaldescription},
        {"JSEP_SetRemoteDescription", l_setremotedescription},

        {"JSEP_InsertDtmf", l_insertdtmf},
        {"JSEP_SetBitrate", l_setbitrate},
        {"JSEP_LastErrorDescription", l_lasterrordescription},

        {"JSEP_GetStats", l_getstats},
        {"JSEP_LogRtcEvent", l_logrtcevent},
        {"JSEP_DumpAudioProcessing", l_dumpaudioprocessing},

        {nullptr, nullptr},
    };
    lua_createtable(L, 0, 0);

    for (int i=0; str_vars[i].name; ++i) {
        lua_pushlstring (L, str_vars[i].name, strlen(str_vars[i].name));
        lua_pushlstring (L, str_vars[i].value, strlen(str_vars[i].value));
        lua_settable(L, -3);
    }

    for (int i=0; int_vars[i].name; ++i) {
        lua_pushlstring (L, int_vars[i].name, strlen(int_vars[i].name));
        lua_pushinteger (L, int_vars[i].value);
        lua_settable(L, -3);
    }

    for (int i=0; funcs[i].name; ++i) {
        lua_pushlstring (L, funcs[i].name, strlen(funcs[i].name));
        lua_pushcclosure(L, funcs[i].func, 0);
        lua_settable(L, -3);
    }
    _L = L;
    return 1;
}
