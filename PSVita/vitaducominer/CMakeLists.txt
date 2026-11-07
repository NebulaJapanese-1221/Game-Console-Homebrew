cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(vitaducominer C)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaDucoMiner")
set(VITA_TITLEID  "NEBU00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

add_executable(${PROJECT_NAME}
  src/ui/main.c
  src/miner/miner.c
  src/net/net.c
  src/crypto/duco-s1.c
  src/net/http/http.c
  src/util/json.c
  src/pool/pool.c
  src/config/config.c
)

target_include_directories(${PROJECT_NAME}
  PRIVATE
    src/ui
    src/miner
    src/net
    src/net/http
    src/crypto
    src/pool
    src/util
    src/config
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    SceNet_stub
    SceNetCtl_stub
    SceHttp_stub
    SceSsl_stub
    SceSysmodule_stub
    SceIofilemgr_stub
    SceIofilemgrForDriver_stub
    SceStdio_0931_stub
    SceCtrl_stub
    SceTouch_stub
    SceHid_stub
    SceIme_stub
    vita2d
    SceAppMgr_stub
    SceCommonDialog_stub
    SceDisplay_stub
    SceGxm_stub
    ScePgf_stub
    pthread
    m
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
