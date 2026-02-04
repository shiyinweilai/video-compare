cmake_minimum_required(VERSION 3.10)
project(video-compare VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# GCC/Clang编译器选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3 -Ofast -D__STDC_CONSTANT_MACROS")
# GCC/Clang警告选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-deprecated -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdisabled-optimization -Wctor-dtor-privacy -Woverloaded-virtual")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused -Wno-missing-field-initializers")

# Windows 下避免需要 SDL2main
if(WIN32)
    add_definitions(-DSDL_MAIN_HANDLED)
endif()

# 可选：屏蔽部分输入相关功能，尽量减少对旧键盘布局API的依赖（可按需开启）
option(VC_DISABLE_INPUT_FEATURES "Disable input-heavy features to avoid old macOS keyboard API deps" OFF)
if(VC_DISABLE_INPUT_FEATURES)
    add_definitions(-DVC_DISABLE_INPUT_FEATURES=1)
endif()

# 查找所有源文件
file(GLOB SOURCES "*.cpp")

# 设置包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 添加ffmpeg库路径和包含目录
if(WIN32)
    # Windows: 直接使用传入的安装目录，避免EXISTS检查导致交叉路径失效
    include_directories(${FFMPEG_INSTALL_DIR}/include)
    link_directories(${FFMPEG_INSTALL_DIR}/lib)
    message(STATUS "[Win] Using ffmpeg from ${FFMPEG_INSTALL_DIR}")
else()
    if(EXISTS ${FFMPEG_INSTALL_DIR})
        link_directories(${FFMPEG_INSTALL_DIR}/lib)
        link_directories(${FFMPEG_INSTALL_DIR}/bin)
        include_directories(${FFMPEG_INSTALL_DIR}/include)
        message(STATUS "Using ffmpeg from ${FFMPEG_INSTALL_DIR}")
    else()
        message(WARNING "FFMPEG_INSTALL_DIR not found: ${FFMPEG_INSTALL_DIR}")
    endif()
endif()

# 添加SDL2_ttf包含目录和库路径
if(WIN32)
    include_directories(${SDL_TTF_INSTALL_DIR}/include)
    link_directories(${SDL_TTF_INSTALL_DIR}/lib)
    message(STATUS "[Win] Using SDL2_ttf from ${SDL_TTF_INSTALL_DIR}")
else()
    if(EXISTS ${SDL_TTF_INSTALL_DIR})
        link_directories(${SDL_TTF_INSTALL_DIR}/lib)
        include_directories(${SDL_TTF_INSTALL_DIR}/include)
        message(STATUS "Using SDL2_ttf from ${SDL_TTF_INSTALL_DIR}")
    else()
        message(WARNING "SDL_TTF_INSTALL_DIR not found: ${SDL_TTF_INSTALL_DIR}")
    endif()
endif()

# 添加SDL2包含目录
if(WIN32)
    include_directories(${SDL_INSTALL_DIR}/include)
    include_directories(${SDL_INSTALL_DIR}/include/SDL2)
    link_directories(${SDL_INSTALL_DIR}/lib)
    message(STATUS "[Win] Using SDL2 from ${SDL_INSTALL_DIR}")
else()
    if(EXISTS ${SDL_INSTALL_DIR})
        include_directories(${SDL_INSTALL_DIR}/include)
        include_directories(${SDL_INSTALL_DIR}/include/SDL2)
        link_directories(${SDL_INSTALL_DIR}/lib)
        message(STATUS "Using SDL2 from ${SDL_INSTALL_DIR}")
    else()
        message(WARNING "SDL2 include directory not found: ${SDL_INSTALL_DIR}")
    endif()
endif()

if(WIN32)
    set(SDL2_LDFLAGS SDL2)
    set(SDL2_FOUND TRUE)
    message(STATUS "Using direct SDL2 linking on Windows platform (MinGW static)")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 sdl2)
    include_directories(${SDL2_INCLUDE_DIRS})
endif()

set(FFMPEG_LIBS avformat avcodec avfilter avutil swscale swresample)

if(WIN32)
    set(LIBS ${FFMPEG_LIBS} ${SDL2_LDFLAGS} SDL2_ttf)
    list(APPEND LIBS ws2_32)
    list(APPEND LIBS bcrypt)
    list(APPEND LIBS secur32)
    list(APPEND LIBS crypt32)
    list(APPEND LIBS ncrypt)
    list(APPEND LIBS winmm imm32)
    set(TTF_LIBDIR ${SDL_TTF_INSTALL_DIR}/lib)
    set(_TTF_STATIC_CANDIDATES
        freetype
        harfbuzz
        bz2
        brotlidec
        brotlicommon
        png
        png16
        z
    )
    foreach(_lib ${_TTF_STATIC_CANDIDATES})
        if(EXISTS ${TTF_LIBDIR}/lib${_lib}.a)
            list(APPEND LIBS ${TTF_LIBDIR}/lib${_lib}.a)
        endif()
    endforeach()

    list(APPEND LIBS gdi32 setupapi version ole32 shell32 user32 advapi32 uuid)
else()
    set(LIBS ${FFMPEG_LIBS} ${SDL2_LDFLAGS} -lSDL2_ttf pthread)
endif()

# macOS特定框架和库
if(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(COREVIDEO_LIBRARY CoreVideo)
    find_library(VIDEOTOOLBOX_LIBRARY VideoToolbox)
    find_library(COREMEDIA_LIBRARY CoreMedia)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(SECURITY_LIBRARY Security)
    find_library(ZLIB_LIBRARY z)
    find_library(BZ2_LIBRARY bz2)
    find_library(ICONV_LIBRARY iconv)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    find_library(METAL_LIBRARY Metal)
    find_library(COREIMAGE_LIBRARY CoreImage)
    find_library(OPENGL_LIBRARY OpenGL)
    find_library(QUARTZCORE_LIBRARY QuartzCore)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices)
    # 新增：解决 SDL2 静态链接在 macOS 下的未定义符号
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(FORCEFEEDBACK_LIBRARY ForceFeedback)
    find_library(COREHAPTICS_LIBRARY CoreHaptics)
    find_library(FOUNDATION_LIBRARY Foundation)
    find_library(GAMECONTROLLER_LIBRARY GameController)
    # 老式输入API（TIS*/LMGetKbdType/KBGetLayoutType）通过 Carbon/ApplicationServices 满足
    find_library(CARBON_LIBRARY Carbon)
    # 移除对 HIToolbox 的显式查找，以兼容新 SDK
    # find_library(HITOOLBOX_LIBRARY HIToolbox)

    # 精确的回退逻辑：为框架使用正确的 -framework 名称；为库使用 -l 名称
    if(NOT COCOA_LIBRARY OR COCOA_LIBRARY MATCHES ".*-NOTFOUND")
        set(COCOA_LIBRARY "-framework Cocoa")
    endif()
    if(NOT COREVIDEO_LIBRARY OR COREVIDEO_LIBRARY MATCHES ".*-NOTFOUND")
        set(COREVIDEO_LIBRARY "-framework CoreVideo")
    endif()
    if(NOT VIDEOTOOLBOX_LIBRARY OR VIDEOTOOLBOX_LIBRARY MATCHES ".*-NOTFOUND")
        set(VIDEOTOOLBOX_LIBRARY "-framework VideoToolbox")
    endif()
    if(NOT COREMEDIA_LIBRARY OR COREMEDIA_LIBRARY MATCHES ".*-NOTFOUND")
        set(COREMEDIA_LIBRARY "-framework CoreMedia")
    endif()
    if(NOT COREFOUNDATION_LIBRARY OR COREFOUNDATION_LIBRARY MATCHES ".*-NOTFOUND")
        set(COREFOUNDATION_LIBRARY "-framework CoreFoundation")
    endif()
    if(NOT SECURITY_LIBRARY OR SECURITY_LIBRARY MATCHES ".*-NOTFOUND")
        set(SECURITY_LIBRARY "-framework Security")
    endif()
    # 这些是库而非框架
    if(NOT ZLIB_LIBRARY OR ZLIB_LIBRARY MATCHES ".*-NOTFOUND")
        set(ZLIB_LIBRARY "-lz")
    endif()
    if(NOT BZ2_LIBRARY OR BZ2_LIBRARY MATCHES ".*-NOTFOUND")
        set(BZ2_LIBRARY "-lbz2")
    endif()
    if(NOT ICONV_LIBRARY OR ICONV_LIBRARY MATCHES ".*-NOTFOUND")
        set(ICONV_LIBRARY "-liconv")
    endif()
    if(NOT AUDIOTOOLBOX_LIBRARY OR AUDIOTOOLBOX_LIBRARY MATCHES ".*-NOTFOUND")
        set(AUDIOTOOLBOX_LIBRARY "-framework AudioToolbox")
    endif()
    if(NOT METAL_LIBRARY OR METAL_LIBRARY MATCHES ".*-NOTFOUND")
        set(METAL_LIBRARY "-framework Metal")
    endif()
    if(NOT COREIMAGE_LIBRARY OR COREIMAGE_LIBRARY MATCHES ".*-NOTFOUND")
        set(COREIMAGE_LIBRARY "-framework CoreImage")
    endif()
    if(NOT OPENGL_LIBRARY OR OPENGL_LIBRARY MATCHES ".*-NOTFOUND")
        set(OPENGL_LIBRARY "-framework OpenGL")
    endif()
    if(NOT QUARTZCORE_LIBRARY OR QUARTZCORE_LIBRARY MATCHES ".*-NOTFOUND")
        set(QUARTZCORE_LIBRARY "-framework QuartzCore")
    endif()
    if(NOT APPLICATIONSERVICES_LIBRARY OR APPLICATIONSERVICES_LIBRARY MATCHES ".*-NOTFOUND")
        set(APPLICATIONSERVICES_LIBRARY "-framework ApplicationServices")
    endif()
    if(NOT COREAUDIO_LIBRARY OR COREAUDIO_LIBRARY MATCHES ".*-NOTFOUND")
        set(COREAUDIO_LIBRARY "-framework CoreAudio")
    endif()
    if(NOT IOKIT_LIBRARY OR IOKIT_LIBRARY MATCHES ".*-NOTFOUND")
        set(IOKIT_LIBRARY "-framework IOKit")
    endif()
    if(NOT FORCEFEEDBACK_LIBRARY OR FORCEFEEDBACK_LIBRARY MATCHES ".*-NOTFOUND")
        set(FORCEFEEDBACK_LIBRARY "-framework ForceFeedback")
    endif()
    if(NOT COREHAPTICS_LIBRARY OR COREHAPTICS_LIBRARY MATCHES ".*-NOTFOUND")
        set(COREHAPTICS_LIBRARY "-framework CoreHaptics")
    endif()
    if(NOT FOUNDATION_LIBRARY OR FOUNDATION_LIBRARY MATCHES ".*-NOTFOUND")
        set(FOUNDATION_LIBRARY "-framework Foundation")
    endif()
    if(NOT GAMECONTROLLER_LIBRARY OR GAMECONTROLLER_LIBRARY MATCHES ".*-NOTFOUND")
        set(GAMECONTROLLER_LIBRARY "-framework GameController")
    endif()
    if(NOT CARBON_LIBRARY OR CARBON_LIBRARY MATCHES ".*-NOTFOUND")
        set(CARBON_LIBRARY "-framework Carbon")
    endif()
    # 不再添加 HIToolbox（新 SDK 中单独链接可能失败）

    # SDL2_ttf依赖的库（静态）
    list(APPEND LIBS 
        ${SDL_TTF_INSTALL_DIR}/lib/libfreetype.a
        ${SDL_TTF_INSTALL_DIR}/lib/libharfbuzz.a
    )

    set(MACOS_FRAMEWORKS 
        ${COCOA_LIBRARY}
        ${COREVIDEO_LIBRARY} 
        ${VIDEOTOOLBOX_LIBRARY}
        ${COREMEDIA_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
        ${SECURITY_LIBRARY}
        ${ZLIB_LIBRARY}
        ${BZ2_LIBRARY}
        ${ICONV_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY}
        ${METAL_LIBRARY}
        ${COREIMAGE_LIBRARY}
        ${OPENGL_LIBRARY}
        ${QUARTZCORE_LIBRARY}
        ${APPLICATIONSERVICES_LIBRARY}
        ${COREAUDIO_LIBRARY}
        ${IOKIT_LIBRARY}
        ${FORCEFEEDBACK_LIBRARY}
        ${COREHAPTICS_LIBRARY}
        ${FOUNDATION_LIBRARY}
        ${GAMECONTROLLER_LIBRARY}
        ${CARBON_LIBRARY}
    )

    list(APPEND LIBS ${MACOS_FRAMEWORKS})
endif()

# 创建可执行文件
add_executable(video-compare ${SOURCES})

# 链接库
target_link_libraries(video-compare ${LIBS})

# 安装目标（可选）
install(TARGETS video-compare DESTINATION bin)