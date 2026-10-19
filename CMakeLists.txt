cmake_minimum_required(VERSION 3.20)
project(Snex9x_v2 VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/MP /EHsc /utf-8 /W1)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ============================================================================
# 1. External Dependencies Paths (Local Sibling Packages + Cloned Repos)
# ============================================================================
set(SNES9X_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/snes9x")
set(PHOTINO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/photino.Native/Photino.Native")
set(SDL3_DIR "D:/Rodrigo/Documents/Dev/vscode/Desktop/C++/PS1_Remake/SDL3")
set(WEBVIEW2_PKG_DIR "D:/Rodrigo/Documents/Dev/vscode/Desktop/C++/photino.Native/packages/Microsoft.Web.WebView2.1.0.2903.40/build/native")
set(WIL_PKG_DIR "D:/Rodrigo/Documents/Dev/vscode/Desktop/C++/photino.Native/packages/Microsoft.Windows.ImplementationLibrary.1.0.240803.1/include")

# ============================================================================
# 2. Snes9x Core Static Library
# ============================================================================
set(SNES9X_SOURCES
    "${SNES9X_DIR}/filter/snes_ntsc.c"
    "${SNES9X_DIR}/filter/epx.cpp"
    "${SNES9X_DIR}/filter/2xsai.cpp"
    "${SNES9X_DIR}/filter/hq2x.cpp"
    "${SNES9X_DIR}/filter/xbrz.cpp"
    "${SNES9X_DIR}/apu/apu.cpp"
    "${SNES9X_DIR}/apu/bapu/dsp/sdsp.cpp"
    "${SNES9X_DIR}/apu/bapu/smp/smp.cpp"
    "${SNES9X_DIR}/apu/bapu/smp/smp_state.cpp"
    "${SNES9X_DIR}/bsx.cpp"
    "${SNES9X_DIR}/c4.cpp"
    "${SNES9X_DIR}/c4emu.cpp"
    "${SNES9X_DIR}/cheats.cpp"
    "${SNES9X_DIR}/cheats2.cpp"
    "${SNES9X_DIR}/clip.cpp"
    "${SNES9X_DIR}/conffile.cpp"
    "${SNES9X_DIR}/controls.cpp"
    "${SNES9X_DIR}/cpu.cpp"
    "${SNES9X_DIR}/cpuexec.cpp"
    "${SNES9X_DIR}/cpuops.cpp"
    "${SNES9X_DIR}/crosshairs.cpp"
    "${SNES9X_DIR}/dma.cpp"
    "${SNES9X_DIR}/dsp.cpp"
    "${SNES9X_DIR}/dsp1.cpp"
    "${SNES9X_DIR}/dsp2.cpp"
    "${SNES9X_DIR}/dsp3.cpp"
    "${SNES9X_DIR}/dsp4.cpp"
    "${SNES9X_DIR}/fxinst.cpp"
    "${SNES9X_DIR}/fxemu.cpp"
    "${SNES9X_DIR}/gfx.cpp"
    "${SNES9X_DIR}/globals.cpp"
    "${SNES9X_DIR}/memmap.cpp"
    "${SNES9X_DIR}/obc1.cpp"
    "${SNES9X_DIR}/msu1.cpp"
    "${SNES9X_DIR}/ppu.cpp"
    "${SNES9X_DIR}/stream.cpp"
    "${SNES9X_DIR}/sa1.cpp"
    "${SNES9X_DIR}/sa1cpu.cpp"
    "${SNES9X_DIR}/screenshot.cpp"
    "${SNES9X_DIR}/sdd1.cpp"
    "${SNES9X_DIR}/sdd1emu.cpp"
    "${SNES9X_DIR}/seta.cpp"
    "${SNES9X_DIR}/seta010.cpp"
    "${SNES9X_DIR}/seta011.cpp"
    "${SNES9X_DIR}/seta018.cpp"
    "${SNES9X_DIR}/snapshot.cpp"
    "${SNES9X_DIR}/snes9x.cpp"
    "${SNES9X_DIR}/spc7110.cpp"
    "${SNES9X_DIR}/srtc.cpp"
    "${SNES9X_DIR}/tile.cpp"
    "${SNES9X_DIR}/tileimpl-n1x1.cpp"
    "${SNES9X_DIR}/tileimpl-n2x1.cpp"
    "${SNES9X_DIR}/tileimpl-h2x1.cpp"
    "${SNES9X_DIR}/sha256.cpp"
    "${SNES9X_DIR}/bml.cpp"
    "${SNES9X_DIR}/movie.cpp"
    "${SNES9X_DIR}/fscompat.cpp"
)

add_library(snes9x_core STATIC ${SNES9X_SOURCES})

target_include_directories(snes9x_core PUBLIC
    "${SNES9X_DIR}"
    "${SNES9X_DIR}/apu"
    "${SNES9X_DIR}/apu/bapu"
    "${SNES9X_DIR}/filter"
)

target_compile_definitions(snes9x_core PUBLIC
    RIGHTSHIFT_IS_SAR
    __LIBRETRO__
    ALLOW_CPU_OVERCLOCK
    HAVE_STDINT_H
    __WIN32__
)

# ============================================================================
# 3. Photino.Native Static Library (with SharedBuffer & CustomScheme Extensions)
# ============================================================================
set(PHOTINO_SOURCES
    "${PHOTINO_DIR}/Photino.Windows.cpp"
    "${PHOTINO_DIR}/Photino.Windows.DarkMode.cpp"
    "${PHOTINO_DIR}/Photino.Windows.Dialog.cpp"
    "${PHOTINO_DIR}/Dependencies/wintoastlib.cpp"
)

add_library(photino_native STATIC ${PHOTINO_SOURCES})

target_include_directories(photino_native PUBLIC
    "${PHOTINO_DIR}"
    "${PHOTINO_DIR}/Dependencies"
    "${WEBVIEW2_PKG_DIR}/include"
    "${WIL_PKG_DIR}"
)

target_compile_definitions(photino_native PUBLIC UNICODE _UNICODE)

target_link_libraries(photino_native PUBLIC
    "${WEBVIEW2_PKG_DIR}/x64/WebView2LoaderStatic.lib"
    Shlwapi
    Shcore
    Urlmon
    Ole32
    Shell32
    User32
    Gdi32
    Advapi32
)

# ============================================================================
# 4. Main Executable: Snex9x_v2 (MVC Application)
# ============================================================================
set(APP_SOURCES
    "src/main.cpp"
    "src/app.rc"
    "src/model/ConfigModel.cpp"
    "src/model/BoxartService.cpp"
    "src/model/GameLibraryModel.cpp"
    "src/model/EmulatorModel.cpp"
    "src/controller/InputController.cpp"
    "src/controller/AppController.cpp"
)

add_executable(Snex9x_v2 ${APP_SOURCES})

target_include_directories(Snex9x_v2 PRIVATE
    "src"
    "${SDL3_DIR}/include"
    "${PHOTINO_DIR}"
)

target_link_libraries(Snex9x_v2 PRIVATE
    snes9x_core
    photino_native
    "${SDL3_DIR}/lib/x64/SDL3.lib"
    winhttp
    ole32
    shell32
    uuid
    winmm
)

# Copy SDL3.dll next to executable after build
add_custom_command(TARGET Snex9x_v2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL3_DIR}/lib/x64/SDL3.dll"
        "$<TARGET_FILE_DIR:Snex9x_v2>/SDL3.dll"
    COMMENT "Copying SDL3.dll to output directory"
)
