/*
 * payload.h
 *
 *  Created on: 31. 8. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_PAYLOAD_H_qwiod43928rf4354543
#define LIB_UMQ_PAYLOAD_H_qwiod43928rf4354543
#include <shared/async_future.h>
#include <string_view>
#include <string>
#include <vector>

namespace umq {


///The future contains attachment- it is avalable when attachments are currently downloaded
using AttachContent = ondra_shared::async_future<std::string>;
using Attachment = std::shared_ptr<AttachContent>;
using AttachList = std::vector<Attachment>;



template<typename T>
class TypeWithAttachT: public T {
public:
	using T::T;
	TypeWithAttachT(const T &other):T(other) {}
	TypeWithAttachT(T &&other):T(std::forward<T>(other)) {}
	TypeWithAttachT(const TypeWithAttachT &other) = default;
	TypeWithAttachT(TypeWithAttachT &&other) = default;
	TypeWithAttachT(const T &other, const AttachList &lst):T(other),attachments(lst) {}

	TypeWithAttachT &operator=(const T &msg) {
		T::operator=(msg);
		return *this;
	}
	TypeWithAttachT &operator=(T &&msg) {
		T::operator=(std::forward<T>(msg));
		return *this;
	}
	TypeWithAttachT &operator=(const TypeWithAttachT &msg) = default;
	TypeWithAttachT &operator=(TypeWithAttachT &&msg) = default;

	template<typename X>
	TypeWithAttachT(const TypeWithAttachT<X> &other)
		:T(other),attachments(other.attachments) {}
	template<typename X>
	TypeWithAttachT &operator=(const TypeWithAttachT<X> &other) {
		T::operator=(other);
		attachments = other.attachments;
		return *this;
	}



	AttachList attachments;
};

///Payload is helper class, which acts as a string_view but can carry attachments
/**
 * Main part of every payload is ability to transfer binary attachments. There
 * can be unilimited count of attachments. For large files, it is adviced to
 * split one large file into serveral attachments.
 *
 * The attachments are declared as async_future. You can always send a message
 * containing a payload without the data itself. You can supply the data later. The
 * same is applied when Payload is received. These attachment can resolve
 * later.
 */
using Payload = TypeWithAttachT<std::string_view>;

using PayloadStr =  TypeWithAttachT<std::string>;

}
#endif
