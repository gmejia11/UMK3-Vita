# vita/vita.cmake -- included by the root CMakeLists.txt (inserted by apply_cmake_patch.py).
# Reuses ALL of the existing build (umk3-game, the tables extracted from the .ipa, etc.)
# and only changes the platform layer + VPK packaging.
set(UMK3_VITA_DIR "${CMAKE_CURRENT_LIST_DIR}")

if(VITA)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  set(UMK3_VITA_TITLEID "UMKT00003" CACHE STRING "Vita title id (4 letters + 5 digits)")
endif()

function(umk3_vita_link_platform tgt)
  set(R "${CMAKE_SOURCE_DIR}/src/runtime")

  target_sources(${tgt} PRIVATE
    ${UMK3_VITA_DIR}/src/vita_plat.c      # window/GL (vitaGL), input, time
    ${UMK3_VITA_DIR}/src/vita_rgl.c       # replaces rgl_front.c (OpenGL only)
    ${UMK3_VITA_DIR}/src/vita_log.c
    ${UMK3_VITA_DIR}/src/vita_trace.c
    ${UMK3_VITA_DIR}/src/vita_prof.c
    ${UMK3_VITA_DIR}/src/vita_par.c
    ${UMK3_VITA_DIR}/src/vita_watch.c
    ${UMK3_VITA_DIR}/src/vita_zmalloc.c
    ${UMK3_VITA_DIR}/src/vita_fs.c        # relative paths -> ux0:data/umk3/
    ${R}/platform/sdl_audio.c             # audio through SDL2 (vdpm)
    ${R}/platform/ui_alert.c
    ${UMK3_VITA_DIR}/src/vita_ui.c
    ${R}/mp_eos.c)

  target_include_directories(${tgt} BEFORE PRIVATE
    ${UMK3_VITA_DIR}/include
    ${R}/platform ${R}/render
    "$ENV{VITASDK}/arm-vita-eabi/include/SDL2")

  target_compile_options(${tgt} PRIVATE "SHELL:-include stddef.h" -mfpu=neon -O2 -mtune=cortex-a9 -fno-strict-aliasing -fwrapv -fsigned-char -fno-short-enums)
  target_compile_definitions(${tgt} PRIVATE
    UMK3_VITA=1 _snprintf=snprintf _strdup=strdup)

  target_link_options(${tgt} PRIVATE
    -Wl,--wrap=mk3_update -Wl,--wrap=MaintainParticles -Wl,--wrap=GameCodeMain -Wl,--wrap=LIME_RenderMeshSingleIndexed -Wl,--wrap=LIME_RenderMeshSingle -Wl,--wrap=printf -Wl,--wrap=puts -Wl,--wrap=putchar -Wl,--wrap=get_tsound
    -Wl,--wrap=fopen -Wl,--wrap=open -Wl,--wrap=stat -Wl,--wrap=access
    -Wl,--wrap=opendir -Wl,--wrap=mkdir -Wl,--wrap=remove -Wl,--wrap=rename -Wl,--no-enum-size-warning -Wl,--wrap=malloc -Wl,--wrap=lime_pvr_load -Wl,--wrap=lime_png_load -Wl,--wrap=fread -Wl,--wrap=lime_par_for -Wl,--wrap=lime_par_threads -Wl,--wrap=glDrawElements -Wl,--wrap=glDrawArrays -Wl,--wrap=glEnd -Wl,--wrap=glTexImage2D -Wl,--wrap=vglSwapBuffers
    -Wl,--wrap=Mix_LoadMUS -Wl,--wrap=Mix_FreeMusic -Wl,--wrap=Mix_PlayMusic -Wl,--wrap=Mix_FadeInMusic -Wl,--wrap=Mix_HaltMusic -Wl,--wrap=Mix_FadeOutMusic -Wl,--wrap=SDL_LockAudio -Wl,--wrap=SDL_LockAudioDevice -Wl,--wrap=SDL_CloseAudioDevice -Wl,--wrap=SDL_PauseAudioDevice -Wl,--wrap=SDL_WaitThread)

  # SDL2_mixer + whichever decoders your vitasdk has (a group: the order does not matter)
  set(UMK3_MIX_DEPS "")
  foreach(L opusfile opus vorbisfile vorbis vorbisenc ogg xmp modplug mikmod
            mpg123 mad FLAC)
    if(EXISTS "$ENV{VITASDK}/arm-vita-eabi/lib/lib${L}.a")
      list(APPEND UMK3_MIX_DEPS ${L})
    endif()
  endforeach()
  message(STATUS "SDL2_mixer deps: ${UMK3_MIX_DEPS}")
  set(UMK3_MIXER_GROUP -Wl,--start-group SDL2_mixer ${UMK3_MIX_DEPS} SDL2 m -Wl,--end-group)

  target_link_libraries(${tgt} PRIVATE
    vitaGL vitashark mathneon ${UMK3_MIXER_GROUP}
    SceShaccCg_stub SceGxm_stub SceDisplay_stub SceCommonDialog_stub SceAppMgr_stub
    SceSysmodule_stub SceCtrl_stub SceTouch_stub SceMotion_stub SceHid_stub
    SceAudio_stub SceAudioIn_stub ScePower_stub SceKernelDmacMgr_stub SceNet_stub SceNetCtl_stub
    pthread m)
  # vitaGL/vitashark bring C++ (GLSL preprocessor) and want these libs when present
  set(UMK3_EXTRA "")
  foreach(L SceShaccCgExt taihen_stub)
    if(EXISTS "$ENV{VITASDK}/arm-vita-eabi/lib/lib${L}.a")
      list(APPEND UMK3_EXTRA ${L})
    endif()
  endforeach()
  target_link_libraries(${tgt} PRIVATE ${UMK3_EXTRA} stdc++)
endfunction()

function(umk3_vita_package tgt)
  vita_create_self(eboot.bin ${tgt} UNSAFE)
  vita_create_vpk(umk3.vpk ${UMK3_VITA_TITLEID} eboot.bin
    VERSION 01.00
    NAME "Ultimate Mortal Kombat 3"
    FILE ${UMK3_VITA_DIR}/sce_sys/icon0.png sce_sys/icon0.png
    FILE ${UMK3_VITA_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE ${UMK3_VITA_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE ${UMK3_VITA_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
endfunction()
