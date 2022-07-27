#ifndef LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983
#define LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983

#include "request.h"

#include <kissjson/value.h>
#include <shared/callback.h>
#include <shared/shared_lockable_ptr.h>

namespace umq {


using MethodCall = ondra_shared::Callback<void(Request &req)>;

using MethodList = std::unordered_map<std::string, MethodCall>;

using PMethodList = ondra_shared::shared_lockable_ptr<MethodList>;


}




#endif /* LIB_UMQ_METHODLIST_H_qweqweq8e32eu29dwioed2983 */
