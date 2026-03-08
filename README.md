### Description
- This project implements a chat interface with a locally run llm. Model must be gguf and is initialized/run with llama.cpp.
- The llm's responses are converted to speech and played using PiperTTS and voice onnx models. The audio engine is built in OpenAL.
- A launcher with a save selector is also implemented in order to have multiple sessions with different model settings and chat histories.
- The chat interface runs in OpenGL and we use GLFW3 and Glad for streamlining the window and ImGui for the UI.
- Also it also implements a retrieval augmented generation system to give the LLM a long term memory and it can retrieve memories of both the user and its chat's based on the user's prompt. This system uses llama.cpp to generate the embeddings and faiss to retrieve them. The memory database is managed with SQL.
- Also we implement chipmunk 2D physics engine and use PSD image layers to build and load 2D scenes with physics. PSD SDK is used to load PSD layers and Delaunator is used to automatically triangulate the bodies from the layers to seperate the translucent pixels.
- The user can interact with parts of the scene which will tell the LLM it was interacted with, giving it a kind of physical body in a world space. A config file will determine how a part reacts under physics and we can build both soft bodies and rigid bodies.

### To build, in blob/build/Release do:
cmake --build .. --config Release && blob.exe

### Requirements:
- OpenGL
- glfw3
- glad
- ImGui
- Chipmunk
- Psd SDK
- delaunator
- sqlite3
- nlohmann json
- OpenAL
- piper
- onnxruntime
- llama.cpp
- faiss