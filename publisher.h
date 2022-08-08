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
	bool publish(const std::string_view &v);


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
	std::vector<std::size_t> _unsubs;
	std::size_t idcnt = 0;
	/** in progress = true?
	 *
	 * Because recursive_mutex is used, it is possible to call unsubscribe from
	 * the subscriber during publish cycle. This can harm publishing cycle. So
	 * if the _inp is true, unsubscribe requests are collected in _unsubs array,
	 * which is ordered by id. Once the publish cycle is finished, all
	 * marked subscribers are removed from the _subs. So it is now possible to call
	 * unsubscribe from the subscriber, which has the same effect as returning false
	 * from the subscriber. It is also possible to unsubscribe other subscribers.
	 * (this can happen, when during publishing, the subscriber's connection is closed,
	 * which can cause massive unsubscribe of all topics on the connection
	 *
	 */
	bool _inp = false;

	void do_unsubscribe();

};


}




#endif /* _LIB_UMQ_PUBLISHER_H_qeu289dhdh9dhqw */
