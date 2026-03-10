#include "AppRuntime.hpp"

#include <GLFW/glfw3.h>
#include <iostream>
#include <string>

int main() {
    std::cout << "Running.\n";

    if (!glfwInit()) {
        return 1;
    }

    int exit_code = 0;

    {
        AppRuntime app("default_app_config.json", "saves");

        std::string error;
        if (!app.initialize(error)) {
            std::cerr << error << "\n";
            glfwTerminate();
            return 1;
        }

        exit_code = app.run();
        app.shutdown();
    }

    glfwTerminate();
    return exit_code;
}