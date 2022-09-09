#include "payload.h"

namespace umq {

Payload::Payload(const std::string_view &str)
:text(str)
{
}

Payload::Payload(const std::string_view &str, std::weak_ptr<BinaryPayload> bin)
:text(str)
,binary(bin)
{
}

bool Payload::has_binary() const {
    return !binary.expired();
}

std::shared_ptr<BinaryPayload> Payload::get_binary() const {
}

std::string_view Payload::get_text() const {
}

Payload::operator std::string_view() const {
}

Payload::operator std::string() const {
}

}
