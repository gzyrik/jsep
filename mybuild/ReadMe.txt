Ubuntu
    sudo ./install-build-deps.sh --no-chromeos-fonts
BUILD STEP
    - export PATH=$PWD/tools/depot_tools:$PATH
    - gn gen out/linux --args='is_debug=false rtc_use_h264=true'  --filters=//webrtc/examples:jsep 
    - ninja -C out/linux webrtc/examples:jsep
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
    - gn gen out/win32 --args="is_debug=false rtc_use_h264=true target_cpu=\"x86\"" --ide=vs --filters=//webrtc/examples:jsep
    - ninja -C out/win32 webrtc/examples:jsep
======================================================================================================
MacOS
    - export PATH=$PWD/tools/depot_tools:$PATH
    - gn gen out/mac --args='is_debug=false rtc_use_h264=true clang_base_path="/usr" clang_use_chrome_plugins=false' --filters=//webrtc/examples:jsep
    - ninja -C out/mac webrtc/examples:jsep
      IF        error: unknown warning option '-Wno-unused-lambda-capture'
      REMOVE    config/compiler/BUILD.gn: "-Wno-unused-lambda-capture"
ios
    - gn gen out/ios --args='is_debug=false rtc_use_h264=true use_xcode_clang=true target_os="ios" target_cpu="arm64"'  --filters=//webrtc/examples:jsep
    - ninja -C out/ios webrtc/examples:jsep
