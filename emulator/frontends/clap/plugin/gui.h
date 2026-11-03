#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "panel_layout.h"

namespace mmt8 {

class Engine;

/** Calls the GUI makes back into the plugin (all on the main thread). */
struct GuiHost {
    std::function<void()> params_changed;  // a parameter was changed from the panel
    std::function<void()> state_changed;   // the project needs saving (ROM swapped, ...)
    std::function<bool(uint32_t, uint32_t)> request_resize;
};

/** The replica front panel, embedded in the host's window. Main thread only. */
class Gui {
public:
    static constexpr uint32_t kDefaultWidth = static_cast<uint32_t>(kPanelW);
    static constexpr uint32_t kMinWidth = 600;
    static constexpr uint32_t kMaxWidth = 3000;

    static bool supported();
    static std::unique_ptr<Gui> create(Engine &engine, GuiHost host);
    static uint32_t height_for(uint32_t width) {
        return static_cast<uint32_t>(static_cast<float>(width) * kPanelH / kPanelW + 0.5f);
    }

    virtual ~Gui() = default;
    virtual bool set_parent(void *native_window) = 0;
    virtual void set_scale(double scale) = 0;
    virtual double scale() const = 0;
    virtual void set_size(uint32_t width, uint32_t height) = 0;
    virtual void show(bool visible) = 0;
};

}  // namespace mmt8
