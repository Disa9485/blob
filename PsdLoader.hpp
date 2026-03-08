// PsdLoader.hpp
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct LayerImageRGBA {
    std::string name;                 // UTF-8 layer name
    int left = 0, top = 0, right = 0, bottom = 0;  // layer bounds in canvas space
    int width = 0;                    // canvas width
    int height = 0;                   // canvas height
    std::vector<std::uint8_t> rgba;   // width*height*4, interleaved RGBA8
};

struct Psd {
    int canvasWidth = 0;
    int canvasHeight = 0;
    int bitsPerChannel = 0;           // should be 8 for your pipeline
    std::unordered_map<std::string, LayerImageRGBA> layersByName;
};

class PsdLoader {
public:
    // Loads a PSD from disk and extracts each layer as RGBA8 expanded to canvas size.
    // Returns false on failure, with error text filled.
    static bool LoadPsd(const std::string& psdPath, Psd& out, std::string& error);
};