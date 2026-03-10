// EyeController.hpp
#pragma once

#include "PsdAssembler.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace physics {

class EyeController {
public:
    bool initialize(PsdAssembly* assembly);

    // user-facing API
    bool pickEye(const std::string& type); // "question", "loading", "closed", "basic", etc.
    void update(double dtSeconds);

    std::string currentEye() const { return currentEye_; }

private:
    void applyCurrentVisual();
    void showSingleEyeLayer(const std::string& fullLayerName);
    void hideAllEyes();
    void showAllLoadingExcept(int hiddenIndex);
    void buildEyeLayerList();

private:
    PsdAssembly* assembly_ = nullptr;

    std::unordered_map<std::string, std::string> eyeNameToLayer_;
    std::vector<std::string> allEyeLayers_;

    std::string currentEye_ = "loading";
    std::string blinkBaseEye_ = "basic";

    // loading animation
    int loadingFrame_ = 0;
    double loadingAccum_ = 0.0;

    // blink animation
    double blinkCycleAccum_ = 0.0;
    double blinkClosedAccum_ = 0.0;
    bool blinkClosedPhase_ = false;

    static constexpr double kLoadingFrameSeconds = 0.25; // 500ms
    static constexpr double kBlinkIntervalSeconds = 10.0;
    static constexpr double kBlinkClosedSeconds = 0.25;  // 500ms
};

} // namespace physics