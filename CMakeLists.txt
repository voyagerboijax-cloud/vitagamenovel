cmake_minimum_required(VERSION 3.16)

# Por defecto se compila para PS Vita. Para PC: cmake -DBUILD_PC=ON
option(BUILD_PC "Compilar la version de PC en lugar de la de Vita" OFF)

if(NOT BUILD_PC)
  if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
      set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
      message(FATAL_ERROR "Falta la variable VITASDK (para compilar en PC usa -DBUILD_PC=ON)")
    endif()
  endif()
endif()

project(novela_visual C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(SOURCES src/main.c src/vn.c)

if(BUILD_PC)
  # ---------------------------------------------------------------- PC
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl2 SDL2_image SDL2_ttf SDL2_mixer)

  add_executable(novela ${SOURCES})
  target_link_libraries(novela PRIVATE PkgConfig::SDL m)
  add_custom_command(TARGET novela POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:novela>/assets")
else()
  # --------------------------------------------------------------- Vita
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

  # Nombre que sale debajo del icono en el menu de la Vita (puedes cambiarlo).
  set(VITA_APP_NAME "El faro de Cabo Niebla")
  set(VITA_TITLEID  "NOVE00001")
  set(VITA_VERSION  "01.00")

  set(VITA_LIBDIR "$ENV{VITASDK}/arm-vita-eabi/lib")

  # Librerias de SDL2 y las que usan por dentro. Las que no existan se saltan solas.
  set(VITA_LIB_NAMES
    SDL2_mixer SDL2_ttf SDL2_image SDL2
    vorbisfile vorbis ogg mpg123 modplug opusfile opus FLAC xmp
    webp webpdemux webpmux sharpyuv tiff jpeg png freetype harfbuzz bz2 brotlidec brotlicommon z
    pthread vitaGL mathneon vitashark SceShaccCgExt taihen_stub)
  set(VITA_STUB_NAMES
    SceAppMgr SceAppUtil SceAudio SceAudioIn SceCamera SceCommonDialog SceCtrl SceDisplay
    SceFios2 SceGxm SceHid SceIme SceMotion ScePower SceRtc SceSysmodule SceTouch
    SceLibKernel SceKernelThreadMgr SceKernelDmacMgr SceKernelModulemgr SceProcessmgr SceIofilemgr SceSysmem
    SceShaccCg)

  set(VITA_LIBS "")
  foreach(name IN LISTS VITA_LIB_NAMES)
    find_library(VLIB_${name} NAMES ${name} PATHS "${VITA_LIBDIR}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(VLIB_${name})
      list(APPEND VITA_LIBS "${VLIB_${name}}")
    endif()
  endforeach()
  foreach(name IN LISTS VITA_STUB_NAMES)
    find_library(VSTUB_${name} NAMES ${name}_stub PATHS "${VITA_LIBDIR}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(VSTUB_${name})
      list(APPEND VITA_LIBS "${VSTUB_${name}}")
    endif()
  endforeach()
  message(STATUS "Librerias Vita encontradas: ${VITA_LIBS}")
  if(NOT VLIB_SDL2 OR NOT VLIB_SDL2_image OR NOT VLIB_SDL2_ttf OR NOT VLIB_SDL2_mixer)
    message(FATAL_ERROR "Faltan librerias de SDL2 en VitaSDK (SDL2, SDL2_image, SDL2_ttf o SDL2_mixer). "
                        "Mira la lista de arriba y avisame cual no aparece.")
  endif()

  add_executable(${PROJECT_NAME} ${SOURCES})
  target_link_libraries(${PROJECT_NAME} PRIVATE
    -Wl,--start-group ${VITA_LIBS} -Wl,--end-group
    stdc++ m)

  # Todo lo que haya dentro de assets/ viaja dentro del .vpk automaticamente.
  file(GLOB_RECURSE ASSET_FILES RELATIVE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/assets/*")
  set(VPK_EXTRA "")
  foreach(f IN LISTS ASSET_FILES)
    list(APPEND VPK_EXTRA FILE "${f}" "${f}")
  endforeach()

  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
  vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE sce_sys/icon0.png sce_sys/icon0.png
    FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    ${VPK_EXTRA}
  )
endif()
