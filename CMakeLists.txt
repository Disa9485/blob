cmake_minimum_required(VERSION 3.20)
project(blob)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(unofficial-chipmunk CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(faiss CONFIG REQUIRED)

set(LLAMA_STANDALONE OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    glad::glad
    opengl32
)

add_subdirectory(third_party/llama.cpp)
add_subdirectory(third_party/psd_sdk/src/Psd)

# SQLite
add_library(sqlite3 STATIC
    third_party/sqlite/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC
    third_party/sqlite
)

# Piper
add_library(piper SHARED IMPORTED)
set_target_properties(piper PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/third_party/piper/lib/piper.lib"
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/piper/bin/piper.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/piper/include"
)

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/third_party/piper/lib/onnxruntime.lib"
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/piper/bin/onnxruntime.dll"
)

add_executable(blob
    main.cpp
    AppConfig.cpp
    SaveManager.cpp
    SaveLauncherPanel.cpp
    LlamaChat.cpp
    LlamaChatLoader.cpp
    EmbeddingModel.cpp
    ConversationMemoryStore.cpp
    ConversationMemoryService.cpp
    ImGuiLayer.cpp
    ChatPanel.cpp
    AudioEngine.cpp
    SentenceDetector.cpp
    FaissMemoryIndex.cpp
    PiperTts.cpp
    SpeechPipeline.cpp
    VoiceEffects.cpp
    PhysicsBodyFactory.cpp
    PhysicsScene.cpp
    GameRenderer.cpp
    RenderRegistry.cpp
    delaunator.cpp
    PsdLoader.cpp
    SceneConfig.cpp
    PsdAssembler.cpp
    SoftBodyInteractor.cpp
)

target_include_directories(blob PRIVATE
    .
    third_party/psd_sdk/src/Psd
    third_party/sqlite
)

target_link_libraries(blob PRIVATE
    glfw
    glad::glad
    opengl32
    unofficial::chipmunk::chipmunk
    Psd
    llama
    imgui
    sqlite3
    OpenAL::OpenAL
    faiss
    piper
    onnxruntime
)

# keep your game on C++20
set_property(TARGET blob PROPERTY CXX_STANDARD 20)

# force llama.cpp library to build as C++17
set_property(TARGET llama PROPERTY CXX_STANDARD 17)