// EyeController.cpp
#include "EyeController.hpp"

namespace physics {

bool EyeController::initialize(PsdAssembly* assembly) {
    assembly_ = assembly;
    if (!assembly_) {
        return false;
    }

    eyeNameToLayer_.clear();
    eyeNameToLayer_["closed"] = "eye_closed";
    eyeNameToLayer_["question"] = "eye_question";
    eyeNameToLayer_["exclamation"] = "eye_exclamation";
    eyeNameToLayer_["basic"] = "eye_basic";
    eyeNameToLayer_["annoyed"] = "eye_annoyed";
    eyeNameToLayer_["happy"] = "eye_happy";

    buildEyeLayerList();

    currentEye_ = "basic";
    blinkBaseEye_ = "basic";
    loadingFrame_ = 0;
    loadingAccum_ = 0.0;
    blinkCycleAccum_ = 0.0;
    blinkClosedAccum_ = 0.0;
    blinkClosedPhase_ = false;

    applyCurrentVisual();
    return true;
}

void EyeController::buildEyeLayerList() {
    allEyeLayers_.clear();

    allEyeLayers_.push_back("eye_closed");
    allEyeLayers_.push_back("eye_question");
    allEyeLayers_.push_back("eye_exclamation");
    allEyeLayers_.push_back("eye_basic");
    allEyeLayers_.push_back("eye_annoyed");
    allEyeLayers_.push_back("eye_happy");

    for (int i = 0; i < 8; ++i) {
        allEyeLayers_.push_back("eye_loading_" + std::to_string(i));
    }
}

void EyeController::hideAllEyes() {
    if (!assembly_) {
        return;
    }

    for (const std::string& name : allEyeLayers_) {
        assembly_->setItemVisible(name, false);
    }
}

void EyeController::showAllLoadingExcept(int hiddenIndex) {
    if (!assembly_) {
        return;
    }

    for (int i = 0; i < 8; ++i) {
        const std::string layerName = "eye_loading_" + std::to_string(i);
        assembly_->setItemVisible(layerName, i != hiddenIndex);
    }

    // Hide all non-loading eye layers while loading is active.
    assembly_->setItemVisible("eye_closed", false);
    assembly_->setItemVisible("eye_question", false);
    assembly_->setItemVisible("eye_exclamation", false);
    assembly_->setItemVisible("eye_basic", false);
    assembly_->setItemVisible("eye_annoyed", false);
    assembly_->setItemVisible("eye_happy", false);
}

void EyeController::showSingleEyeLayer(const std::string& fullLayerName) {
    if (!assembly_) {
        return;
    }

    assembly_->setOnlyVisible(allEyeLayers_, fullLayerName);
}

bool EyeController::pickEye(const std::string& type) {
    if (!assembly_) {
        return false;
    }

    if (type == "loading") {
        currentEye_ = "loading";
        loadingFrame_ = 0;
        loadingAccum_ = 0.0;
        blinkClosedPhase_ = false;
        blinkCycleAccum_ = 0.0;
        blinkClosedAccum_ = 0.0;
        applyCurrentVisual();
        return true;
    }

    auto it = eyeNameToLayer_.find(type);
    if (it == eyeNameToLayer_.end()) {
        return false;
    }

    currentEye_ = type;
    blinkBaseEye_ = type;
    blinkClosedPhase_ = false;
    blinkCycleAccum_ = 0.0;
    blinkClosedAccum_ = 0.0;
    loadingFrame_ = 0;
    loadingAccum_ = 0.0;

    applyCurrentVisual();
    return true;
}

void EyeController::applyCurrentVisual() {
    if (!assembly_) {
        return;
    }

    if (currentEye_ == "loading") {
        showAllLoadingExcept(loadingFrame_);
        return;
    }

    if (blinkClosedPhase_) {
        showSingleEyeLayer("eye_closed");
        return;
    }

    auto it = eyeNameToLayer_.find(currentEye_);
    if (it != eyeNameToLayer_.end()) {
        showSingleEyeLayer(it->second);
    }
}

void EyeController::update(double dtSeconds) {
    if (!assembly_) {
        return;
    }

    if (currentEye_ == "loading") {
        loadingAccum_ += dtSeconds;
        while (loadingAccum_ >= kLoadingFrameSeconds) {
            loadingAccum_ -= kLoadingFrameSeconds;
            loadingFrame_ = (loadingFrame_ + 1) % 8;
            applyCurrentVisual();
        }
        return;
    }

    // no blink for closed
    if (currentEye_ == "closed") {
        return;
    }

    if (!blinkClosedPhase_) {
        blinkCycleAccum_ += dtSeconds;
        if (blinkCycleAccum_ >= kBlinkIntervalSeconds) {
            blinkCycleAccum_ = 0.0;
            blinkClosedAccum_ = 0.0;
            blinkClosedPhase_ = true;
            applyCurrentVisual();
        }
    } else {
        blinkClosedAccum_ += dtSeconds;
        if (blinkClosedAccum_ >= kBlinkClosedSeconds) {
            blinkClosedAccum_ = 0.0;
            blinkClosedPhase_ = false;
            applyCurrentVisual();
        }
    }
}

} // namespace physics