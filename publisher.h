/*
 * publisher.h
 *
 *  Created on: 1. 8. 2022
 *      Author: ondra
 */

#ifndef _LIB_UMQ_PUBLISHER_H_qeu289dhdh9dhqw
#define _LIB_UMQ_PUBLISHER_H_qeu289dhdh9dhqw
#include <mutex>
#include <vector>

#include "peer.h"
namespace umq {

class Publisher {
public:

	~Publisher();

	///Subscribe subscriber
	/**
	 * @param cb a callback function called on publish/topic updae
	 * @return ID of the subscriber
	 */
	std::size_t subscribe(TopicUpdateCallback &cb);

	///Unsubscribe the subscriber
	/**
	 * @param id id of subscriber
	 */
	void unsubscribe(std::size_t id);

	///Publish the topic update
	/**
	 * @param v value to publish
	 * @retval true published
	 * @retval false no subscribers
	 */
	bool publish(const kjson::Value &v);


	///Create unsubscribe request for given ID
	UnsubscribeRequest create_unsub_request(std::size_t id);

	///Create unsubscribe request while it refers the publisher using a weak pointer
	static UnsubscribeRequest create_unsub_request(
			const std::shared_ptr<Publisher> &pub,
			std::size_t id);

	///Clear all subscribers
	void reset();

	///returns true, if publisher has no subscribers
	bool empty() const;

protected:

	struct Subscriber {
		std::size_t id;
		TopicUpdateCallback cb;
	};

	mutable std::recursive_mutex _mx;
	std::vector<Subscriber> _subs;
	std::size_t idcnt = 0;

};


}




#endif /* _LIB_UMQ_PUBLISHER_H_qeu289dhdh9dhqw */
