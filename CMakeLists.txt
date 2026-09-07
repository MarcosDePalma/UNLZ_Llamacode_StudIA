cmake_minimum_required(VERSION 3.21)
project(LlamaCode VERSION 0.1.116 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_TESTS "Build unit tests (LlamaCodeTests)" OFF)
option(LC_USE_QTKEYCHAIN "Cifrar secretos vía QtKeychain (Secret Service / WinCred / macOS Keychain)" ON)
option(LC_STRICT_WARNINGS "Enable stricter compiler warnings for project targets" OFF)
option(LC_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(LC_ENABLE_CLANG_TIDY "Run clang-tidy during C++ builds when available" OFF)
option(LC_USE_OPENCV "Use OpenCV acceleration for visual template matching when available" ON)

# MSVC añade /RTC1 automáticamente a Debug. Es incompatible con /O2; quitarlo
# en la configuración, antes de declarar los targets, permite que Debug sea un
# candidato optimizado sin perder símbolos ni aserciones.
if (MSVC)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    # Debug combina con las dependencias Qt release; usa el mismo CRT /MD.
    # Los símbolos PDB y las aserciones no dependen de /MDd.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING
        "MSVC runtime for the optimized Debug candidate" FORCE)
endif()

# Qt Debug carga DLLs de instrumentación muy grandes (Qt6Cored.dll,
# Qt6Quickd.dll, etc.) y puede tardar decenas de segundos antes de entrar a
# main(). El candidato Debug de LlamaCode conserva sus propios símbolos,
# asserts y optimización, pero usa las DLL release de Qt para tener un arranque
# representativo del producto y evitar ese bloqueo del loader.
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release CACHE STRING
    "Use optimized imported dependencies for the Debug candidate" FORCE)

# FetchContent still populates a missing dependency on first configure, but an
# existing checkout must not contact its remote on every CMake regeneration.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
    "Do not update already populated FetchContent dependencies" FORCE)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Quick QuickControls2 Sql Concurrent Network Widgets Multimedia Svg)
if (LC_USE_OPENCV)
    find_package(OpenCV QUIET COMPONENTS core imgproc)
endif()

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
    src/TrayController.cpp
    src/ThemeProvider.cpp
    src/core/LlamaBinary.cpp
    src/core/BinaryRegistry.cpp
    src/core/CapabilityDetector.cpp
    src/core/EngineCatalog.cpp
    src/core/HardwareDiagnostics.cpp
    src/core/PerformanceMatrix.cpp
    src/core/ServerBenchmarkMetrics.cpp
    src/core/ModelRoot.cpp
    src/core/ModelRootRegistry.cpp
    src/core/CatalogModel.cpp
    src/core/ModelCatalog.cpp
    src/core/GGUFScanner.cpp
    src/core/OllamaImporter.cpp
    src/core/DocumentExtractor.cpp
    src/core/data/DataLab.cpp
    src/core/MermaidRenderer.cpp
    src/core/ControlApi.cpp
    src/core/AuxiliaryJobScheduler.cpp
    src/core/ModelRoleRegistry.cpp
    src/core/assistant/AssistantRuntime.cpp
    src/core/gateway/LlmGateway.cpp
    src/core/integrations/OpenCodeIntegration.cpp
    src/core/integrations/ClaudeDesktopIntegration.cpp
    src/core/SecretStore.cpp
    src/core/profiles/HarnessSpec.cpp
    src/core/profiles/HarnessEngine.cpp
    src/core/profiles/ProfileTypes.cpp
    src/core/profiles/ProfileManager.cpp
    src/core/profiles/EffectiveProfileBuilder.cpp
    src/core/profiles/ProfileHealthChecker.cpp
    src/core/agent/OpencodeBackend.cpp
    src/core/agent/RawChatBackend.cpp
    src/core/agent/LlamaAgentBackend.cpp
    src/core/agent/McpClient.cpp
    src/core/agent/AgentToolRunner.cpp
    src/core/agent/SubAgentRunner.cpp
    src/core/agent/AgentEventLog.cpp
    src/core/agent/AgentRunStore.cpp
    src/core/agent/AgentDeliverableStore.cpp
    src/core/agent/HarnessEventLog.cpp
    src/core/agent/HarnessCapabilitySnapshot.cpp
    src/core/agent/HarnessEffectLedger.cpp
    src/core/agent/HarnessSandbox.cpp
    src/core/agent/HarnessWorkerProtocol.cpp
    src/core/agent/AgentRoomStore.cpp
    src/core/agent/ToolExecutionSafety.cpp
    src/core/agent/MemoryStore.cpp
    src/core/agent/ToolSurfaceRouter.cpp
    src/core/agent/GraphStore.cpp
    src/core/agent/CodeGraphIndexer.cpp
    src/core/agent/KnowledgePacket.cpp
    src/core/agent/AgentEfficiency.cpp
    src/core/agent/AgentProgressGovernor.cpp
    src/core/agent/DifficultyRouter.cpp
    src/core/agent/HybridPlanning.cpp
    src/core/agent/StructuredSourceView.cpp
    src/core/agent/ProjectBrain.cpp
    src/core/agent/ContextIndex.cpp
    src/core/agent/ContextPreflight.cpp
    src/core/agent/WorkRegistry.cpp
    src/core/agent/MasterCli.cpp
    src/core/agent/ManagedAgentRunStore.cpp
    src/core/agent/BrowserTeach.cpp
    src/core/agent/PortableSkillStore.cpp
    src/core/agent/HarnessDirectiveStore.cpp
    src/core/agent/HotspotAnalyzer.cpp
    src/core/agents/AgentDefinitionStore.cpp
    src/core/agents/TriggerManager.cpp
    src/core/mail/MailClient.cpp
    src/core/voice/VoiceTypes.cpp
    src/core/voice/VoiceCursorCommand.cpp
    src/core/voice/AudioCodec.cpp
    src/core/voice/SttEngine.cpp
    src/core/voice/TtsEngine.cpp
    src/core/voice/TtsPolicy.cpp
    src/core/voice/VoiceAgentPolicy.cpp
    src/core/voice/VadEngine.cpp
    src/core/voice/TurnDetector.cpp
    src/core/voice/VoiceController.cpp
    src/core/voice/VoiceServerManager.cpp
    src/core/voice/VoiceLatencyTracker.cpp
    src/core/voice/CharlaTuning.cpp
    src/core/eval/EvalSuite.cpp
    src/core/eval/BenchmarkPack.cpp
    src/core/ToolCallingSupport.cpp
    src/core/diag/LogTriage.cpp
    src/core/downloads/DownloadHistoryStore.cpp
    src/core/tasks/TaskStore.cpp
    src/core/tasks/TaskSecurityPolicy.cpp
    src/core/tasks/AutomationStore.cpp
    src/core/tasks/RunHistoryStore.cpp
    src/core/tasks/EvidenceBundle.cpp
    src/core/tasks/CronSchedule.cpp
    src/core/tasks/TaskSchedule.cpp
    src/core/tasks/TaskScheduler.cpp
    src/core/tasks/SchedulerDaemonRegistration.cpp
    src/core/tasks/WorkflowEngine.cpp
    src/core/tasks/WorkflowRunner.cpp
    src/core/tasks/WorkflowVisualModel.cpp
    src/core/tasks/EngineeringWorkflowCatalog.cpp
    src/core/automation/DesktopAutomationBackend.cpp
    src/core/automation/DesktopComputerUse.cpp
    src/core/automation/DesktopRecoveryPolicy.cpp
    src/core/automation/VisualMatcher.cpp
    src/core/automation/FuzzyMatch.cpp
    src/core/automation/OcrEngine.cpp
    src/core/automation/OcrTextLocator.cpp
    src/core/automation/AutomationArtifactStore.cpp
    src/core/automation/TeachKeyBuffer.cpp
    src/core/automation/TeachSessionRecorder.cpp
    src/core/automation/AutomationRunner.cpp
    src/core/tuner/AutoTuner.cpp
    src/core/tuner/TunerEngine.cpp
    src/core/tuner/TunerWorker.cpp
    src/core/studia/StudiaIndex.cpp
    src/core/studia/StudiaTexto.cpp
    src/core/studia/StudiaPlot.cpp
    src/core/studia/StudiaEmbed.cpp
    src/core/studia/StudiaEmbedServer.cpp
    src/core/studia/StudiaHerramientas.cpp
    src/core/studia/StudiaPrompt.cpp
    src/core/studia/StudiaSessionStore.cpp
    src/core/studia/StudiaController.cpp
)

set(HEADERS
    src/AppController.h
    src/TrayController.h
    src/ThemeProvider.h
    src/core/LlamaBinary.h
    src/core/BinaryRegistry.h
    src/core/CapabilityDetector.h
    src/core/EngineCatalog.h
    src/core/HardwareDiagnostics.h
    src/core/PerformanceMatrix.h
    src/core/ServerBenchmarkMetrics.h
    src/core/ModelRoot.h
    src/core/ModelRootRegistry.h
    src/core/CatalogModel.h
    src/core/ModelCatalog.h
    src/core/GGUFScanner.h
    src/core/OllamaImporter.h
    src/core/DocumentExtractor.h
    src/core/data/DataLab.h
    src/core/MermaidRenderer.h
    src/core/ControlApi.h
    src/core/AuxiliaryJobScheduler.h
    src/core/ModelRoleRegistry.h
    src/core/assistant/AssistantRuntime.h
    src/core/gateway/LlmGateway.h
    src/core/integrations/OpenCodeIntegration.h
    src/core/integrations/ClaudeDesktopIntegration.h
    src/core/profiles/ProfileTypes.h
    src/core/profiles/ProfileManager.h
    src/core/profiles/EffectiveProfileBuilder.h
    src/core/profiles/ProfileHealthChecker.h
    src/core/agent/AgentTypes.h
    src/core/agent/IAgentBackend.h
    src/core/agent/AgentLifecycle.h
    src/core/agent/OpencodeBackend.h
    src/core/agent/RawChatBackend.h
    src/core/agent/LlamaAgentBackend.h
    src/core/agent/ReasoningWire.h
    src/core/agent/McpClient.h
    src/core/agent/AgentToolRunner.h
    src/core/agent/SubAgentRunner.h
    src/core/agent/AgentEventLog.h
    src/core/agent/AgentRunStore.h
    src/core/agent/AgentDeliverableStore.h
    src/core/agent/HarnessSandbox.h
    src/core/agent/AgentRoomStore.h
    src/core/agent/ToolExecutionSafety.h
    src/core/agent/MemoryStore.h
    src/core/agent/ToolSurfaceRouter.h
    src/core/agent/GraphStore.h
    src/core/agent/CodeGraphIndexer.h
    src/core/agent/KnowledgePacket.h
    src/core/agent/AgentEfficiency.h
    src/core/agent/DifficultyRouter.h
    src/core/agent/HybridPlanning.h
    src/core/agent/StructuredSourceView.h
    src/core/agent/ProjectBrain.h
    src/core/agent/ContextIndex.h
    src/core/agent/ContextPreflight.h
    src/core/agent/WorkRegistry.h
    src/core/agent/BrowserTeach.h
    src/core/agent/ManagedAgentRunStore.h
    src/core/agent/PortableSkillStore.h
    src/core/agent/HotspotAnalyzer.h
    src/core/agents/AgentDefinitionStore.h
    src/core/agents/TriggerManager.h
    src/core/mail/MailClient.h
    src/core/voice/VoiceTypes.h
    src/core/voice/AudioCodec.h
    src/core/voice/SttEngine.h
    src/core/voice/TtsEngine.h
    src/core/voice/TtsPolicy.h
    src/core/voice/VoiceAgentPolicy.h
    src/core/voice/VadEngine.h
    src/core/voice/TurnDetector.h
    src/core/voice/VoiceController.h
    src/core/voice/VoiceServerManager.h
    src/core/voice/VoiceLatencyTracker.h
    src/core/eval/EvalSuite.h
    src/core/eval/BenchmarkPack.h
    src/core/ToolCallingSupport.h
    src/core/downloads/DownloadHistoryStore.h
    src/core/tasks/TaskStore.h
    src/core/tasks/TaskSecurityPolicy.h
    src/core/tasks/AutomationStore.h
    src/core/tasks/RunHistoryStore.h
    src/core/tasks/EvidenceBundle.h
    src/core/tasks/CronSchedule.h
    src/core/tasks/TaskSchedule.h
    src/core/tasks/TaskScheduler.h
    src/core/tasks/SchedulerDaemonRegistration.h
    src/core/tasks/WorkflowEngine.h
    src/core/tasks/WorkflowRunner.h
    src/core/tasks/WorkflowVisualModel.h
    src/core/tasks/EngineeringWorkflowCatalog.h
    src/core/automation/DesktopAutomationBackend.h
    src/core/automation/DesktopComputerUse.h
    src/core/automation/DesktopRecoveryPolicy.h
    src/core/automation/VisualMatcher.h
    src/core/automation/AutomationArtifactStore.h
    src/core/automation/TeachKeyBuffer.h
    src/core/automation/TeachSessionRecorder.h
    src/core/automation/AutomationRunner.h
    src/core/tuner/AutoTuner.h
    src/core/tuner/TunerEngine.h
    src/core/tuner/TunerWorker.h
    src/core/studia/StudiaIndex.h
    src/core/studia/StudiaTexto.h
    src/core/studia/StudiaPlot.h
    src/core/studia/StudiaEmbed.h
    src/core/studia/StudiaEmbedServer.h
    src/core/studia/StudiaHerramientas.h
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
qt_add_resources(llamacode_core "llamacode_core_assets"
    PREFIX "/"
    FILES
        assets/system_profiles.json
)
target_include_directories(llamacode_core PUBLIC src)
target_compile_definitions(llamacode_core PRIVATE
    LC_BUNDLED_SKILLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/skills")
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
if (OpenCV_FOUND)
    target_link_libraries(llamacode_core PUBLIC ${OpenCV_LIBS})
    target_include_directories(llamacode_core PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(llamacode_core PRIVATE LC_HAVE_OPENCV=1)
    message(STATUS "VisualMatcher: OpenCV acceleration enabled (${OpenCV_VERSION})")
else()
    message(STATUS "VisualMatcher: portable Qt matcher (OpenCV not found)")
endif()
if (WIN32)
    # Wtsapi32: sesión interactiva. Ole32/OleAut32: COM + BSTR para UI Automation
    # (árbol de controles del escritorio, DesktopAutomationBackend::controls).
    # RuntimeObject: activación WinRT (RoGetActivationFactory / RoOriginate...)
    # que usa C++/WinRT en OcrEngine (Windows.Media.Ocr). Es la variante para apps
    # de escritorio; windowsapp.lib es el paraguas de UWP y acá no corresponde.
    target_link_libraries(llamacode_core PUBLIC Wtsapi32 Ole32 OleAut32 RuntimeObject)
endif()
if (MSVC)
    target_compile_options(llamacode_core PRIVATE /utf-8 /bigobj)
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

# Debug es el release candidate que se usa a diario. Mantener los símbolos y
# asserts de Debug, pero optimizar el código nativo: las opciones MSVC por
# defecto (/Od + /RTC1) hacen que la GUI y el núcleo sean sensiblemente más
# lentos que Release. /RTC1 es incompatible con /O2, por eso se desactiva sólo
# para esta configuración; el debugger sigue recibiendo PDB y las aserciones
# siguen activas porque no definimos NDEBUG.
if (MSVC)
    foreach(_lc_debug_target IN ITEMS llamacode_core LlamaCode)
        target_compile_options(${_lc_debug_target} PRIVATE
            "$<$<CONFIG:Debug>:/O2>"
            "$<$<CONFIG:Debug>:/Ob2>"
            "$<$<CONFIG:Debug>:/Oi>"
            "$<$<CONFIG:Debug>:/Ot>")
    endforeach()
endif()

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
        qml/components/NavigationPolicy.js
        qml/components/BenchmarkScore.js
        qml/components/PageHeader.qml
        qml/components/CommandPreview.qml
        qml/components/ToolSupportBanner.qml
        qml/components/LcButton.qml
        qml/components/LcComboBox.qml
        qml/components/LcCheckBox.qml
        qml/components/LcSwitch.qml
        qml/components/LcTextField.qml
        qml/components/LcDialog.qml
        qml/components/LcColorPicker.qml
        qml/components/LcColorRow.qml
        qml/components/LcHarnessEditor.qml
        qml/components/LcScrollBar.qml
        qml/components/OpencodeConfigDialog.qml
        qml/components/MermaidPreviewDialog.qml
        qml/components/StudiaTemas.qml
        qml/pages/AgentPage.qml
        qml/pages/AgentsPage.qml
        qml/pages/TasksPage.qml
        qml/pages/BenchmarkPage.qml
        qml/pages/RankingPage.qml
        qml/pages/TunerPage.qml
        qml/pages/ResearchPage.qml
        qml/pages/DataLabPage.qml
        qml/pages/CharlaPage.qml
        qml/pages/DownloadsPage.qml
        qml/pages/StudiaPage.qml
        qml/pages/SettingsPage.qml
    RESOURCES
        assets/best_bolt.svg
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
file(GLOB_RECURSE LLAMACODE_BUNDLED_SKILLS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS
    "assets/skills/*/SKILL.md"
)
# Directivas de ejemplo del harness modular: se copian a la carpeta del usuario
# la primera vez (HarnessDirectiveStore::seedBundledExamples) para que la seccion
# no arranque vacia.
file(GLOB LLAMACODE_BUNDLED_DIRECTIVES
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS
    "assets/harness/directives/*.md"
)

qt_add_resources(LlamaCode "llamacode_assets"
    PREFIX "/"
    FILES
        assets/app_icon.png
        assets/app_icon.ico
        assets/debug_icon.ico
        assets/tray_icon.png
        assets/update/latest.json
        assets/hwfit/hf_models.json
        assets/chat-templates/gemma4-tools-fixed.jinja
        assets/chat-templates/kat-coder-tools.jinja
        assets/chat-templates/poolside-Laguna-S-2.1.jinja
        assets/chat-templates/laguna-tools-v24.jinja
        assets/chat-templates/qwen38-tools-fixed.jinja
        assets/benchmarks/aa_intelligence.json
        assets/benchmarks/local_cookbook_priorities.json
        assets/benchmarks/server_speed_v1.json
        assets/eval/reasoning_bias.json
        assets/eval/snake_retro_singlefile.json
        tools/pocket_tts_server.py
        docs/benchmark-results.md
        docs/benchmark-results-history.md
        ${LLAMACODE_BUNDLED_CUSTOM_BENCHMARKS}
        ${LLAMACODE_BUNDLED_SKILLS}
        ${LLAMACODE_BUNDLED_DIRECTIVES}
)

target_include_directories(LlamaCode PRIVATE src)

target_link_libraries(LlamaCode
    PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
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
        qt_add_executable(${name} ${file} ${ARGN})
        # Subsistema consola: si no, QtTest no imprime a stdout (output perdido).
        set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
        if (MSVC)
            target_compile_options(${name} PRIVATE /utf-8)
            target_link_options(${name} PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
        endif()
        target_include_directories(${name} PRIVATE src tests)
        target_link_libraries(${name} PRIVATE llamacode_core Qt6::Test)
        add_test(NAME ${name} COMMAND ${name})
        # Todos los QtTests deben ser ejecutables sin escritorio. Los probes que
        # requieren foreground no se registran como tests del gate.
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1")
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
    add_lc_test(test_profile_health     tests/test_profile_health.cpp)
    add_lc_test(test_profiles           tests/test_profiles.cpp)
    add_lc_test(test_agent_profiles     tests/test_agent_profiles.cpp)
    add_lc_test(test_harness_spec       tests/test_harness_spec.cpp)
    add_lc_test(test_harness_modules    tests/test_harness_modules.cpp)
    add_lc_test(test_harness_engine     tests/test_harness_engine.cpp)
    add_lc_test(test_harness_event_log  tests/test_harness_event_log.cpp)
    add_lc_test(test_harness_capabilities tests/test_harness_capabilities.cpp)
    add_lc_test(test_harness_effects    tests/test_harness_effects.cpp)
    add_lc_test(test_agent_runs         tests/test_agent_runs.cpp)
    add_lc_test(test_harness_worker_protocol tests/test_harness_worker_protocol.cpp)
    add_lc_test(test_agent_definitions  tests/test_agent_definitions.cpp)
    add_lc_test(test_agent_context_budget tests/test_agent_context_budget.cpp)
    add_lc_test(test_agent_efficiency    tests/test_agent_efficiency.cpp)
    add_lc_test(test_agent_progress      tests/test_agent_progress.cpp)
    add_lc_test(test_agent_rooms         tests/test_agent_rooms.cpp)
    add_lc_test(test_agent_project_dir   tests/test_agent_project_dir.cpp)
    add_lc_test(test_system_profiles    tests/test_system_profiles.cpp)
    add_lc_test(test_secret_store       tests/test_secret_store.cpp)
    add_lc_test(test_registries         tests/test_registries.cpp)
    add_lc_test(test_catalog            tests/test_catalog.cpp)
    add_lc_test(test_capability         tests/test_capability.cpp)
    add_lc_test(test_engine_catalog     tests/test_engine_catalog.cpp)
    add_lc_test(test_document_extractor tests/test_document_extractor.cpp)
    add_lc_test(test_datalab             tests/test_datalab.cpp tests/test_datalab.h)
    add_lc_test(test_memory_graph       tests/test_memory_graph.cpp)
    add_lc_test(test_work_registry      tests/test_work_registry.cpp)
    add_lc_test(test_code_graph         tests/test_code_graph.cpp)
    add_lc_test(test_context_index      tests/test_context_index.cpp)
     add_lc_test(test_tuner              tests/test_tuner.cpp)
     add_lc_test(test_server_benchmark_metrics tests/test_server_benchmark_metrics.cpp)
     add_lc_test(test_long_context_probe tests/test_long_context_probe.cpp
                tests/long_context_probe.cpp)
    add_lc_test(test_hardware_diagnostics tests/test_hardware_diagnostics.cpp)
    add_lc_test(test_eval               tests/test_eval.cpp)
    # Ruta de la suite bundleada para validar el JSON real (no sólo el sample inline).
    target_compile_definitions(test_eval PRIVATE
        LC_REASONING_BIAS_JSON="${CMAKE_CURRENT_SOURCE_DIR}/assets/eval/reasoning_bias.json"
        LC_SNAKE_SUITE_JSON="${CMAKE_CURRENT_SOURCE_DIR}/assets/eval/snake_retro_singlefile.json")
    add_lc_test(test_control_api        tests/test_control_api.cpp)
    add_lc_test(test_auxiliary_job_scheduler tests/test_auxiliary_job_scheduler.cpp)
    add_lc_test(test_model_role_registry tests/test_model_role_registry.cpp)
    add_lc_test(test_tool_surface_router tests/test_tool_surface_router.cpp)
    add_lc_test(test_assistant_runtime tests/test_assistant_runtime.cpp)
    add_lc_test(test_agent_tools        tests/test_agent_tools.cpp)
    add_lc_test(test_portable_skills    tests/test_portable_skills.cpp)
    add_lc_test(test_agent_wire         tests/test_agent_wire.cpp)
    add_lc_test(test_hotspots           tests/test_hotspots.cpp)
    add_lc_test(test_master_cli         tests/test_master_cli.cpp)
    add_lc_test(test_managed_agent_runs tests/test_managed_agent_runs.cpp)
    add_lc_test(test_backends_net       tests/test_backends_net.cpp)
    add_lc_test(test_appcontroller      tests/test_appcontroller.cpp)
    add_lc_test(test_mermaid            tests/test_mermaid.cpp)
    add_lc_test(test_mail               tests/test_mail.cpp)
    add_lc_test(test_voice              tests/test_voice.cpp)
    add_lc_test(test_tasks              tests/test_tasks.cpp)
    add_lc_test(test_engineering_workflows tests/test_engineering_workflows.cpp)
    add_lc_test(test_task_security_policy tests/test_task_security_policy.cpp)
    add_lc_test(test_automation         tests/test_automation.cpp)
    add_lc_test(test_visual_matcher     tests/test_visual_matcher.cpp)
    add_lc_test(test_cron               tests/test_cron.cpp)
    add_lc_test(test_automation_store   tests/test_automation_store.cpp)
    add_lc_test(test_run_history        tests/test_run_history.cpp)
    add_lc_test(test_evidence_bundle    tests/test_evidence_bundle.cpp)
    add_lc_test(test_tool_execution_safety tests/test_tool_execution_safety.cpp)
    add_lc_test(test_download_history   tests/test_download_history.cpp)
    add_lc_test(test_theme              tests/test_theme.cpp)
    add_lc_test(test_toolcalling        tests/test_toolcalling.cpp)
    add_lc_test(test_gateway            tests/test_gateway.cpp)
    add_lc_test(test_logtriage          tests/test_logtriage.cpp)
    add_lc_test(test_fuzzy_match        tests/test_fuzzy_match.cpp)
    add_lc_test(test_ocr_locator        tests/test_ocr_locator.cpp)
    add_lc_test(test_difficulty_router  tests/test_difficulty_router.cpp)

    # ── Test de componentes QML (sin C++) ──
    # Corre el runtime `qml` sobre tests/qml/tst_*.qml en offscreen. Los
    # componentes REALES se copian al lado de los stubs de Theme/App (que en el
    # app son context properties de C++): un archivo QML resuelve los nombres
    # sin calificar en SU directorio, asi que tienen que convivir.
    get_target_property(_lc_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_lc_qt_bin "${_lc_qmake_exe}" DIRECTORY)
    set(LC_QML_HARNESS_DIR "${CMAKE_CURRENT_BINARY_DIR}/qml_harness")
    set(LC_QML_HARNESS_SOURCES
        tests/qml/qmldir
        tests/qml/Theme.qml
        tests/qml/App.qml
        tests/qml/PageHeader.qml
        tests/qml/LcDialog.qml
        tests/qml/LcButton.qml
        tests/qml/LcTextField.qml
        tests/qml/tst_color_picker.qml
        tests/qml/tst_navigation_policy.qml
        tests/qml/tst_benchmark_score.qml
        tests/qml/tst_datalab.qml
        tests/qml/LcComboBox.qml
        tests/qml/LcSwitch.qml
        tests/qml/tst_harness_editor.qml
        qml/components/LcHarnessEditor.qml
        qml/components/NavigationPolicy.js
        qml/components/BenchmarkScore.js
        qml/pages/DataLabPage.qml
        qml/components/LcColorPicker.qml
        qml/components/LcColorRow.qml
    )
    set(_lc_qml_harness_copies)
    foreach(_src ${LC_QML_HARNESS_SOURCES})
        get_filename_component(_name "${_src}" NAME)
        add_custom_command(
            OUTPUT "${LC_QML_HARNESS_DIR}/${_name}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${_src}" "${LC_QML_HARNESS_DIR}/${_name}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}"
            COMMENT "Staging ${_name} for the QML harness")
        list(APPEND _lc_qml_harness_copies "${LC_QML_HARNESS_DIR}/${_name}")
    endforeach()

    # Modulo "LlamaCode" del harness: una PAGINA real (qml/pages/*) resuelve los
    # componentes Lc* con `import LlamaCode 1.0`, porque dentro del qrc viven en
    # otro directorio. Para que esos archivos corran bajo el runtime `qml` hace
    # falta un modulo importable, y Qt lo busca en <importpath>/LlamaCode/qmldir:
    # se stagean los stubs tambien ahi, con un qmldir que declara el modulo.
    set(LC_QML_HARNESS_MODULE_DIR "${LC_QML_HARNESS_DIR}/LlamaCode")
    set(LC_QML_HARNESS_MODULE_SOURCES
        tests/qml/Theme.qml
        tests/qml/App.qml
        tests/qml/LcDialog.qml
        tests/qml/LcButton.qml
        tests/qml/LcTextField.qml
        tests/qml/LcComboBox.qml
        tests/qml/LcSwitch.qml
        tests/qml/PageHeader.qml
    )
    add_custom_command(
        OUTPUT "${LC_QML_HARNESS_MODULE_DIR}/qmldir"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/module_qmldir"
                "${LC_QML_HARNESS_MODULE_DIR}/qmldir"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/module_qmldir"
        COMMENT "Staging qmldir for the QML harness module")
    list(APPEND _lc_qml_harness_copies "${LC_QML_HARNESS_MODULE_DIR}/qmldir")
    foreach(_src ${LC_QML_HARNESS_MODULE_SOURCES})
        get_filename_component(_name "${_src}" NAME)
        add_custom_command(
            OUTPUT "${LC_QML_HARNESS_MODULE_DIR}/${_name}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${_src}" "${LC_QML_HARNESS_MODULE_DIR}/${_name}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}"
            COMMENT "Staging ${_name} for the QML harness module")
        list(APPEND _lc_qml_harness_copies "${LC_QML_HARNESS_MODULE_DIR}/${_name}")
    endforeach()

    add_custom_target(qml_harness ALL DEPENDS ${_lc_qml_harness_copies})

    add_test(NAME qml_color_picker
             COMMAND "${_lc_qt_bin}/qml" -I "${LC_QML_HARNESS_DIR}"
                     "${LC_QML_HARNESS_DIR}/tst_color_picker.qml")
    set_tests_properties(qml_color_picker PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
        FAIL_REGULAR_EXPRESSION "FAIL |ReferenceError|Unable to assign")

    add_test(NAME qml_navigation_policy
             COMMAND "${_lc_qt_bin}/qml" -I "${LC_QML_HARNESS_DIR}"
                     "${LC_QML_HARNESS_DIR}/tst_navigation_policy.qml")
    set_tests_properties(qml_navigation_policy PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
        FAIL_REGULAR_EXPRESSION "FAIL |ReferenceError|Unable to assign")

    add_test(NAME qml_benchmark_score
             COMMAND "${_lc_qt_bin}/qml" -I "${LC_QML_HARNESS_DIR}"
                     "${LC_QML_HARNESS_DIR}/tst_benchmark_score.qml")
    set_tests_properties(qml_benchmark_score PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
        FAIL_REGULAR_EXPRESSION "FAIL |ReferenceError|Unable to assign")

    add_test(NAME qml_datalab
             COMMAND "${_lc_qt_bin}/qml" -I "${LC_QML_HARNESS_DIR}"
                     "${LC_QML_HARNESS_DIR}/tst_datalab.qml")
    set_tests_properties(qml_datalab PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
        FAIL_REGULAR_EXPRESSION "FAIL |ReferenceError|Unable to assign")

    add_test(NAME qml_harness_editor
             COMMAND "${_lc_qt_bin}/qml" -I "${LC_QML_HARNESS_DIR}"
                     "${LC_QML_HARNESS_DIR}/tst_harness_editor.qml")
    set_tests_properties(qml_harness_editor PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
        FAIL_REGULAR_EXPRESSION "FAIL |ReferenceError|Unable to assign")

    # Harness de QA manual del OCR: la prueba de pantalla necesita escritorio vivo
    # y paquete de idioma, pero el contrato CLI de ayuda sí es headless-testable.
    # La ayuda debe salir antes de tocar OCR/UIA; la validación visual se corre a
    # mano con una app abierta. Ver "QA manual pendiente" en CLAUDE.md.
    qt_add_executable(qa_ocr_probe tests/qa_ocr_probe.cpp)
    set_target_properties(qa_ocr_probe PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(qa_ocr_probe PRIVATE /utf-8)
        target_link_options(qa_ocr_probe PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
    endif()
    target_include_directories(qa_ocr_probe PRIVATE src tests)
    target_link_libraries(qa_ocr_probe PRIVATE llamacode_core)
    add_test(NAME qa_ocr_probe_help COMMAND qa_ocr_probe --help)
    set_tests_properties(qa_ocr_probe_help PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1")
    add_test(NAME qa_ocr_probe_offscreen_guard COMMAND qa_ocr_probe Archivo)
    set_tests_properties(qa_ocr_probe_offscreen_guard PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
        WILL_FAIL TRUE)
    if (WIN32)
        set_property(TEST qa_ocr_probe_help PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:qa_ocr_probe>"
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:llamacode_core>"
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
        set_property(TEST qa_ocr_probe_offscreen_guard PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:qa_ocr_probe>"
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:llamacode_core>"
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()

    qt_add_executable(qa_visual_automation tests/qa_visual_automation.cpp)
    set_target_properties(qa_visual_automation PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(qa_visual_automation PRIVATE /utf-8)
        target_link_options(qa_visual_automation PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
    endif()
    target_include_directories(qa_visual_automation PRIVATE src tests)
    target_link_libraries(qa_visual_automation PRIVATE llamacode_core)

    # Probe opt-in de proveedores web reales. No entra en ctest porque requiere
    # Camofox o Playwright MCP instalados por el operador.
    qt_add_executable(qa_web_providers tests/qa_web_providers.cpp)
    set_target_properties(qa_web_providers PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(qa_web_providers PRIVATE /utf-8)
        target_link_options(qa_web_providers PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
    endif()
    target_include_directories(qa_web_providers PRIVATE src tests)
    target_link_libraries(qa_web_providers PRIVATE llamacode_core)

    # Opt-in probe against a real llama-server. It is intentionally not an
    # add_test(): it needs a running model/server and may allocate 128K prompts.
    qt_add_executable(qa_kv_cache tests/qa_kv_cache.cpp tests/long_context_probe.cpp)
    set_target_properties(qa_kv_cache PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(qa_kv_cache PRIVATE /utf-8)
        target_link_options(qa_kv_cache PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
    endif()
    target_include_directories(qa_kv_cache PRIVATE src tests)
    target_link_libraries(qa_kv_cache PRIVATE llamacode_core)

    # Probe opt-in de concurrencia para decidir si un workload auxiliar (STT,
    # embeddings, juez o resumen) debe compartir servidor/proceso. No entra en
    # ctest: requiere un llama-server y un modelo elegidos por el operador.
    qt_add_executable(qa_auxiliary_concurrency tests/qa_auxiliary_concurrency.cpp)
    set_target_properties(qa_auxiliary_concurrency PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MSVC)
        target_compile_options(qa_auxiliary_concurrency PRIVATE /utf-8)
        target_link_options(qa_auxiliary_concurrency PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
    endif()
    target_include_directories(qa_auxiliary_concurrency PRIVATE src tests)
    target_link_libraries(qa_auxiliary_concurrency PRIVATE llamacode_core)
    add_lc_test(test_studia             tests/test_studia.cpp)

    # Ingestor de StudIA: es Python (usa markitdown/pypdf), asi que su suite es
    # unittest y se engancha a ctest como un test mas. Si no hay Python en el
    # sistema, no se registra (el resto de la suite sigue corriendo).
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        add_test(NAME test_studia_ingest
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/studia/test_ingest.py)
        add_test(NAME test_studia_vectorizar
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/studia/test_vectorizar.py)
        add_test(NAME test_studia_graficar
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/studia/test_graficar.py)
    else()
        message(STATUS "Python3 no encontrado: se omite test_studia_ingest")
    endif()
endif()
