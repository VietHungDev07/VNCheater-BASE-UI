cmake_minimum_required(VERSION 3.24)
project(VNCheater LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(WEBVIEW2_SDK "${CMAKE_SOURCE_DIR}/vendor/webview2" CACHE PATH "Extracted WebView2 SDK")
if(NOT EXISTS "${WEBVIEW2_SDK}/build/native/include/WebView2.h")
  message(FATAL_ERROR "WebView2 SDK missing. Run scripts/build.ps1 first.")
endif()
add_executable(VNCheater WIN32 native/main.cpp native/SplashWindow.cpp native/app.rc)
target_compile_definitions(VNCheater PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(VNCheater PRIVATE /W4 /permissive- /utf-8)
target_include_directories(VNCheater PRIVATE "${WEBVIEW2_SDK}/build/native/include")
target_link_libraries(VNCheater PRIVATE "${WEBVIEW2_SDK}/build/native/x64/WebView2LoaderStatic.lib" user32 ole32 shell32 shlwapi dwmapi version advapi32 gdi32 gdiplus)
target_link_options(VNCheater PRIVATE /MANIFEST:NO)
add_custom_command(TARGET VNCheater POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VNCheater>/ui"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/index.html" "$<TARGET_FILE_DIR:VNCheater>/ui/index.html"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/css" "$<TARGET_FILE_DIR:VNCheater>/ui/css"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/js" "$<TARGET_FILE_DIR:VNCheater>/ui/js"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/images" "$<TARGET_FILE_DIR:VNCheater>/ui/assets/images"
)

# Optional offline renderer QA; uses exactly the same native splash code.
add_executable(SplashPreview EXCLUDE_FROM_ALL native/SplashPreview.cpp native/SplashWindow.cpp)
target_compile_definitions(SplashPreview PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(SplashPreview PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(SplashPreview PRIVATE user32 gdi32 gdiplus ole32)
