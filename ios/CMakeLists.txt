# POSTAL for iOS: builds POSTAL.app (arm64) from the source release.
# Configure from the repo root:
#   cmake -S ios -B build -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DSDL2_ROOT=<deps> -DPOSTAL_IOS_BUILD_NUMBER=<n>
cmake_minimum_required(VERSION 3.20)
project(POSTAL C CXX OBJC)

set(TOP ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(NOT POSTAL_IOS_BUILD_NUMBER)
  set(POSTAL_IOS_BUILD_NUMBER 1)
endif()

# The same game sources as the Unix makefile
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/sources.txt POSTAL_SRCS)
list(TRANSFORM POSTAL_SRCS PREPEND ${TOP}/)

add_executable(POSTAL MACOSX_BUNDLE
  ${POSTAL_SRCS}
  ${CMAKE_CURRENT_SOURCE_DIR}/IOSSupport.m
  ${CMAKE_CURRENT_SOURCE_DIR}/IOSGame.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/IOSBot.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/IOSTouchCore.c
  ${CMAKE_CURRENT_SOURCE_DIR}/IOSTouch.m
  ${CMAKE_CURRENT_SOURCE_DIR}/IOSPhotos.m
  ${CMAKE_CURRENT_SOURCE_DIR}/POSTAL.INI
)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/POSTAL.INI PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
# The game's own 1997 code builds with warnings off (as the makefile); the
# iOS files are new and must build with no warnings at all.
set_source_files_properties(${POSTAL_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/IOSSupport.m ${CMAKE_CURRENT_SOURCE_DIR}/IOSTouch.m
  ${CMAKE_CURRENT_SOURCE_DIR}/IOSPhotos.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-Wall;-Wextra;-Werror;-Wno-unused-parameter;-Wno-missing-field-initializers")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/IOSTouchCore.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wno-unused-parameter;-Wno-missing-field-initializers")
# IOSGame/IOSBot include the game's 1997 headers, which warn by the dozen:
# warnings off here; their own lines are checked with -Wall in the Linux rig.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/IOSGame.cpp ${CMAKE_CURRENT_SOURCE_DIR}/IOSBot.cpp
  PROPERTIES COMPILE_OPTIONS "-w")

target_compile_definitions(POSTAL PRIVATE
  PLATFORM_UNIX=1 PLATFORM_IOS=1 LOCALE=US TARGET=POSTAL_2015 ALLOW_TWINSTICK)
# As the makefile: char is signed (arm64 makes it unsigned by default)
target_compile_options(POSTAL PRIVATE -fsigned-char
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++11-narrowing>
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11>)

target_include_directories(POSTAL PRIVATE
  ${SDL2_ROOT}/include/SDL2
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${TOP}
  ${TOP}/RSPiX ${TOP}/RSPiX/Inc ${TOP}/RSPiX/Src
  ${TOP}/RSPiX/Src/BLUE ${TOP}/RSPiX/Src/BLUE/unix
  ${TOP}/RSPiX/Src/CYAN ${TOP}/RSPiX/Src/CYAN/Unix
  ${TOP}/RSPiX/Src/ORANGE ${TOP}/RSPiX/Src/ORANGE/CDT ${TOP}/RSPiX/Src/ORANGE/GameLib
  ${TOP}/RSPiX/Src/ORANGE/File ${TOP}/RSPiX/Src/ORANGE/Multigrid
  ${TOP}/RSPiX/Src/GREEN/Image
  ${TOP}/WishPiX ${TOP}/WishPiX/Spry)

set_target_properties(POSTAL PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  MACOSX_BUNDLE_GUI_IDENTIFIER io.github.sithewok13.postal
  MACOSX_BUNDLE_BUNDLE_NAME POSTAL
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.${POSTAL_IOS_BUILD_NUMBER}"
  MACOSX_BUNDLE_BUNDLE_VERSION ${POSTAL_IOS_BUILD_NUMBER})

target_link_libraries(POSTAL
  ${SDL2_ROOT}/lib/libSDL2main.a ${SDL2_ROOT}/lib/libSDL2.a
  "-framework Foundation" "-framework UIKit" "-framework OpenGLES"
  "-framework QuartzCore" "-framework CoreGraphics" "-framework CoreVideo"
  "-framework CoreMotion" "-framework CoreAudio" "-framework AudioToolbox"
  "-framework AVFoundation" "-framework GameController" "-framework Metal"
  "-framework Photos" "-framework ImageIO"
  "-weak_framework CoreHaptics" "-weak_framework UniformTypeIdentifiers"
  iconv)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/icon.png)
  add_custom_command(TARGET POSTAL POST_BUILD
    COMMAND sips -z 120 120 ${CMAKE_CURRENT_SOURCE_DIR}/icon.png --out $<TARGET_BUNDLE_DIR:POSTAL>/AppIcon60x60@2x.png
    COMMAND sips -z 180 180 ${CMAKE_CURRENT_SOURCE_DIR}/icon.png --out $<TARGET_BUNDLE_DIR:POSTAL>/AppIcon60x60@3x.png
    VERBATIM)
endif()
