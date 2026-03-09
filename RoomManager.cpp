// RoomManager.cpp
#include "RoomManager.hpp"

#include <algorithm>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace rooms {

namespace fs = std::filesystem;

RoomManager::~RoomManager() {
    shutdown();
}

bool RoomManager::initialize(const std::string& roomsDirectory, std::string& error) {
    shutdown();

    if (!fs::exists(roomsDirectory)) {
        error = "Rooms directory does not exist: " + roomsDirectory;
        return false;
    }

    if (!fs::is_directory(roomsDirectory)) {
        error = "Rooms path is not a directory: " + roomsDirectory;
        return false;
    }

    for (const auto& entry : fs::directory_iterator(roomsDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const fs::path path = entry.path();
        if (path.extension() != ".png") {
            continue;
        }

        const std::string roomId = path.stem().string();
        if (roomId.empty()) {
            continue;
        }

        if (rooms_.find(roomId) != rooms_.end()) {
            error = "Duplicate room id found: " + roomId;
            shutdown();
            return false;
        }

        GLuint tex = 0;
        int w = 0;
        int h = 0;
        std::string texError;
        if (!loadTextureFromPng(path.string(), tex, w, h, texError)) {
            error = "Failed to load room '" + roomId + "': " + texError;
            shutdown();
            return false;
        }

        RoomTexture room;
        room.texture = tex;
        room.width = w;
        room.height = h;
        room.path = path.string();

        rooms_.emplace(roomId, room);
        orderedRoomIds_.push_back(roomId);
    }

    std::sort(orderedRoomIds_.begin(), orderedRoomIds_.end());

    if (orderedRoomIds_.empty()) {
        error = "No .png room files found in: " + roomsDirectory;
        return false;
    }

    currentRoomId_ = orderedRoomIds_.front();
    return true;
}

void RoomManager::shutdown() {
    destroyAllTextures();
    rooms_.clear();
    orderedRoomIds_.clear();
    currentRoomId_.clear();
}

std::vector<std::string> RoomManager::roomIds() const {
    return orderedRoomIds_;
}

bool RoomManager::hasRoom(const std::string& roomId) const {
    return rooms_.find(roomId) != rooms_.end();
}

bool RoomManager::setCurrentRoom(const std::string& roomId, std::string& error) {
    if (!hasRoom(roomId)) {
        error = "Unknown room id: " + roomId;
        return false;
    }

    currentRoomId_ = roomId;
    return true;
}

const std::string& RoomManager::currentRoomId() const {
    return currentRoomId_;
}

GLuint RoomManager::currentTexture() const {
    auto it = rooms_.find(currentRoomId_);
    return (it != rooms_.end()) ? it->second.texture : 0;
}

int RoomManager::currentWidth() const {
    auto it = rooms_.find(currentRoomId_);
    return (it != rooms_.end()) ? it->second.width : 0;
}

int RoomManager::currentHeight() const {
    auto it = rooms_.find(currentRoomId_);
    return (it != rooms_.end()) ? it->second.height : 0;
}

bool RoomManager::loadTextureFromPng(
    const std::string& path,
    GLuint& texture,
    int& width,
    int& height,
    std::string& error
) {
    texture = 0;
    width = 0;
    height = 0;

    stbi_set_flip_vertically_on_load(0);

    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        error = "stbi_load failed for: " + path;
        return false;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels
    );

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
    return true;
}

void RoomManager::destroyAllTextures() {
    for (auto& [id, room] : rooms_) {
        if (room.texture != 0) {
            glDeleteTextures(1, &room.texture);
            room.texture = 0;
        }
    }
}

} // namespace rooms