/*
 * payload.h
 *
 *  Created on: 31. 8. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_PAYLOAD_H_qwiod43928rf4354543
#define LIB_UMQ_PAYLOAD_H_qwiod43928rf4354543
#include <optional>
#include <string_view>
#include <shared/async_future.h>
#include <vector>

namespace umq {




class Payload{
public:

    using Attachment = std::shared_ptr<ondra_shared::async_future<std::string> >;
    class Access {
    public:
        Access(const Attachment &a):_a(a) {}
        template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<bool>(), std::declval<const std::string &>()))>
        void operator>>(Fn &&fn) {
            (*_a) >> [fn = std::forward<Fn>(fn)](const ondra_shared::async_future<std::string> &f) {
                if (f.is_ready()) fn(true, *f);
                else fn(false, std::string());
            };
        }
    protected:
        Attachment _a;
    };

    
    Payload(const std::string_view &str);
    Payload(const std::string_view &str, std::weak_ptr<BinaryPayload> bin);
    
    
    std::string_view get_text() const;
    operator std::string_view() const;
    operator std::string() const;

    bool has_attachments() const;
    std::size_t attachment_count() const;
    Access get_attachment(std::size_t idx) {
        return Access(_attachments[idx]);
    }

protected:
    std::string_view _text;
    std::vector<Attachment> _attachments;
};


}

#endif /* LIB_UMQ_PAYLOAD_H_qwiod43928rf4354543 */

