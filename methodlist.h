#ifndef LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983
#define LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983

#include "request.h"

#include <shared/callback.h>
#include <shared/shared_lockable_ptr.h>
#include <map>
#include <unordered_map>

namespace umq {


using MethodCall = ondra_shared::Callback<void(Request &&req)>;

class MethodList {
public:

    const MethodCall * find_method(const std::string &name) const {
        auto iter = methods.find(name);
        if (iter != methods.end()) return &iter->second;
        auto iter2 = proxies.lower_bound(name);
        if (iter2 != proxies.end()) {
            if (name.compare(0,iter2->first.size(), iter2->first)==0) {
                return &iter2->second;
            }
        }
        return nullptr;
    }

    std::unordered_map<std::string, MethodCall> methods;
    std::map<std::string, MethodCall, std::greater<std::string> > proxies;


};


using PMethodList = ondra_shared::shared_lockable_ptr<MethodList>;


}




#endif /* LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983 */
