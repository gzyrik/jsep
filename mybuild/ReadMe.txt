Ubuntu
    sudo ./install-build-deps.sh --no-chromeos-fonts
BUILD STEP
    - export PATH=$PWD/tools/depot_tools:$PATH
    - gn gen out/linux --args='is_debug=false rtc_use_h264=true ffmpeg_branding="Chrome"'  --filters=//webrtc/examples:jsep 
    - ninja -C out/linux webrtc/examples:jsep
------------------------------------------------------------------------------------------------------
Android
    - gn gen out/android --args='is_debug=false target_os="android" target_cpu="arm"'  --filters=//webrtc/examples:jsep
======================================================================================================
Window
System requirements
    - A 64-bit Intel machine with at least 8GB of RAM. More than 16GB is highly recommended.
    - At least 100GB of free disk space on an NTFS-formatted hard drive. FAT32 will not work, as some of the Git packfiles are larger than 4GB.
    - Visual Studio 2015 Update 3, see below (no other version is supported).
    - Windows 7 or newer.

Visual Studio 2015 Update 3 + 
    Use the Custom Install option and select:
    - Visual C++, which will select three sub-categories including MFC
    - Universal Windows Apps Development Tools > Tools (1.4.1) and Windows 10 SDK (10.0.14393)

BUILD STEP

    - set PATH=%CD%\tools\depot_tools_win;%PATH%
    - set DEPOT_TOOLS_WIN_TOOLCHAIN=0
    - gn gen out/win32 --args="is_debug=false rtc_use_h264=true target_cpu=\"x86\" ffmpeg_branding=\"Chrome\"" --ide=vs --filters=//webrtc/examples:jsep
    - ninja -C out/win32 webrtc/examples:jsep
======================================================================================================
MacOS
EDIT
    error: unknown warning option '-Wno-unused-lambda-capture'
    - build/config/compiler/BUILD.gn:
         # TODO(hans): https://crbug.com/681136
         #"-Wno-unused-lambda-capture",

BUILD STEP
    - export PATH=$PWD/tools/depot_tools:$PATH
    - gn gen out/mac --args='is_debug=false rtc_use_h264=true clang_base_path="/usr" clang_use_chrome_plugins=false ffmpeg_branding="Chrome"' --filters=//webrtc/examples:jsep
    - ninja -C out/mac webrtc/examples:jsep
------------------------------------------------------------------------------------------------------
iOS
EDIT
    - build/toolchain/mac/BUILD.gn:
         if (use_xcode_clang) { #(toolchain_args.current_os == "ios" && use_xcode_clang) {
             prefix = ""
BUILD STEP
    - gn gen out/ios --args='is_debug=false clang_base_path="/user" use_xcode_clang=true target_os="ios" target_cpu="arm64" ios_enable_code_signing=false'  --filters=//webrtc/examples:jsep_framework
    - ninja -C out/ios webrtc/examples:jsep_framework



