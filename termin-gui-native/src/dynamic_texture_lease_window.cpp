#include <termin/gui_native/dynamic_texture_lease.hpp>

#include <termin/gui_native/application_host.hpp>

namespace termin::gui_native {

DynamicTextureLease::DynamicTextureLease(GuiApplicationHost& host)
    : DynamicTextureLease(host.texture_lease_state()) {}

DynamicTextureLease::DynamicTextureLease(GuiWindowHost& host)
    : DynamicTextureLease(host.application_host()) {}

} // namespace termin::gui_native
