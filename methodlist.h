#ifndef LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983
#define LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983

#include "request.h"

#include <shared/callback.h>
#include <shared/shared_lockable_ptr.h>
#include <map>
#include <unordered_map>

namespace umq {




using MethodCall = ondra_shared::Callback<void(Request &&req)>;
using DiscoverCall = ondra_shared::Callback<void(DiscoverRequest &&req)>;

enum class EntryType {
    method,
    route,
};

struct MethodCallEntry {
    EntryType type;
    std::string name;
    MethodCall call;
    std::string doc;
};

class MethodList {
public:

    using MethodDoc = std::pair<MethodCall, std::string>;
    using RouterDoc = std::pair<MethodCall, DiscoverCall>;

    class MethodSetHelper {
    public:
        MethodSetHelper(MethodDoc &ref):_ref(ref) {}
        MethodSetHelper operator >> (MethodCall &&call) {
            _ref.first = std::move(call);
            return MethodSetHelper(_ref);
        }
        MethodSetHelper operator << (std::string &&doc) {
            _ref.second = std::move(doc);
            return MethodSetHelper(_ref);
        }
        MethodSetHelper operator << (const std::string_view &doc) {
            _ref.second = doc;
            return MethodSetHelper(_ref);
        }
        MethodSetHelper operator << (const char *doc) {
            _ref.second = doc;
            return MethodSetHelper(_ref);
        }
    protected:
        MethodDoc &_ref;
    };

    class RouteSetHelper {
    public:
        RouteSetHelper(RouterDoc &ref): _ref(ref) {}
        RouteSetHelper operator >> (MethodCall &&call) {
            _ref.first  = std::move(call);
            return RouteSetHelper(_ref);
        }
        RouteSetHelper operator << (DiscoverCall &&call) {
            _ref.second = std::move(call);
            return RouteSetHelper(_ref);
        }
    protected:
        RouterDoc &_ref;
    };


    MethodSetHelper method(std::string &&name) {
        return MethodSetHelper(methods[std::move(name)]);
    }
    MethodSetHelper method(const std::string_view &name) {
        return MethodSetHelper(methods[std::string(name)]);
    }
    MethodSetHelper method(const char *name) {
        return MethodSetHelper(methods[std::string(name)]);
    }
    RouteSetHelper route(std::string &&name) {
        return RouteSetHelper(proxies[std::move(name)]);
    }
    RouteSetHelper route(const std::string_view &name) {
        return RouteSetHelper(proxies[std::string(name)]);
    }
    RouteSetHelper route(const char *name) {
        return RouteSetHelper(proxies[std::string(name)]);
    }

    const MethodCall * find_method(const std::string &name) const {
        auto iter = methods.find(name);
        if (iter != methods.end()) return &iter->second.first;
        auto iter2 = proxies.lower_bound(name);
        if (iter2 != proxies.end()) {
            if (name.compare(0,iter2->first.size(), iter2->first)==0) {
                return &iter2->second.first;
            }
        }
        return nullptr;
    }

    const std::string *find_doc(const std::string &name) const {
        auto iter = methods.find(name);
        if (iter != methods.end()) return &iter->second.second;
        return nullptr;

    }

    const DiscoverCall * find_route_discover(const std::string &name) const {
        auto iter2 = proxies.lower_bound(name);
        if (iter2 != proxies.end()) {
            if (name.compare(0,iter2->first.size(), iter2->first)==0) {
                if (iter2->second.second != nullptr) {
                    return &iter2->second.second;
                } else {
                    return nullptr;
                }
            }
        }
        return nullptr;
    }

    std::unordered_map<std::string, std::pair<MethodCall, std::string> > methods;
    std::map<std::string, RouterDoc, std::greater<std::string> > proxies;


};


using PMethodList = ondra_shared::shared_lockable_ptr<MethodList>;


}




#endif /* LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983 */
