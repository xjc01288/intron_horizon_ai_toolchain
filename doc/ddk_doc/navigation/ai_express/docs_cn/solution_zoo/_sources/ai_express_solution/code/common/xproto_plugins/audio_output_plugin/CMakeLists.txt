cmake_minimum_required(VERSION 2.8)

option(PARENT_BUILD "build subdirectory from here" OFF)
if(NOT ${PARENT_BUILD})
  set(CMAKE_C_COMPILER /opt/gcc-linaro-6.5.0-2018.12-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc)
  set(CMAKE_CXX_COMPILER /opt/gcc-linaro-6.5.0-2018.12-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-g++)
endif()
# 项目名称
project(audio_output_plugin)

STRING(REGEX REPLACE ".*/(.*)" "\\1" CURRENT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}) 

set(SOLUTION_ZOO_COMMON_DIR)
  function(get_parent_dir arg dir n)
    foreach(i RANGE ${ARGV2})
      message("i:${i} dir:${dir}")
      get_filename_component(dir ${dir} DIRECTORY)
    endforeach(i)
    set(SOLUTION_ZOO_COMMON_DIR ${dir} PARENT_SCOPE)
  endfunction()
get_parent_dir(XSTREAM_CURRENT_DIR ${PROJECT_SOURCE_DIR} 1)

message("solution_zoo common dir:${SOLUTION_ZOO_COMMON_DIR}")
# get deps directory end

set(WRAPPER_AUDIO_SOURCE ${SOLUTION_ZOO_COMMON_DIR}/wrapper/audio_source/alsa_device)
message("audio wrapper dir:${WRAPPER_AUDIO_SOURCE}")

if (NOT TARGET alsa_device)
add_subdirectory(${WRAPPER_AUDIO_SOURCE}
  ${WRAPPER_AUDIO_SOURCE}/output
)
endif()

if(DEFINED ENV{HORIZON_LIB_PATH})
  set(HOST_PACKAGE_DIR $ENV{HORIZON_LIB_PATH}/ddk/xj3_aarch64)
else()
  set(HOST_PACKAGE_DIR ~/.horizon/ddk/xj3_aarch64)
endif()

# 添加头文件路径
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/deps/hobotlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/deps/jsoncpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/xproto_plugins/message/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/wrapper/audio_source/alsa_device/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/thread_pool/
    ${HOST_PACKAGE_DIR}/xproto/include
    ${HOST_PACKAGE_DIR}/xstream/include
    ${HOST_PACKAGE_DIR}/appsdk/appuser/include
)

link_directories(
  ${HOST_PACKAGE_DIR}/xstream/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/deps/hobotlog/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/deps/jsoncpp/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/wrapper/audio_source/alsa_device/output/lib
  ${HOST_PACKAGE_DIR}/appsdk/appuser/lib
  ${HOST_PACKAGE_DIR}/appsdk/xproto/lib
)

# 头文件路径信息
file(GLOB PROJECT_INC
    "include/audio_output_plugin/*.h"
)

# 源文件路径信息
file(GLOB PROJECT_SRC
    "src/*.cc"
)

set(SOURCE_FILES ${PROJECT_SRC} ${PROJECT_INC})
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

set(BASE_LIBRARIES pthread jsoncpp asound alsa_device)
target_link_libraries(${PROJECT_NAME} ${BASE_LIBRARIES})

if(NOT ${PARENT_BUILD})
set(OUTPUT_ROOT ${CMAKE_SOURCE_DIR}/output/)
endif()
set(MY_OUTPUT_ROOT ${OUTPUT_ROOT}/audio_output_plugin)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/audio_output_plugin/audio_output_plugin.h
    DESTINATION ${MY_OUTPUT_ROOT}/include/audio_output_plugin)
install(TARGETS audio_output_plugin DESTINATION ${MY_OUTPUT_ROOT}/lib)
