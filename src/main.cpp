#include "app.hpp"

#include <cstdio>
#include <exception>

int main() {
    try {
        return vig::run_app();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
