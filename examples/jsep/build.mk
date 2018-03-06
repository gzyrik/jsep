LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := jsep
LOCAL_CPP_EXTENSION := .cpp
LOCAL_SRC_FILES := jsep.cpp

LOCAL_CFLAGS := $(MY_WEBRTC_COMMON_DEFS) -DJSEP_EXPORT

LOCAL_C_INCLUDES := $(MY_WEBRTC_ROOT_PATH)/.. \
	$(MY_WEBRTC_ROOT_PATH)/../third_party/jsoncpp/source/include

LOCAL_SHARED_LIBRARIES = zmf_shared
ifdef WEBRTC_ANDROID
LOCAL_LDLIBS := -llog
endif

LOCAL_STATIC_LIBRARIES = libjrtc \
						 libjingle \
						 rtc_p2p \
						 jsoncpp \
						 usrsctp \
						 srtp \
						 boringssl

ifeq ($(NDK_PLATFORM_PREFIX),darwin)
LOCAL_LDFLAGS += -framework CoreMedia\
				 -framework CoreVideo\
				 -framework AVFoundation\
				 -framework AudioToolbox\
				 -framework CoreAudioKit\
				 -framework CoreAudio\
				 -framework AppKit\
				 -framework OpenGL\
				 -framework GLKit \
				 -framework IOKit \
				 -framework Carbon \
				 -framework Security \
				 -framework SystemConfiguration
endif
#gn gen out/mac --args='is_debug=false clang_base_path="/usr" clang_use_chrome_plugins=false'  --filters=//webrtc/examples:jsep
#gn gen out/ios --args='is_debug=false use_xcode_clang=true target_os="ios" target_cpu="arm64"'  --filters=//webrtc/examples:jsep
include $(BUILD_SHARED_LIBRARY)
