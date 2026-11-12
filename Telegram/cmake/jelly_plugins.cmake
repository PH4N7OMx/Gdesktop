set(jelly_root "${CMAKE_SOURCE_DIR}/jellyplugins/v2")

add_executable(JellyPluginHost
    "${jelly_root}/host/main.cpp"
    "${jelly_root}/host/frame.cpp"
    "${jelly_root}/host/frame.h"
    "${jelly_root}/host/sandbox.cpp"
    "${jelly_root}/host/sandbox.h"
    "${jelly_root}/host/bootstrap.generated.h"
)
target_compile_features(JellyPluginHost PRIVATE cxx_std_20)
target_include_directories(JellyPluginHost SYSTEM PRIVATE "${quickjs_loc}")
target_link_libraries(JellyPluginHost PRIVATE tdesktop::lib_quickjs)
set_target_properties(JellyPluginHost PROPERTIES
    OUTPUT_NAME jelly-plugin-host
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/jelly-host"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
)
if (DESKTOP_APP_MAC_ARCH)
    set_target_properties(JellyPluginHost PROPERTIES OSX_ARCHITECTURES "${DESKTOP_APP_MAC_ARCH}")
endif()
if (WIN32)
    target_compile_definitions(JellyPluginHost PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(JellyPluginHost PRIVATE advapi32)
elseif (APPLE)
    target_link_libraries(JellyPluginHost PRIVATE "-framework CoreFoundation" "-framework Security")
    add_custom_command(TARGET JellyPluginHost POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - --entitlements "${jelly_root}/host/host.entitlements" "$<TARGET_FILE:JellyPluginHost>"
        VERBATIM
    )
else()
    find_package(Threads REQUIRED)
    target_link_libraries(JellyPluginHost PRIVATE m Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_dependencies(Telegram JellyPluginHost)
if (APPLE)
    set(jelly_runtime_destination "$<TARGET_FILE_DIR:Telegram>/../Helpers")
else()
    set(jelly_runtime_destination "$<TARGET_FILE_DIR:Telegram>/jelly-runtime")
endif()
add_custom_command(TARGET Telegram POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${jelly_runtime_destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:JellyPluginHost>" "${jelly_runtime_destination}/$<TARGET_FILE_NAME:JellyPluginHost>"
    VERBATIM
)
if (LINUX AND DESKTOP_APP_USE_PACKAGED)
    include(GNUInstallDirs)
    install(TARGETS JellyPluginHost RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/gummygram")
    target_compile_definitions(jelly_client_core PRIVATE
        JELLY_INSTALLED_HOST_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/gummygram/jelly-plugin-host"
    )
endif()
