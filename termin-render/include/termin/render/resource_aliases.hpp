#pragma once

#include <string>

namespace termin {

enum class AttachmentKind {
    Color,
    Depth,
};

struct ResourceView {
    std::string parent;
    AttachmentKind attachment = AttachmentKind::Color;
};

struct FboComposition {
    std::string color;
    std::string depth;
};

} // namespace termin

