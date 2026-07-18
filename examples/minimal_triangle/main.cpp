// The minimal reference app - see examples/minimal_triangle/CMakeLists.txt's own comment.
// Genuinely load-bearing entry point (ARCHITECTURE.md section 2), hence .cpp rather than .hpp.

#include "konative/app/application.hpp"

namespace {

class MinimalTriangleApp final : public konative::app::Application {
public:
    void on_started() override {
        // Real apps install systems/components here via world().registry()/world().systems().
    }
};

} // namespace

namespace konative::app {

Application& create_application() {
    static MinimalTriangleApp instance;
    return instance;
}

} // namespace konative::app

int main() {
    konative::app::Application& app = konative::app::create_application();
    app.start();
    app.tick(0.0F);
    app.destroy();
    return 0;
}
