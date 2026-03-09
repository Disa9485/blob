// RoomManager.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <glad/glad.h>

namespace rooms {

class RoomManager {
public:
    RoomManager() = default;
    ~RoomManager();

    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    bool initialize(const std::string& roomsDirectory, std::string& error);
    void shutdown();

    std::vector<std::string> roomIds() const;
    bool hasRoom(const std::string& roomId) const;

    bool setCurrentRoom(const std::string& roomId, std::string& error);
    const std::string& currentRoomId() const;

    GLuint currentTexture() const;
    int currentWidth() const;
    int currentHeight() const;

private:
    struct RoomTexture {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        std::string path;
    };

    static bool loadTextureFromPng(
        const std::string& path,
        GLuint& texture,
        int& width,
        int& height,
        std::string& error
    );

    void destroyAllTextures();

    std::unordered_map<std::string, RoomTexture> rooms_;
    std::vector<std::string> orderedRoomIds_;
    std::string currentRoomId_;
};

} // namespace rooms