cmake_minimum_required(VERSION 3.16)

# Read version from VERSION file
file(READ "VERSION" PROJECT_VERSION_STR)
string(STRIP "${PROJECT_VERSION_STR}" PROJECT_VERSION_STR)

project(txt2img VERSION ${PROJECT_VERSION_STR} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
)

# Get current year for copyright
string(TIMESTAMP COPYRIGHT_YEAR "%Y")

set(SOURCES
    src/main.cpp
    include/ArgumentParser.cpp
)

# Windows Resource file
if(WIN32)
    set(PROJECT_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/docs/media/icons/txt2img.ico")
    file(TO_NATIVE_PATH "${PROJECT_ICON_PATH}" PROJECT_ICON_PATH)
    string(REPLACE "\\" "\\\\" PROJECT_ICON_PATH "${PROJECT_ICON_PATH}")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/include/resource.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/resource.rc"
    )
    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/resource.rc")
endif()

# Find FreeType
find_package(Freetype REQUIRED)

# Find Threads (required for std::async/std::thread on Linux/Unix)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} ${SOURCES})

# Compilation Options
if(MSVC)
    # /utf-8 ensures source is read correctly; /W4 for high-quality warnings
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# Option for static linking (useful for the "single file executable" goal)
option(BUILD_STATIC "Build as a static executable" OFF)
if(BUILD_STATIC)
    if(MSVC)
        # For MSVC, use /MT to link the runtime statically
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        # For GCC/Clang, use -static
        target_link_options(${PROJECT_NAME} PRIVATE -static)
    endif()
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR} # For generated version.h
    PRIVATE ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE ${FREETYPE_LIBRARIES}
    PRIVATE Threads::Threads
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Linux specific installation
if(LINUX)
    # Desktop file
    install(FILES "docs/media/com.github.Lateralus138.txt2img.desktop"
            DESTINATION "share/applications")
    
    # AppStream metainfo
    install(FILES "docs/media/com.github.Lateralus138.txt2img.metainfo.xml"
            DESTINATION "share/metainfo")
    
    # Icons (hicolor theme)
    set(ICON_SIZES 16 32 48 64 128 256)
    foreach(SIZE ${ICON_SIZES})
        install(FILES "docs/media/icons/hicolor/${SIZE}x${SIZE}/apps/com.github.Lateralus138.txt2img.png"
                DESTINATION "share/icons/hicolor/${SIZE}x${SIZE}/apps")
    endforeach()
    install(FILES "docs/media/icons/hicolor/scalable/apps/com.github.Lateralus138.txt2img.svg"
            DESTINATION "share/icons/hicolor/scalable/apps")

    # Documentation (Debian style)
    install(FILES "debian/copyright"
            DESTINATION "share/doc/${PROJECT_NAME}")
    
    # Generate Debian changelog
    string(TIMESTAMP CURRENT_RFC2822_DATE "%a, %d %b %Y %H:%M:%S %z")
    configure_file(debian/changelog.in debian/changelog @ONLY)
    
    # Compress changelog
    execute_process(COMMAND gzip -cn9 "${CMAKE_CURRENT_BINARY_DIR}/debian/changelog"
                    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz"
            DESTINATION "share/doc/${PROJECT_NAME}")

    # Man page
    execute_process(COMMAND gzip -cn9 "${CMAKE_CURRENT_SOURCE_DIR}/docs/txt2img.1"
                    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/txt2img.1.gz")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/txt2img.1.gz"
            DESTINATION "share/man/man1")
endif()

# CPack configuration
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "Ian Pride")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Render wrapped text into an image")
set(CPACK_PACKAGE_DESCRIPTION "Text 2 Image is a cross-platform command-line utility designed to\n render ANSI-styled text into high-quality images. It supports word\n wrapping, multi-page output, and highly customizable formatting.")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if(LINUX)
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ian Pride <faithnomoread@yahoo.com>")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libfreetype6")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Lateralus138/txt2img")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")
    # Control file for icon cache refresh
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()

include(CPack)
