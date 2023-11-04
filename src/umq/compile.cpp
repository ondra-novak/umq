/*
 * compile.cpp
 *
 *  Created on: 3. 11. 2023
 *      Author: ondra
 */


#include "connection.h"
#include "future.h"

template class umq::Future<int>;
template class umq::Future<void>;
template class umq::SharedFuture<int>;



