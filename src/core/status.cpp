#include "glyph/core/status.hpp"

namespace glyph {

const char* status_name(const Status status) noexcept {
    switch (status) {
    case Status::ok:
        return "ok";
    case Status::invalid_argument:
        return "invalid_argument";
    case Status::io:
        return "io";
    case Status::protocol:
        return "protocol";
    case Status::integrity:
        return "integrity";
    case Status::crypto:
        return "crypto";
    case Status::camera:
        return "camera";
    case Status::display:
        return "display";
    case Status::resource_limit:
        return "resource_limit";
    case Status::unsupported:
        return "unsupported";
    }

    return "unknown";
}

}  // namespace glyph

