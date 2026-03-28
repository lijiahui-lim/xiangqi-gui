#include "gui.h"
#include <iostream>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    GUI gui;
    if (!gui.init()) {
        std::cerr << "初始化失败: " << SDL_GetError() << std::endl;
        return 1;
    }

    gui.run();
    return 0;
}
