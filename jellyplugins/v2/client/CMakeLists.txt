if (NOT TARGET Qt::Core)
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()

add_library(jelly_client_core STATIC
    contract.cpp contract.h contract.generated.h
    launcher.cpp launcher.h
    scheduler.cpp scheduler.h
    store.cpp store.h
    supervisor.cpp supervisor.h
)
target_compile_features(jelly_client_core PUBLIC cxx_std_20)
target_include_directories(jelly_client_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(jelly_client_core PUBLIC Qt::Core)
if (WIN32)
    target_compile_definitions(jelly_client_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(jelly_client_core PRIVATE advapi32)
endif()
if (MSVC)
    target_compile_options(jelly_client_core PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(jelly_client_core PRIVATE -Wall -Wextra -Werror)
endif()
