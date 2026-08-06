cmake_minimum_required(VERSION 3.21)
project(LlamaCode VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_TESTS "Build unit tests (LlamaCodeTests)" OFF)
option(LC_USE_QTKEYCHAIN "Cifrar secretos vía QtKeychain (Secret Service / WinCred / macOS Keychain)" ON)
option(LC_STRICT_WARNINGS "Enable stricter compiler warnings for project targets" OFF)
option(LC_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(LC_ENABLE_CLANG_TIDY "Run clang-tidy during C++ builds when available" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Quick Sql Concurrent Network Widgets Multimedia Svg)

# --- QtKeychain: almacenamiento de secretos respaldado por el SO, multiplataforma.
# Se intenta find_package (system/vcpkg); si no está y la opción está ON, se baja con
# FetchContent. Si todo falla (build offline sin paquete), se sigue sin keychain y
# SecretStore usa su fallback (DPAPI en Windows / texto plano marcado en otros OS).
set(LC_HAVE_QTKEYCHAIN OFF)
if (LC_USE_QTKEYCHAIN)
    find_package(Qt6Keychain QUIET)
    if (NOT Qt6Keychain_FOUND)
        include(FetchContent)
        set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)            # qtkeychain default es Qt5
        set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)   # no compilar demos de qtkeychain
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(QTKEYCHAIN_STATIC ON CACHE BOOL "" FORCE)
        FetchContent_Declare(qtkeychain
            GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
            GIT_TAG 0.14.3)
        FetchContent_MakeAvailable(qtkeychain)
    endif()
    if (TARGET Qt6Keychain OR TARGET qt6keychain OR Qt6Keychain_FOUND)
        set(LC_HAVE_QTKEYCHAIN ON)
    endif()
endif()

qt_standard_project_setup(REQUIRES 6.4)

set(CORE_SOURCES
    src/AppController.cpp
    src/ThemeProvider.cpp
    src/core/LlamaBinary.cpp
    src/core/BinaryRegistry.cpp
    src/core/CapabilityDetector.cpp
    src/core/ModelRoot.cpp
    src/core/ModelRootRegistry.cpp
    src/core/CatalogModel.cpp
    src/core/ModelCatalog.cpp
    src/core/GGUFScanner.cpp
    src/core/DocumentExtractor.cpp
    src/core/MermaidRenderer.cpp
    src/core/ControlApi.cpp
    src/core/SecretStore.cpp
    src/core/profiles/ProfileTypes.cpp
    src/core/profiles/ProfileManager.cpp
    src/core/profiles/EffectiveProfileBuilder.cpp
    src/core/agent/OpencodeBackend.cpp
    src/core/agent/RawChatBackend.cpp
    src/core/agent/LlamaAgentBackend.cpp
    src/core/agent/McpClient.cpp
    src/core/agent/AgentToolRunner.cpp
    src/core/agent/SubAgentRunner.cpp
    src/core/agent/AgentEventLog.cpp
    src/core/agent/MemoryStore.cpp
    src/core/agent/GraphStore.cpp
    src/core/agent/MasterCli.cpp
    src/core/agent/BrowserTeach.cpp
    src/core/mail/MailClient.cpp
    src/core/voice/VoiceTypes.cpp
    src/core/voice/AudioCodec.cpp
    src/core/voice/SttEngine.cpp
    src/core/voice/TtsEngine.cpp
    src/core/voice/VoiceController.cpp
    src/core/voice/VoiceServerManager.cpp
    src/core/eval/EvalSuite.cpp
    src/core/tasks/TaskStore.cpp
    src/core/tasks/CronSchedule.cpp
    src/core/tasks/TaskScheduler.cpp
    src/core/tuner/AutoTuner.cpp
    src/core/tuner/TunerEngine.cpp
    src/core/tuner/TunerWorker.cpp
    src/core/studia/StudiaIndex.cpp
    src/core/studia/StudiaTexto.cpp
    src/core/studia/StudiaPlot.cpp
    src/core/studia/StudiaEmbed.cpp
    src/core/studia/StudiaPrompt.cpp
    src/core/studia/StudiaSessionStore.cpp
    src/core/studia/StudiaController.cpp
)

set(HEADERS
    src/AppController.h
    src/ThemeProvider.h
    src/core/LlamaBinary.h
    src/core/BinaryRegistry.h
    src/core/CapabilityDetector.h
    src/core/ModelRoot.h
    src/core/ModelRootRegistry.h
    src/core/CatalogModel.h
    src/core/ModelCatalog.h
    src/core/GGUFScanner.h
    src/core/DocumentExtractor.h
    src/core/MermaidRenderer.h
    src/core/ControlApi.h
    src/core/profiles/ProfileTypes.h
    src/core/profiles/ProfileManager.h
    src/core/profiles/EffectiveProfileBuilder.h
    src/core/agent/AgentTypes.h
    src/core/agent/IAgentBackend.h
    src/core/agent/OpencodeBackend.h
    src/core/agent/RawChatBackend.h
    src/core/agent/LlamaAgentBackend.h
    src/core/agent/McpClient.h
    src/core/agent/AgentToolRunner.h
    src/core/agent/SubAgentRunner.h
    src/core/agent/AgentEventLog.h
    src/core/agent/MemoryStore.h
    src/core/agent/GraphStore.h
    src/core/agent/BrowserTeach.h
    src/core/mail/MailClient.h
    src/core/voice/VoiceTypes.h
    src/core/voice/AudioCodec.h
    src/core/voice/SttEngine.h
    src/core/voice/TtsEngine.h
    src/core/voice/VoiceController.h
    src/core/voice/VoiceServerManager.h
    src/core/eval/EvalSuite.h
    src/core/tasks/TaskStore.h
    src/core/tasks/CronSchedule.h
    src/core/tasks/TaskScheduler.h
    src/core/tuner/AutoTuner.h
    src/core/tuner/TunerEngine.h
    src/core/tuner/TunerWorker.h
    src/core/studia/StudiaIndex.h
    src/core/studia/StudiaTexto.h
    src/core/studia/StudiaPlot.h
    src/core/studia/StudiaEmbed.h
    src/core/studia/StudiaPrompt.h
    src/core/studia/StudiaSessionStore.h
    src/core/studia/StudiaController.h
)

# ── Libreria core compartida (app + tests) ──
# Toda la logica del nucleo vive aca para que los tests linkeen contra los mismos
# objetos que el app, sin recompilar ni mantener listas de fuentes duplicadas.
add_library(llamacode_core STATIC
    ${CORE_SOURCES}
    ${HEADERS}
)
target_include_directories(llamacode_core PUBLIC src)
target_link_libraries(llamacode_core PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Svg
)
if (MSVC)
    target_compile_options(llamacode_core PRIVATE /utf-8)
endif()
if (LC_STRICT_WARNINGS)
    if (MSVC)
        target_compile_options(llamacode_core PRIVATE /W4 /permissive-)
    else()
        target_compile_options(llamacode_core PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
if (LC_WARNINGS_AS_ERRORS)
    if (MSVC)
        target_compile_options(llamacode_core PRIVATE /WX)
    else()
        target_compile_options(llamacode_core PRIVATE -Werror)
    endif()
endif()
if (LC_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if (CLANG_TIDY_EXE)
        set_target_properties(llamacode_core PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--quiet")
    else()
        message(WARNING "LC_ENABLE_CLANG_TIDY=ON but clang-tidy was not found")
    endif()
endif()
if (WIN32)
    # DPAPI (CryptProtectData/CryptUnprotectData): fallback de cifrado at-rest.
    target_link_libraries(llamacode_core PUBLIC Crypt32)
endif()
if (LC_HAVE_QTKEYCHAIN)
    # El target exportado puede llamarse Qt6Keychain o qt6keychain según la fuente.
    if (TARGET Qt6Keychain)
        target_link_libraries(llamacode_core PUBLIC Qt6Keychain)
    else()
        target_link_libraries(llamacode_core PUBLIC qt6keychain)
        # Vía FetchContent el target no exporta include dir: agregar src + build
        # (donde queda keychain.h y el export header generado).
        if (DEFINED qtkeychain_SOURCE_DIR)
            target_include_directories(llamacode_core PUBLIC
                ${qtkeychain_SOURCE_DIR} ${qtkeychain_BINARY_DIR})
        endif()
    endif()
    target_compile_definitions(llamacode_core PUBLIC HAVE_QTKEYCHAIN)
    message(STATUS "SecretStore: usando QtKeychain (cifrado respaldado por el SO)")
else()
    message(STATUS "SecretStore: QtKeychain no disponible → fallback DPAPI/plano")
endif()

qt_add_executable(LlamaCode
    src/main.cpp
)
target_link_libraries(LlamaCode PRIVATE llamacode_core)
target_compile_definitions(LlamaCode PRIVATE
    "$<$<CONFIG:Debug>:LC_DEBUG_ICON>")

if (WIN32)
    target_sources(LlamaCode PRIVATE app_icon.rc)
    target_link_libraries(LlamaCode PRIVATE shell32)  # SetCurrentProcessExplicitAppUserModelID
    # Debug builds embed the red debug_icon.ico; Release uses app_icon.ico.
    set_source_files_properties(app_icon.rc PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:/dLC_DEBUG_ICON>")
endif()

# NOTE: en Linux/macOS el ejecutable se llama "LlamaCode" (sin extension) y
# colisiona con el directorio del modulo QML cuyo URI tambien es "LlamaCode"
# (ld: "cannot open output file LlamaCode: Is a directory"). En Windows no pasa
# porque el binario es "LlamaCode.exe". Reubicamos el modulo bajo qml/ para
# evitar el choque; el modulo igual se embebe via qmlcachegen + recursos, asi
# que no se necesita en disco en runtime.
qt_add_qml_module(LlamaCode
    URI LlamaCode
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/LlamaCode
    QML_FILES
        qml/Main.qml
        qml/pages/BinariesPage.qml
        qml/pages/ModelRootsPage.qml
        qml/pages/ProfilesPage.qml
        qml/pages/LaunchPage.qml
        qml/pages/ChatPage.qml
        qml/components/NavBar.qml
        qml/components/PageHeader.qml
        qml/components/CommandPreview.qml
        qml/components/LcButton.qml
        qml/components/LcComboBox.qml
        qml/components/LcTextField.qml
        qml/components/LcDialog.qml
        qml/components/LcScrollBar.qml
        qml/components/OpencodeConfigDialog.qml
        qml/components/MermaidPreviewDialog.qml
        qml/pages/AgentPage.qml
        qml/pages/TasksPage.qml
        qml/pages/BenchmarkPage.qml
        qml/pages/ResearchPage.qml
        qml/pages/CharlaPage.qml
        qml/pages/StudiaPage.qml
        qml/pages/SettingsPage.qml
)

if (MSVC)
    target_compile_options(LlamaCode PRIVATE /utf-8)
endif()
if (LC_STRICT_WARNINGS)
    if (MSVC)
        target_compile_options(LlamaCode PRIVATE /W4 /permissive-)
    else()
        target_compile_options(LlamaCode PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if (CLANG_FORMAT_EXE)
    file(GLOB_RECURSE LLAMACODE_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp")
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${LLAMACODE_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting C++ sources with clang-format")
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${LLAMACODE_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking C++ source formatting with clang-format")
endif()

file(GLOB LLAMACODE_BUNDLED_CUSTOM_BENCHMARKS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS
    "assets/benchmarks/custom/*.json"
)

qt_add_resources(LlamaCode "llamacode_assets"
    PREFIX "/"
    FILES
        assets/app_icon.png
        assets/app_icon.ico
        assets/debug_icon.ico
        assets/update/latest.json
        assets/hwfit/hf_models.json
        assets/benchmarks/aa_intelligence.json
        assets/benchmarks/local_cookbook_priorities.json
        ${LLAMACODE_BUNDLED_CUSTOM_BENCHMARKS}
)

target_include_directories(LlamaCode PRIVATE src)

target_link_libraries(LlamaCode
    PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Svg
)

set_target_properties(LlamaCode PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# ── Tests unit + integration ──
# Un executable por subsistema (QtTest = 1 QTEST_MAIN por binario). Cada uno
# linkea la lib core (mismos objetos que el app) + Qt6::Test.
# Para agregar tests de una feature nueva: crear tests/test_<area>.cpp y sumar
# una linea add_lc_test(<area> tests/test_<area>.cpp). Ver CLAUDE.md.
if (BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)

    function(add_lc_test name file)
        qt_add_executable(${name} ${file})
        # Subsistema consola: si no, QtTest no imprime a stdout (output perdido).
        set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
        if (MSVC)
            target_compile_options(${name} PRIVATE /utf-8)
            target_link_options(${name} PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
        endif()
        target_include_directories(${name} PRIVATE src tests)
        target_link_libraries(${name} PRIVATE llamacode_core Qt6::Test)
        add_test(NAME ${name} COMMAND ${name})
        if (WIN32)
            set_property(TEST ${name} PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:${name}>"
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:llamacode_core>"
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            )
            if (LC_HAVE_QTKEYCHAIN)
                if (TARGET Qt6Keychain)
                    set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6Keychain>")
                else()
                    set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                        "PATH=path_list_prepend:$<TARGET_FILE_DIR:qt6keychain>")
                endif()
            endif()
        endif()
    endfunction()

    add_lc_test(test_gguf_profiles      tests/test_gguf_profiles.cpp)
    add_lc_test(test_profiles           tests/test_profiles.cpp)
    add_lc_test(test_secret_store       tests/test_secret_store.cpp)
    add_lc_test(test_registries         tests/test_registries.cpp)
    add_lc_test(test_catalog            tests/test_catalog.cpp)
    add_lc_test(test_capability         tests/test_capability.cpp)
    add_lc_test(test_document_extractor tests/test_document_extractor.cpp)
    add_lc_test(test_memory_graph       tests/test_memory_graph.cpp)
    add_lc_test(test_tuner              tests/test_tuner.cpp)
    add_lc_test(test_eval               tests/test_eval.cpp)
    add_lc_test(test_control_api        tests/test_control_api.cpp)
    add_lc_test(test_agent_tools        tests/test_agent_tools.cpp)
    add_lc_test(test_agent_wire         tests/test_agent_wire.cpp)
    add_lc_test(test_master_cli         tests/test_master_cli.cpp)
    add_lc_test(test_backends_net       tests/test_backends_net.cpp)
    add_lc_test(test_appcontroller      tests/test_appcontroller.cpp)
    add_lc_test(test_mermaid            tests/test_mermaid.cpp)
    add_lc_test(test_mail               tests/test_mail.cpp)
    add_lc_test(test_voice              tests/test_voice.cpp)
    add_lc_test(test_tasks              tests/test_tasks.cpp)
    add_lc_test(test_cron               tests/test_cron.cpp)
    add_lc_test(test_studia             tests/test_studia.cpp)

    # Ingestor de StudIA: es Python (usa markitdown/pypdf), asi que su suite es
    # unittest y se engancha a ctest como un test mas. Si no hay Python en el
    # sistema, no se registra (el resto de la suite sigue corriendo).
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        add_test(NAME test_studia_ingest
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/studia/test_ingest.py)
    else()
        message(STATUS "Python3 no encontrado: se omite test_studia_ingest")
    endif()
endif()
