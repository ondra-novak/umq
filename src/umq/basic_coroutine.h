/** SIMPLE COROUTINE SUPPORT *****
 *
 * This header only library supports coroutines for asynchronous operations
 *
 * To write a coroutine, you must comply with the following conditions
 * - a return value of the coroutine must be either Future<T> or Async<T>.
 * - your function must use at least co_await and/or co_return
 * - co_return expression always return value T (not Future<T> nor Async<T>
 *
 * Future<int> coroutine() {
 *   co_return 42;
 * }
 *
 * Object Future<T> is not movable. However you can write
 * std::unique_ptr<Future<int> > fut (new auto(coroutine()));
 * - which allocates future on heap
 *
 * For convience you can use SharedFuture<T> which acts as ref-counted shared object
 *
 * Awaiting:
 *---------------
 *
 * The Future<T> is awaitable. To retrieve result, you can co_await Future<T>. Result
 * is T.
 *
 * int res = co_await coroutine();
 *
 * If coroutine completed with exception, co_await throws that exception
 *
 * Synchronous awaiting
 * ---------------------
 *
 * Conversion Future<T> to T triggers synchronous await
 *
 * int res = coroutine() - blocks while waiting for result
 *
 * Awaiting using Target
 * ---------------------
 *
 * Future<T>::Target defines target object, which is activated once the future is resolved.
 * The target must be allocated to ensure, that is valid until activated.
 *
 * You can register target to future
 *
 * Future<int>::Target target = ....
 * Future<int> fut = corutine();
 * fut.register_target(&target);
 *
 * NOTE: You must ensure that the Future instance is valid until it is resolved!
 *
 * Awaiting using callback
 * -------------------------
 *
 * Future<T> can accept a callback
 * Future<int> fut = corutine();
 * fut >> [&]{.... callback ...};
 *
 * NOTE: You must ensure that the Future instance is valid until it is resolved!
 *
 * Using SharedFuture
 * ------------------
 *
 * SharedFuture works similar as Future, but it is always allocated at heap. It is
 * shared using counting references.
 *
 * To construct SharedFuture from coroutine's result, you need to use proxy-lambda
 * function
 *
 * SharedFuture fut([&]{return coroutine();})
 *
 * The reson for this is that Future<T> is non-copyable and unmovable object. This
 * constructor ensures, that coroutine initializes the shared future at already
 * preallocated memory.
 *
 * NOTE it is recommended to return Future<T> from functions, because construction of
 * the Future<T> doesn't allocate memory. If you need SharedFuture<T>, you can easly
 * convert it.
 *
 *
 * Both Future<T> and SharedFuture<T> can notify multiple Targets - so multiple
 * await can be requested at once. In case of SharedFuture<T> every await should
 * also hold a reference to the instance.
 *
 */

#ifndef _HEADER_BASIC_COROUTINE_H_
#define _HEADER_BASIC_COROUTINE_H_

#pragma once
#include <utility>
#include <coroutine>
#include <atomic>
#include <stdexcept>
#include <condition_variable>
#include <queue>
#include <optional>

namespace basic_coro {

namespace _details {

template<typename X>
struct ExtractObjectType;

template<typename Obj, typename Ret, typename ... Args>
struct ExtractObjectType<Ret (Obj::*)(Args...) noexcept> {
    using value = Obj;
};

template<auto fn>
using ExtractObject_t = typename ExtractObjectType<decltype(fn)>::value;

template<typename T, typename R, typename ... Args>
concept InvokableWithResult = std::is_same_v<std::invoke_result_t<T, Args...>, R>;


template<typename A, typename B, typename ... Args>
concept MemberFunction = requires(A a, B b, Args ... args) {
    (a->*b)(args...);
};

template<typename X>
class Target {
public:
    using FastCallback = std::coroutine_handle<> (*)(X fut, void *user_ptr);
    using TargetArgType = X;

    ///Defines FastCallback function which wake's up a sleeping coroutine
    /**
     * @param Future - not used
     * @param ptr contains address of corutine handle
     * @return Function just converts address to coroutine handle, which is
     * resumed later. This handle is returned
     */
    static std::coroutine_handle<> coroutine_target(X , void *ptr) {
        return std::coroutine_handle<>::from_address(ptr);
    }


    ///Defines FastCallback function, which calls a member function of an object
    /**
     * @tparam fn pointer to member function (constexpr pointer)
     * @param f future which activated the Target
     * @param ptr user defined pointer which points to object's instance in this case
     * @return function always returns nullptr
     */
    template<auto fn>
    static std::coroutine_handle<> member_function_target(X f, void *ptr) {
        using MemPtr = decltype(fn);
        using Obj = _details::ExtractObject_t<fn> *;
        auto me = reinterpret_cast<Obj>(ptr);
        if constexpr(_details::MemberFunction<Obj, MemPtr>) {
            (me->*fn)();
        } else {
            (me->*fn)(std::forward<X>(f));
        }
        return nullptr;
    }

private:
    ///pointer to C function (fast and safe, no memory allocation)
    FastCallback _fn;
    ///user pointer which is passed to the function
    void *_user_ptr;
    ///pointer to next slot (internal, should not be changed externally)
    mutable const Target *_next;

public:
    ///Create target which calls a member function of an object
    /**
     * @tparam fn pointer to member function without arguments or with argument
     *  Future *.
     * @param obj pointer to target object
     * @return target instance
     */
    template<auto fn>
    static Target member_fn(typename _details::ExtractObjectType<decltype(fn)>::value *obj) {
        Target t  = {};
        t._fn = &member_function_target<fn>;
        t._user_ptr = obj;
        return t;
    }


    ///Create target which is responsible to wake up a coroutine
    /**
     *
     * @param h handle to coroutine
     * @return target
     */
    static Target coroutine(std::coroutine_handle<> h) {
        Target t = {};
        t._fn = &coroutine_target;
        t._user_ptr = h.address();
        return t;
    }

    static constexpr Target function(FastCallback cb, void *user_ptr) {
        Target t = {};
        t._fn = cb;
        t._user_ptr = user_ptr;
        return t;
    }

    ///Activate the target
    /**
     * @param f pointer to a future which caused an activation
     * @return coroutine handle. Valid coroutine handle causes resumption of
     * a coroutine (which must be handled outside of the target). Otherwise
     * function can return nullptr (invalid handle)
     */
    [[nodiscard]] std::coroutine_handle<> activate(X f) const noexcept {
        //this is shortcut for coroutines
        //it avoids indirect call (through a pointer) when Target is coroutine
        //because coroutine's resume is also indirect call, this remove one indirect call
        //in a way - however it adds one branch prediction
        if (_fn == &coroutine_target) [[likely]] {
            return std::coroutine_handle<>::from_address(_user_ptr);
        }
        return _fn(std::forward<X>(f), _user_ptr);
    }


    ///Activate the target, resume coroutine
    void activate_resume(X f) const noexcept {
        auto r = activate(std::forward<X>(f));
        if (r) r.resume();
    }


    ///Construct special targe which marks disabled target
    static constexpr Target disabled_target = {};


    static constexpr std::coroutine_handle<> empty_target_fn(X , void *) {return nullptr;};
    ////Constructs empty target
    static constexpr Target empty_target() {
        Target t = {};
        t._fn = &empty_target_fn;
        return t;
    }


    ///Push atomically to linked-list stack
    bool push_to(std::atomic<const Target *> &stack) const {
        _next = stack.load(std::memory_order_relaxed);
        do {
            if (_next == &disabled_target) return false;
        } while (!stack.compare_exchange_strong(_next, this));
        return true;
    }

    template<typename ... Targets>
    static bool find_target(const Target *t, const Target *x, Targets ... targets) {
        if (t == x) return true;
        return find_target(t, targets...);
    }

    static bool find_target(const Target *t) {
        return false;
    }

    template<typename ... Targets>
    bool push_to(std::atomic<const Target *> &stack, Targets ... targets) const {
        _next = stack.load(std::memory_order_relaxed);
        do {
            bool res =  find_target(_next, targets...);
            if (res) return false;
        } while (!stack.compare_exchange_strong(_next, this));
        return true;
    }


    ///Push to non-atomic linked-list stack
    void push_to(const Target * &stack) const {
        _next = stack;
        stack = this;
    }

    const Target *get_next() const {
        return _next;
    }


};

///Target which is responsible to call a callback and destroy itself once done
template<typename X, typename Fn>
class CallbackTarget: public Target<X> {
public:
    CallbackTarget(Fn &&fn)
        :Target<X>(Target<X>::template member_fn<&CallbackTarget::activate_callback>(this))
        ,_fn(std::move(fn)) {}
protected:
    Fn _fn;
    std::coroutine_handle<> activate_callback(X f) noexcept {
        if constexpr(std::is_invocable_v<Fn>) {
            _fn();
        } else {
            _fn(std::forward<X>(f));
        }
        delete this;
        return nullptr;
    }
};

///Construct callback target
/**
 * @tparam Target (mandatory) Target type
 * @param fn function, which receives argument defined by the Target
 * @return unique_ptr pointing to the target
 */
template<typename Target, typename Fn>
auto callback_target(Fn &&fn) {
    return std::make_unique<CallbackTarget<typename Target::TargetArgType, std::decay_t<Fn> > >(std::move(fn));
}

template<typename T>
class SyncWaitTarget: public Target<T> {
    //use statically allocated - for performance reasons
    static std::pair<std::mutex, std::condition_variable> _static_sync;

    bool _flag = false;
    void notify() noexcept {
        std::lock_guard _(_static_sync.first);
        _flag = true;
        _static_sync.second.notify_all();
    }
public:
    SyncWaitTarget():Target<T>(Target<T>::template member_fn<&SyncWaitTarget::notify>(this)) {}
    void wait() {
        std::unique_lock lk(_static_sync.first);
        _static_sync.second.wait(lk,[&]{return _flag;});
    }
};

template<typename T>
inline std::pair<std::mutex, std::condition_variable> SyncWaitTarget<T>::_static_sync;

}


namespace FutureException {
///Exception is thrown on attempt to retrieve value after promise has been broken
class BrokenPromise: public std::exception {
public:
    const char *what() const noexcept override {return "Broken promise (umq::Future::Promise)";}
};

///Exception is thrown on attempt to retrieve promise when the future is already pending
class AlreadyPending: public std::exception {
public:
    const char *what() const noexcept override {return "Future is already pending (umq::Future)";}
};

}

///Async implements coroutine future proxy
template<typename T> class Async;
template<typename T> struct AsyncPromiseBase;


///State of future
enum class FutureState {
    pending,///< future is pending, there is promise pointing on it
    ready,  ///< future is ready - resolved
    lazy    ///< future evaluation is deferred.
};

///Declaration of future variable, allows synchronization and co_await on it
/**
 * @tparam T type of value stored in the future. It is possible to use T & to store just
 * only reference. There is no difference when value is access, but the producer must
 * ensure, that referenced value is valid until it is consumed. It is also possible to
 * use T &&, which is similar to T, but enforces rvalue reference when result is consumed
 *
 * - T - store value , return T &
 * - T & - store reference, return T &
 * - T && - store value, return T &&
 *
 */
template<typename T>
class Future {
public:

    ///contains type
    using value_type = std::decay_t<T>;
    ///is true when T is void
    static constexpr bool is_void = std::is_void_v<value_type>;
    static constexpr bool is_rvalue_ref = !std::is_void_v<T> && std::is_rvalue_reference_v<T>;
    static constexpr bool is_lvalue_ref = !std::is_void_v<T> && std::is_lvalue_reference_v<T>;

    ///declaration of type which cannot be void, so void is replaced by bool
    using VoidlessType = std::conditional_t<is_void, bool, value_type >;
    ///declaration of return value - which is reference to type or void
    using RetVal = std::conditional_t<is_rvalue_ref, T, std::add_lvalue_reference_t<T> >;

    using StorageType = std::conditional_t<is_lvalue_ref, VoidlessType *, VoidlessType>;
    using ConstructType = std::conditional_t<is_lvalue_ref, VoidlessType &, VoidlessType>;

    using CastRetVal = std::conditional_t<is_void, bool, RetVal>;

    ///Tags Future<T> as valid return value for coroutines
    using promise_type = typename Async<T>::promise_type;


    using BrokenPromise = FutureException::BrokenPromise;
    using AlreadyPending = FutureException::AlreadyPending;



    ///Promise type
    /**Promise resolves pending Future.
     * Promise is movable object. There can be only one Promise per Future.
     * Promise acts as callback, which can accepts multiple argumens depend on
     * constructor of the T
     * Promise is one shot. Once it is fullfilled, it is detached from the Future
     * Multiple threads can try to resolve promise simultaneously. this operation is
     * MT safe. Only one thread can success, other are ignored
     * Promise can rejected with exception. You can use reject() without arguments
     * inside of catch block
     * Destroying not-filled promse causes BrokenPromise
     * You can break promise by function drop()
     *
     */
    class Promise {
    public:
        ///Construct unbound promise
        Promise() = default;
        ///Construct bound promise to a future
        Promise(Future *f) noexcept :_ptr(f) {}
        ///Move promise
        Promise(Promise &&other) noexcept:_ptr(other._ptr.exchange(nullptr, std::memory_order_relaxed)) {}
        ///Copy is disabled
        Promise(const Promise &other) = delete;
        ///Destructor drops promise
        ~Promise() {
            drop();
        }

        ///Move promise by assignment. It can drop original promise
        Promise &operator=(Promise &&other) noexcept {
            if (this != &other) {
                auto x = other._ptr.exchange(nullptr, std::memory_order_relaxed);
                if (*this) reject();
                _ptr.store(x, std::memory_order_relaxed);
            }
            return *this;
        }
        Promise &operator=(const Promise &other) = delete;

        struct DeferNotifyDel {void operator()(Future *f){f->notify_resume();}};
        ///Returned by most Promise functions
        /** Can be used to test true/false. It contains
         * deferred notification. Must be destroyed to notify the future.
         * Helps to process some operations before notification, for example unlock
         * some locks
         */
        using DeferNotify = std::unique_ptr<Future, DeferNotifyDel>;

        ///Drop promise - so it becomes broken
        /**
         * @retval true  success
         * @retval false promise is unbound
         */
        DeferNotify drop() noexcept {
            auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
            return DeferNotify(x);
        }


        ///resolve promise
        /** Imitates a callback call
         *
         * @param args arguments to construct T
         * @retval true success- resolved
         * @retval false promise is unbound, object was not constructed
         *
         * */
        template<typename ... Args>
        DeferNotify operator()(Args && ... args)  {
            auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
            if (x) {
                x->set_value(std::forward<Args>(args)...);
                return DeferNotify(x);
            }
            return {};
        }



        ///reject promise with exception
        /**
         * @param except exception to construct
         * @retval true success - resolved
         * @retval false promise is unbound
         */
        template<typename Exception>
        DeferNotify reject(Exception && except) {
            auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
            if (x) {
                x->set_exception(std::make_exception_ptr<Exception>(std::forward<Exception>(except)));
                return DeferNotify(x);
            }
            return {};
        }

        ///Determines state of the promise
        /**
         * @retval true promise is bound to pending future
         * @retval false promise is unbound
         */
        explicit operator bool() const {
            return _ptr.load(std::memory_order_relaxed) != nullptr;
        }

        ///Reject under catch handler
        /**
         * If called outside of catch-handler it is equivalent to drop()
         * @retval true success
         * @retval false promise is unbound
         */
        auto reject() {
            auto exp = std::current_exception();
            if (exp) {
                return reject(std::move(exp));
            } else {
                return drop();
            }
        }



        ///Release internal pointer to the future
        /** It can be useful to transfer the pointer through non-pointer type variable,
         * for example if you use std::uintptr_t. You need construct Promise from this
         * pointer to use it as promise
         *
         * @return pointer to pending future. The future is still pending.
         *
         * @note this instance looses the ability to resolve the future
         *
         */
        Future *release() {
            return _ptr.exchange(nullptr, std::memory_order_relaxed);
        }


    protected:
        std::atomic<Future *> _ptr = {nullptr};
    };



    ///Specifies target in code where execution continues after future is resolved
    /** This is maleable object which can be used to wakeup coroutines or
     * call function when asynchronous operation is complete. It was intentionaly
     * designed as non-function object to reduce heap allocations. Target can be
     * declared statically, or can be instantiated as a member variable without
     * need to allocate anything. If used along with coroutines, it is always
     * created in coroutine frame which is already preallocated.
     *
     * Target is designed as POD, it has no constructor, no destructor and it
     * can be put inside to union with other Targets if only one target
     * can be charged
     */
    using Target = _details::Target<Future<T> *>;


    static constexpr Target lazy_resolve_target = Target::empty_target();

    ///constructs dormant future
    /** Dormant future is future which is neither initialized, nor pending. This future
     * is ready to be initialized later. It can be also destroyed with no issues
     */
    Future():_state(unresolved) {}
    ///constructs resolved future
    /** This future is already resolved with a value
     *
     * @param val value stored to the future
     */
    Future(ConstructType val):_state(resolved_value), _value(from_construct_type(std::forward<ConstructType>(val))) {}
    ///constructs resolved future which carries an exception
    /**
     * @param val exception pointer
     */
    Future(const std::exception_ptr &val):_state(resolved_exception), _exception(val) {}
    ///destructor
    /** Pending future can't be destroyed */
    ~Future() {
        cleanup();
    }

    ///Inicialize future and promise at once by spliting code into two branches
    /**
     * @param fn a function, which receives Promise<T> as argument and which can
     * continue to perform operation which leads to resolution of the promise. The
     * promise can be moved to a different thread
     *
     * The constructed Future<T> can be returned from the function as a result
     *
     * @code
     * Future<int> async_call() {
     *      return [&](Promise<int> prom) {
     *          run_async_with_promise(std::move(prom));
     *      };
     * }
     * @endcode
     */
    template<_details::InvokableWithResult<void, Promise> Fn>
    Future(Fn &&fn):_state(unresolved),_targets(nullptr) {
        fn(Promise(this));
    }
    template<_details::InvokableWithResult<Future> Fn>
    Future(Fn &&fn):_state(unresolved) {
        this->operator <<(std::forward<Fn>(fn));
    }

    ///Future cannot be copied
    Future(const Future &) = delete;
    ///Future cannot be assigned (unless see operator << )
    Future &operator=(const Future &) = delete;

    ///Type which defines lazy targets
    using LazyTarget = _details::Target<Promise &&>;


    ///Construct lazy future
    /** Lazy future is stays unresolved until the first awaiter registered
     * to the future.
     * @param t target object which is activated after first awaiter is registered.
     * The reference must stay valid until target is activated
     * @return future object.
     *
     * @note if the future is destroyed, the target is activated with unbound promise
     * to inidcate destruction of the future
     */
    Future(LazyTarget &t):_state(lazy_resolve),_lazy_target(&t),_targets(&lazy_resolve_target) {}

    ///Construct lazy future
    /** Lazy future is stays unresolved until the first awaiter registered
     * to the future.
     * @param t dynamic target object which is activated after first awaiter is registered.
     * Is expected, that target is destroyed automatically
     * @return future object.
     *
     * @note if the future is destroyed, the target is activated with unbound promise
     * to inidcate destruction of the future
     */
    Future(std::unique_ptr<LazyTarget> t):Future(*t.release()) {}

    ///constrcut lazy future
    /** Lazy future is stays unresolved until the first awaiter registered
     * to the future.
     *
     * @param fn callback function which is called to resolve the future.
     * @return future object
     * @note if the future is destroyed, the callback is called with unbound promise
     * to inidcate destruction of the future
     *
     * @note uses heap to temporary hold the callbackn function
     *
     */

    template<_details::InvokableWithResult<void, Promise> Fn>
    static Future lazy(Fn &&fn){
        return _details::callback_target<Promise>(std::forward<Fn>(fn));
    }

    ///Prepares _dormant_ future, retrieves promise
    /**
     * The future must be in either domant or resolved state. If it is in resolved
     * state, it is reset to dormant state. When it is in dormant state, it is set
     * to unresolved state and returns promise. In this case, the future is considered
     * pending
     * .
     * @return promise object
     *
     * @exception AlreadyPending attempt to call this function while the future is
     * already pending
     */
    Promise get_promise() {
        const Target *need = &Target::disabled_target;
        if (_targets.compare_exchange_strong(need, nullptr, std::memory_order_relaxed)) {
            clear_storage();
            _state = unresolved;
            return Promise(this);
        }
        throw AlreadyPending();
    }


    ///Synchronous wait
    /**
     * @return result
     * @exception any if the promise is filled by an exception, it is thrown now
     * @note blocks the thread while it waits for resolution
     */
    RetVal wait() {
        if (is_pending()) {
            _details::SyncWaitTarget<Future<T> *> syncwait;
            if (register_target_async(syncwait)){
                syncwait.wait();
            }
        }
        return get_internal();

    }


    ///Synchronous get
    /**
     * @return result
     * @exception any if the promise is filled by an exception, it is thrown now
     * @note blocks the thread while it waits for resolution
     */
    RetVal get() {
        return wait();
    }

    ///Synchronous get
    /**
     * @return result
     * @exception any if the promise is filled by an exception, it is thrown now
     * @note blocks the thread while it waits for resolution
     */
    operator CastRetVal() {
        if constexpr(is_void) {
            wait();
            return std::get<VoidlessType>(_value);

        } else {
            return wait();
        }
    }

    ///Register callback as target, which is called asynchronously
    /**
     * The callback is always called asynchronously. The function fails, if
     * called on already resolved future. This is indicated by the return value.
     * You can perform a reaction for such state
     *
     * @param fn function to call asynchronously when future is resolved
     * @retval true callback registered, will be called asynchronously
     * @retval false failure, future is already resolved, callback was not called
     *
     */
    template<typename Fn>
    requires (std::invocable<Fn> || std::invocable<Fn, Future *>)
    bool callback_async(Fn fn) {
        //shortcut
        if (is_pending()) {
            auto t = _details::callback_target<Future<T> >(std::forward<Fn>(fn));
            return register_target_aync(std::move(t));
        } else {
            return false;
        }
    }

    ///Register callback as target, it is always called once the future is resolved
    /**
     * The callback can be called synchronously if this function is called on already
     * resolved future
     *
     * @param fn function to calll
     * @retval true the callback will be called asynchronously (postponed)
     * @retval false the callback was called synchronously (already resolved)
     */
    template<typename Fn>
    requires (std::invocable<Fn> || std::invocable<Fn, Future *>)
    bool callback(Fn fn) {
        auto t = _details::callback_target<Target>(std::forward<Fn>(fn));
        return register_target(std::move(t));
    }

    ///Register callback as target, it is always called once the future is resolved
    /**
     * The callback can be called synchronously if this function is called on already
     * resolved future
     *
     * @param fn function to calll
     * @retval true the callback will be called asynchronously (postponed)
     * @retval false the callback was called synchronously (already resolved)
     */
    template<typename Fn>
    requires (std::invocable<Fn> || std::invocable<Fn, Future *>)
    bool operator >> (Fn fn) {
        return callback(fn);
    }



    ///Initialize the future object from return value of a function
    /**
     * @param fn function which's return value is used to initialize the future.
     * The future must be either dormant or resolved
     *
     * @code
     * Future<T> f;
     * f << [&]{return a_function_returning_future(arg1,arg2,arg3);};
     * @endcode
     */
    template<_details::InvokableWithResult<Future<T> > Fn>
    void operator << (Fn &&fn) {
        std::destroy_at(this);
        try {
            new(this) Future(fn());
        } catch (...) {
            new(this) Future(std::current_exception());
        }
    }



    FutureState get_state() const {
        auto c = _targets.load(std::memory_order_relaxed);
        if (c == &Target::disabled_target) return FutureState::ready;
        if (c == &lazy_resolve_target) return FutureState::lazy;
        return FutureState::pending;
    }

    ///Determines pending state
    /**
     * @retval false the future is dormant or resolved
     * @retval true the future is pending
     */
    bool is_pending() const noexcept {
        auto st = get_state();
        return st == FutureState::pending || st == FutureState::lazy;
    }

    ///Determines pending state
    bool is_lazy() const noexcept {
        return get_state() == FutureState::lazy;
    }

    ///Returns true, if future is resolved with either value or exception
    /**
     * @retval true the future is resolved with value or exception
     * @retval false the future is either pending, dormant or broken (broken promise)
     */
    bool has_value() const noexcept {
        return _state != unresolved;
    }

    ///Retrieve exception pointer if the future has exception
    /**
     * @return exception pointer od nullptr if there is no exception recorded
     */
    std::exception_ptr get_exception_ptr() const noexcept {
        return _state != resolved_exception?_exception:nullptr;
    }

    ///helps with awaiting on resolve()
    class ReadyStateAwaiter {
    public:
        ReadyStateAwaiter(Future &owner):_owner(owner) {};
        ReadyStateAwaiter(const ReadyStateAwaiter &) = default;
        ReadyStateAwaiter &operator=(const ReadyStateAwaiter &) = delete;

        bool await_ready() const {
            return !_owner.is_pending();
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h)  {
            _target = Target::coroutine(h);
            return _owner.register_coroutine_target(_target);
        }
        bool await_resume() const {
            return _owner.has_value();
        }

    protected:
        Future &_owner;
        Target _target;

    };

    ///Generic awaiter
    class ValueAwaiter: public ReadyStateAwaiter {
    public:
        using ReadyStateAwaiter::ReadyStateAwaiter;
        RetVal await_resume() const {
            return this->_owner.get_internal();
        }
    };

    ///Retrieves awaitable object which resumes a coroutine once the future is resolved
    /**
     * The awaitable object can't throw exception. It eventually returns has_value() state
     *
     * @code
     * Future<T> f = [&](auto promise) {...}
     * bool is_broken_promise = !co_await f.resolve();
     * @endcode
     *
     * @retval true future is resolved by value or exception
     * @retval false broken promise
     */
    ReadyStateAwaiter ready() {
        return ReadyStateAwaiter(*this);
    }


    ///Retrieves awaitable object to await and retrieve a value (or exception)
    /**
     *
     * @code
     * Future<T> f = [&](auto promise) {...}
     * T result = co_await f;
     * @endcode
     *
     */
    ValueAwaiter operator co_await() {
        return ValueAwaiter(*this);
    }



    ///Atomically register a target when future is still pending (atomically checked)
    /**
     * @param t pointer to target instance
     * @retval true registered successfuly, target can be activated from now
     * @retval false fauilure, future is no longer pending
     */
    bool register_target_async(Target &t) {
        bool r = t.push_to(_targets, &Target::disabled_target, &lazy_resolve_target);
        if (!r && t.get_next() == &lazy_resolve_target) {//handle lazy state
            const Target *need = t.get_next();
            if (!_targets.compare_exchange_strong(need,&t)) {//replace with target
                return register_target_async(t); //failure - repeat
            }
            _state = unresolved; //success
            _lazy_target->activate_resume(Promise(this)); //activate lazy target
            return true;
        }
        return r;
    }

    ///Atomically register target (no failure)
    /**
     * @param t target to register
     * @retval true registered, target will be called asynchronously
     * @retval false future is no longer pending, target were activated sychronously
     */
    bool register_target(Target &t) {
        if (!register_target_async(t)) {
            auto r = t.activate(this);
            if (r) r.resume();
            return false;
        } else {
            return true;
        }
    }

    ///Atomically registers one-shot dynamically allocated target
    /**
     * It is expected, that target is destroyed once it is activated. If this rule
     * is not followed (you allocated standard target and passed its pointer), result
     * is memory leak.
     *
     * @param t unique pointer to one-shot dynamically allocated target
     * @retval true target registered
     * @retval false already resolved, target has been destroyed without activation
     */
    bool register_target_async(std::unique_ptr<Target> t) {
        Target *x = t.release();
        if (!register_target_async(*x)) {
            delete x;
            return false;
        }
        return true;
    }

    ///Atomically registers one-shot dynamically allocated target
    /**
     * It is expected, that target is destroyed once it is activated. If this rule
     * is not followed (you allocated standard target and passed its pointer), result
     * is memory leak.
     *
     * @param t unique pointer to one-shot dynamically allocated target
     * @retval true target registered
     * @retval false already resolved but target was activated and destroyed synchronously
     */
    bool register_target(std::unique_ptr<Target> t) {
        Target *x = t.release();
        return register_target(*x);
    }

protected:

    enum StorageState {
        unresolved,
        lazy_resolve,
        resolved_value,
        resolved_exception
    };

    StorageState _state = unresolved;
    union {
        StorageType _value;
        std::exception_ptr _exception;
        LazyTarget *_lazy_target;
    };
    std::atomic<const Target *> _targets = {&Target::disabled_target};

    std::conditional_t<is_lvalue_ref, value_type *, StorageType &&> from_construct_type(ConstructType &&t) {
        if constexpr(is_lvalue_ref) {
            return &t;
        } else {
            return std::move(t);
        }
    }

    void cleanup() {
        if (get_state() == FutureState::pending) {
            throw std::runtime_error("Destructor or cleanup() called on pending Future");
        }
        clear_storage();
    }

    void reset() {
        cleanup();
        _state = unresolved;
    }

    template<typename ... Args>
    void set_value(Args && ... args) {
        _state = resolved_value;
        if constexpr(is_lvalue_ref) {
            static_assert(sizeof...(args) == 1, "Only one argument is allowed in reference mode");
            static constexpr auto get_ptr  = [](auto &x) {
                return &x;
            };
            std::construct_at(&_value, get_ptr(args...));
        } else {
            std::construct_at(&_value, std::forward<Args>(args)...);
        }
    }

    void set_exception(std::exception_ptr e) {
        _state = resolved_exception;
        std::construct_at(&_exception, std::move(e));
    }

    std::coroutine_handle<> notify_targets() {
        const Target *list = _targets.exchange(&Target::disabled_target);
        while (list) {
            auto cor = list->activate(this);
            if (cor) {
                list = list->get_next();
                while (list) {
                    auto cor = list->activate(this);
                    if (cor) cor.resume();
                    list = list->get_next();
                }
            }
            return cor;
        }
        return nullptr;
    }

    void notify_resume() {
        auto x = notify_targets();
        if (x) x.resume();
    }

    RetVal get_internal() {
        switch (_state) {
            default: throw BrokenPromise();
            case resolved_exception: std::rethrow_exception(_exception);
            case resolved_value:
                if constexpr(is_void) {
                    return;
                } else if constexpr(is_rvalue_ref) {
                    return std::move(_value);
                } else if constexpr(is_lvalue_ref) {
                    return *_value;
                } else {
                    return _value;
                }
        }
    }

    void set_lazy(LazyTarget *t)  {
        _state = lazy_resolve;
        _lazy_target = t;
        _targets = &lazy_resolve_target;
    }

    ///Register coroutine target - used by awaiters
    /** If the future is lazy (coroutine) it uses symmetric transfer from
     *  awaiting coroutine, to starting coroutine.
     *
     * @param t
     * @return
     */
    std::coroutine_handle<> register_coroutine_target(Target &t) {
        bool r = t.push_to(_targets, &Target::disabled_target, &lazy_resolve_target);
        if (!r) { //not stored
            if (t.get_next() == &lazy_resolve_target) { //handle lazy
                const Target *need = t.get_next();
                if (!_targets.compare_exchange_strong(need,&t)) { //replace target
                    return register_coroutine_target(t); //failure - repeat
                }
                _state = unresolved;
                auto c =_lazy_target->activate(Promise(this)); //activate lazy target
                if (c) return c;
            } else {
                return t.activate(this); //already resolved- activate coroutine
            }
        }
        return std::noop_coroutine(); //suspend by switching to noop;
    }


    void clear_storage() {
        switch (_state) {
            default:break;
            case resolved_value: std::destroy_at(&_value);break;
            case resolved_exception: std::destroy_at(&_exception);break;
            case lazy_resolve: _lazy_target->activate_resume(Promise());break;
        }
    }

    friend class Async<T>;
    friend class AsyncPromiseBase<T>;


};


template<typename T>
using Promise = typename Future<T>::Promise;


///SharedFuture is less efficient but sharable future
/**
 * SharedFuture act as Future, but it can be shared by copying. It is always allocated
 * at heap. It also can be left unresolved, which causes that memory is released
 * once the future is resolved
 * @tparam T type of future
 *
 * Single SharedFuture CAN BE AWAITED BY MULTIPLE AWAITERS
 */
template<typename T>
class SharedFuture {

    class RefCntFuture: public Future<T> {
    public:

        using Future<T>::Future;

        void add_ref() {
            _refcnt.fetch_add(1, std::memory_order_relaxed);
        }
        bool release_ref() {
            return _refcnt.fetch_sub(1, std::memory_order_release) < 2;
        }

        void release_on_event() noexcept {
            if (release_ref()) delete this;
        }

        void hold_reference() {
            _my_target = Future<T>::Target::template member_fn<&RefCntFuture::release_on_event>(this);
            add_ref();
            this->register_target(_my_target);
        }

#if BOOST_TEST_MODULE == CouroutineAsync
        ~RefCntFuture() {
            dealloc_counter++;
        }
#endif
    protected:
        typename Future<T>::Target _my_target;
        std::atomic<unsigned int> _refcnt = {0};


    };



public:
    using promise_type = typename Async<T>::promise_type;

#if BOOST_TEST_MODULE == CouroutineAsync
    static int dealloc_counter;
#endif

    using value_type = T;
    using VoidlessType = typename Future<T>::VoidlessType;
    using Promise = typename Future<T>::Promise;
    using RetVal = typename Future<T>::RetVal;
    using CastRetVal = typename Future<T>::CastRetVal;

    ///Constructs dormant future
    /**Dormant future doesn't allocate any memory */
    SharedFuture():_ptr(nullptr) {}
    ///You can construct already initialized future like in Future<T>
    SharedFuture(const VoidlessType &val):_ptr(init(val)) {}
    ///You can construct already initialized future like in Future<T>
    SharedFuture(const std::exception_ptr &val):_ptr(init(val)) {}
    ///You can construct already initialized future like in Future<T>
    SharedFuture(VoidlessType &&val):_ptr(init(std::move(val))) {}
    ///You can construct already initialized future like in Future<T>
    SharedFuture(std::exception_ptr &&val):_ptr(init(std::move(val))) {}

    ///You can convert Future<T> to SharedFuture<T> when it is returned from a function
    /**
     * @param fn a function which returns Future<T>. You can use lambda function!
     *
     * @code
     * SharedFuture<T> f([&]{return function_returning_future(args);});
     * @endcode
     *
     */
    template<_details::InvokableWithResult<Future<T> > Fn>
    SharedFuture(Fn &&fn):_ptr(init(std::forward<Fn>(fn))) {
        auto st = _ptr->get_state();
        //if future is in lazy mode, hold reference doesn't need to be called
        //such future can be dropped without evaluation
        if (st == FutureState::pending) _ptr->hold_reference();
    }

    ///Inicialize future and promise at once by spliting code into two branches
    /**
     * @param fn a function, which receives Promise<T> as argument and which can
     * continue to perform operation which leads to resolution of the promise. The
     * promise can be moved to a different thread
     *
     * The constructed Future<T> can be returned from the function as a result
     *
     * @code
     * Future<int> async_call() {
     *      return [&](Promise<int> prom) {
     *          run_async_with_promise(std::move(prom));
     *      };
     * }
     * @endcode
     */
    template<_details::InvokableWithResult<void, Promise> Fn>
    SharedFuture(Fn &&fn):_ptr(init(std::forward<Fn>(fn))) {
        auto st = _ptr->get_state();
        //if future is in lazy mode, hold reference doesn't need to be called
        //such future can be dropped without evaluation
        if (st == FutureState::pending) _ptr->hold_reference();
    }


    ~SharedFuture() {
        reset();
    }

    ///SharedFuture can be copied
    SharedFuture(const SharedFuture &other):_ptr(other._ptr) {_ptr->add_ref();}
    ///SharedFuture can be moved
    SharedFuture(SharedFuture &&other):_ptr(other._ptr) {other._ptr = nullptr;}
    ///SharedFuture can be assigned
    SharedFuture &operator=(const SharedFuture &other) {
        if (this != &other) {
            reset();
            _ptr = other._ptr;
            _ptr->add_ref();
        }
        return *this;
    }
    ///SharedFuture can be moved assigned
    SharedFuture &operator=(SharedFuture &&other) {
        if (this != &other) {
            reset();
            _ptr = other._ptr;
            other._ptr = nullptr;
        }
        return *this;
    }

    ///Resets this instance of shared future
    /**
     * It decrements count of shares. If this is last reference it destroys the future.
     * However if the future is still pending, it stays allocated until it is resolved
     *
     * @note You must avoid waiting on a this future without keeping a reference as well
     */
    void reset() {
        auto ptr = std::exchange(_ptr, nullptr);
        if (ptr && ptr->release_ref()) {
            delete ptr;
        }
    }


    ///synchronous wait
    RetVal wait() {return _ptr->wait();}
    ///synchronous wait
    RetVal get() {return _ptr->get();}
    ///synchronous wait
    operator CastRetVal() {return *_ptr;}


    template<typename Fn>
    requires (std::invocable<Fn> || std::invocable<Fn, Future<T> *>)
    bool callback_async(Fn fn) {
        return _ptr->template callback_async<Fn>(std::forward<Fn>(fn));
    }

    template<typename Fn>
    requires (std::invocable<Fn> || std::invocable<Fn, Future<T> *>)
    bool callback(Fn fn) {
        return _ptr->template callback<Fn>(std::forward<Fn>(fn));
    }

    ///Call a callback when future is resolved
    /**
     * @param fn a callback function, which receives pointer to this future
     */
    template<typename Fn>
    requires (std::invocable<Fn> || std::invocable<Fn, Future<T> *>)
    bool operator >> (Fn fn) {
        return callback(fn);
    }


    template<_details::InvokableWithResult<Future<T> > Fn>
    void operator << (Fn &&fn) {
        reset();
        _ptr = init();
        (*_ptr) << std::forward<Fn>(fn);
    }

    ///Retrieve pending status
    bool is_pending() const {
        return _ptr->is_pending();
    }

    ///Retrieve has_value status
    bool has_value() const {
        return _ptr->has_value();
    }

    ///@see Future::get_promise
    Promise get_promise() {
        reset();
        _ptr = init();
        return _ptr->get_promise();
    }

    using ValueAwaiter = typename Future<T>::ValueAwaiter;
    using ReadyStateAwaiter = typename Future<T>::ReadyStateAwaiter;

    ///support for co_await
    ValueAwaiter operator co_await() {
        return ValueAwaiter(*_ptr);
    }
    ///co_await on resultion, but not value
    ReadyStateAwaiter ready() {
        return ReadyStateAwaiter(*_ptr);
    }

    std::exception_ptr &get_exception_ptr() const noexcept {
        return _ptr->get_exception_ptr();
    }
protected:

    template<typename ... Args>
    RefCntFuture *init(Args && ... args) {
        RefCntFuture *ptr = new RefCntFuture(std::forward<Args>(args)...);
        ptr->add_ref();
        return ptr;
    }

    RefCntFuture *_ptr;


};

#if BOOST_TEST_MODULE == CouroutineAsync
template<typename T>
inline int SharedFuture<T>::dealloc_counter = 0;
#endif


///helper struct which is part of coroutine promise
/** it contains different content for T = void */
template<typename T>
struct AsyncPromiseBase {
    Future<T> *fut = nullptr;
    template<typename ... Args>
    void return_value(Args &&... args) {
        if (fut) fut->set_value(std::forward<Args>(args)...);
    }
};

///Coroutine future proxy
/**
 * The coroutine can use Future<T> or Async<T>. While Future<T> represents already
 * running coroutine (while the future is pending), Async<T> represents prepared
 * coroutine, which is already initialized, but it is suspended at the beginning
 *
 * When you declare Future<T> coroutine, your coroutine is started immediatelly once
 * it is called. In contrast, by declaring Async<T>, you receive this object which
 * allows you to schedule the start of the coroutine. The coroutine is started once
 * it is converted to Future<T>. The object Async<T> allows you to start coroutine detached
 * (so return value is thrown out)
 *
 * @tparam T type of return value
 */
template<typename T>
class [[nodiscard]] Async {
public:

    ///You can declare an empty variable
    Async() = default;
    ///You can move the Async object
    Async(Async &&other):_h(other._h) {other._h = {};}
    ///Allows to destroy suspended coroutine
    ~Async() {if (_h) _h.destroy();}
    ///You can assign from one object to other
    Async &operator=(Async &&other) {
        if (this != &other) {
            if (_h) _h.destroy();
            _h = other._h;
            other._h = {};
        }
        return *this;
    }



    struct promise_type: AsyncPromiseBase<T> {
        typename Future<T>::LazyTarget _lazy_target;
        struct FinalSuspender {
            bool await_ready() const noexcept {
                return _fut == nullptr;
            }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> me)  noexcept {
                auto h = _fut->notify_targets();
                me.destroy();
                return h?h:std::noop_coroutine();
            }
            constexpr void await_resume() const noexcept {}
            Future<T> *_fut;
        };

        constexpr std::suspend_always initial_suspend() const noexcept {return {};}
        FinalSuspender final_suspend() const noexcept {return {this->fut};}
        void unhandled_exception() {
            if (this->fut) {
                this->fut->set_exception(std::current_exception());
            }
        }
        Async<T> get_return_object() {return {this};}

        using Promise = typename Future<T>::Promise;

        static std::coroutine_handle<> bootstrap(Promise p, void *address) {
            auto h = std::coroutine_handle<promise_type>::from_address(address);
            auto &coro = h.promise();
            if (p) {
                coro.fut = p.release();
                return h;
            } else {
                h.destroy();
                return {};
            }
        }

    };


    ///Starts the coroutine
    /**
     * @return Returns future of this coroutine.
     * @note once the coroutine is started, this instance of Async<T> no longer refers
     * to the coroutine.
      */
    Future<T> start() {
        return [&](auto promise){
            auto &p = _h.promise();
            p.fut = promise.release();
            auto h = std::exchange(_h, {});
            h.resume();
        };
    }

    ///Starts the coroutine in lazy mode
    /**
     * Coroutine started in lazy mode is stays suspended, until its value is needed
     *
     * @return Returns future of this coroutine.
     * @note once the coroutine is started, this instance of Async<T> no longer refers
     * to the coroutine.
     */
    Future<T> lazy_start() {
        promise_type &p = _h.promise();
        p._lazy_target = Future<T>::LazyTarget::function(&promise_type::bootstrap, _h.address());
        _h = {};
        return p._lazy_target;
    }

    ///Starts the coroutine - returns SharedFuture
    /**
     * @return Returns instance of SharedFuture of this coroutine.
     * @note once the coroutine is started, this instance of Async<T> no longer refers
     * to the coroutine.
     */
    SharedFuture<T> start_shared() {
        return [&]{return start();};
    }

    ///Starts coroutine detached
    /**
     * @note any returned value is thrown out
     */
    void detach() {
        auto h = std::exchange(_h, {});
        h.resume();
    }

    ///Allows to convert Async<T> to Future<T>
    operator Future<T>() {
        return start();
    }
    ///Allows to convert Async<T> to SharedFuture<T>
    operator SharedFuture<T>() {
        return start_shared();
    }

    explicit operator bool() const {return _h != nullptr;}

    class StartAwaiter {
        std::coroutine_handle<promise_type> _h;
        Future<T> _storage;
        Future<T>::Target _target;
    public:

        StartAwaiter(std::coroutine_handle<promise_type> h):_h(h) {}
        bool await_ready() const {
            return _h == nullptr;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            _target = Future<T>::Target::coroutine(h);
            _storage.register_target(_target);
            return std::exchange(_h, nullptr);
        }
        decltype(auto) await_resume() {
            return _storage.get();
        }
    };


    ///Allows directly co_await to Async object
    /**
     * this is equivalent to co_await coroutine().start()
     * @return awaiter
     */
    StartAwaiter operator co_await() {
        return StartAwaiter(std::exchange(_h, nullptr));
    }


protected:

    Async(promise_type *ptr):_h(std::coroutine_handle<promise_type>::from_promise(*ptr)) {}


    std::coroutine_handle<promise_type> _h;
};


template<>
struct AsyncPromiseBase<void> {
    Future<void> *fut = nullptr;
    void return_void() {
        if (fut) fut->set_value();
    }
};

///AsyncMutex implements mutex suitable for coroutines
/**
 * AsyncMutex works similar as std::mutex, except it tracks ownership
 * using a special object. This allows to keep ownership while coroutine
 * is suspended and after it is resumed in a different thread
 *
 * To acquire mutex, you need to co_await this object. As result, you
 * receive AsyncMutex::Ownership. The lifetime of this object represents
 * ownership of the mutex. Ownership can be moved, but not copied. Once
 * the ownership is dropped, the mutex is unlocked.
 *
 * @code
 * AsyncMutex mx;
 *
 * Future<T> coroutine() {
 *      auto ownership = co_await mx;
 *      //mutex is held
 *      co_await ...
 *      //mutex is held
 *      co_return;
 *      //mutex is released as ownership ends its lifetime here
 *
 * @endcode
 *
 * You can also lock the mutex outside of coroutine, there si method lock_sync().
 * The same rules apply here
 *
 */

class AsyncMutex {
public:

    ///Ownership is implemented as std::unique_ptr with custom deleter. This is the deleter
    struct OwnershipDeleter {
        void operator()(AsyncMutex *m) noexcept {
            auto x =m->unlock();
            if (x) x.resume();
        }
    };

    ///Ownership declaration
    using Ownership = std::unique_ptr<AsyncMutex, OwnershipDeleter>;
    ///Target declaration
    using Target = _details::Target<Ownership &&>;

    ///Tries to lock the mutex without waiting
    /**
     *
     * @return ownership, which is nullptr if lock failed.
     */
    Ownership try_lock() {
        return try_lock_internal()?Ownership(this):Ownership(nullptr);
    }

    ///Implements co_await on the mutex
    class Awaiter {
        AsyncMutex *_owner;
        Target _t;
        std::coroutine_handle<> _w;
    public:
        Awaiter(AsyncMutex &owner):_owner(&owner) {};
        Awaiter(const Awaiter &) = default;
        Awaiter &operator=(const Awaiter &) = delete;


        bool await_ready() noexcept {
            return _owner->try_lock_internal();
        }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            _w = h;
            _t = Target::member_fn<&Awaiter::resume>(this);
            return _owner->register_target_async(_t);
        }

        void resume(Ownership own) noexcept {
            _owner = own.release(); //reuse AsyncMutex * ptr to store ownership
            //it will converted back to Ownership in await_resume
            _w.resume();
        }
        Ownership await_resume() noexcept {
            return Ownership(_owner);
        }

    };

    ///Register target for locking
    /**
     * @note this follows rule about targets, don't use for locking unless you need
     *  to lock with custom target
     *
     * @param t reference to target
     * @retval true target registered and will be activated once the ownership is gained
     * @retval false you gained ownership immediatelly. To retrieve Ownership object, just
     * construct Ownership with pointer to mutex
     */
    bool register_target_async(Target &t) noexcept {
        //push target to _requests
        t.push_to(_requests);
        //check, whether there is "next" target
        if (t.get_next() == nullptr) [[likely]] {
            //if there is nullptr, the mutex was not owned!
            //however we need to put lock_tag and build queue (even empty)
            build_queue(t);
            //target was not registered, return false
            return false;
        } else {
            //target is registered, return true
            return true;
        }
    }

    ///Register target for locking
    /**
     * @note this follows rule about targets, don't use for locking unless you need
     *  to lock with custom target
     *
     * @param t reference to target
     * @retval true target registered and will be activated once the ownership is gained
     * @retval false target activated synchronously
     */
    bool register_target(Target &t) noexcept {
        if (register_target_async(t)) return true;
        t.activate_resume(Ownership(this));
        return false;
    }

    bool register_target(std::unique_ptr<Target> t) {
        return register_target(*t.release());
    }

    ///implement co_await
    Awaiter operator co_await() noexcept {return *this;}

    ///lock synchronously
    Ownership lock_sync() noexcept {
        if (!try_lock_internal()) {
            _details::SyncWaitTarget<Ownership &&> t;
            if (register_target_async(t)) {
                t.wait();
            }
        }
        return Ownership(this);
    }


protected:
    ///Reserved target which marks busy mutex (mutex is owned)
    static constexpr Target locked_tag = {};
    ///List of requests
    /**
     * If mutex is owned, there is always non-null value. This make queue
     * of targets requesting the ownership. The queue is reversed (lifo)
     *
     * Adding new item to the queue is lock-free atomic
     **/
    std::atomic<const Target *> _requests = {nullptr};
    ///Internal queue ordered in correct order
    /**
     * This queue is managed by owner only, so no locking is needed. It contains
     * list of next-owners. When unlock, next target is popped and activated
     */
    const Target *_queue= nullptr;

    ///try_lock internally (without creating ownership)
    bool try_lock_internal() noexcept {
        //to lock, we need nullptr
        const Target *need = nullptr;
        //returns true, if locked_tag was exchanged - so mutex is owned
        return _requests.compare_exchange_strong(need, &locked_tag);
    }

    ///builds queue from _request to _queue
    /** Atomically picks all requests and builds _queue, in reverse order
     * This is perfomed by the owner.
     *
     * @param stop specified target where operation stops, this is the Target object
     * of current owner.
     */
    void build_queue(const Target &stop) noexcept {
        //atomically move requests from public space into private space
        const Target *req = _requests.exchange(&locked_tag);
        //reverse order of the queue and build it to _queue
        while (req && req != &stop) {
            auto x = req;
            req = req->get_next();
            x->push_to(_queue);
        }
    }

    std::coroutine_handle<> unlock() noexcept {
        //current owner is returning ownership, (but it is still owner)
        //check queue in private space.
        if (!_queue) [[likely]] {
            const Target *need = &locked_tag;
            //if queue is empty, try to remove locked_tag from the _requests
            if (_requests.compare_exchange_strong(need, nullptr)) {
                //success, mutex is no longer owned
                return nullptr;
            }
            //failure? process new requests, build new queue
            build_queue(locked_tag);
        }
        //pick first target from queue
        const Target *first = _queue;
        //remove this target from queue
        _queue = _queue->get_next();
        //active the target, transfer ownership
        //awaiting coroutine is resumed here
        return first->activate(Ownership(this));
    }
};

///Implements asychronous queue with support for coroutines
/**
 * Asynchronous queue allows to co_await on new items. You can have multiple
 * coroutines reading the same queue (for push-pull)
 * @tparam T type of item
 * @tparam QueueImpl object which implements the queue, default is std::queue. You
 * can use for example some kind of priority queue or stack, however the
 * object must have the same interface as the std::queue
 */
template<typename T, typename QueueImpl = std::queue<T> >
class AsyncQueue {
public:

    ///Construct default queue
    AsyncQueue() = default;
    ///The queue is not copyable
    AsyncQueue(const AsyncQueue &other) = delete;
    ///The queue is movable
    /**
     *  @param other source queue instance. The queue is moved
     *  with awaiting coroutines.
     *
     *  @note source queue becomes closed. You should avoid MT access during
     *  the moving.
     */
    AsyncQueue(AsyncQueue &&other):_closed(other._closed) {
        std::lock_guard lk(other._mx);
        _awaiters = std::move(other._awaiters);
        _item_queue = std::move(other._item_queue);
        other._closed = true;
    }
    AsyncQueue &operator= (const AsyncQueue &other) = delete;
    AsyncQueue &operator= (AsyncQueue &other) = delete;


    ///Push the item to the queue (emplace)
    /**
     * @param args arguments needed to construct the item.
     * @note function can cause resumption of awaiting coroutine.
     */
    template<typename ... Args>
    void emplace(Args &&... args) {
        std::unique_lock lk(_mx);
        if (_awaiters.empty()) { //no awaiters? push to the queue
            _item_queue.emplace(std::forward<Args>(args)...);
            return;
        }
        //pick first awaiter
        auto prom = std::move(_awaiters.front());
        _awaiters.pop();
        lk.unlock();
        //construct the item directly to the awaiter
        prom(std::forward<Args>(args)...);
    }

    ///Push item to the queue
    /**
     * @param x item to push
     * @note function can cause resumption of awaiting coroutine.
     */
    void push(const T &x) {emplace(x);}
    ///Push item to the queue
    /**
     * @param x item to push
     * @note function can cause resumption of awaiting coroutine.
     */
    void push(T &&x) {emplace(std::move(x));}

    ///Pop the items
    /**
     * @return return Future wich is eventually resolved with an item.
     * @note The promise can be broken by calling the function close()
     */
    Future<T> pop() {
        return [&](auto prom) {
            std::unique_lock lk(_mx);
            if (_item_queue.empty()) {
                if (_closed) return; //breaks promise
                _awaiters.push(std::move(prom)); //remember promise
                return;
            }
            auto ntf = prom(std::move(_item_queue.front())); //resolve promise
            _item_queue.pop();
            lk.unlock();
            //ntf - performs notify now
        };
    }

    ///Pops item non-blocking way
    /**
     * @return poped item if there is such item, or no-value if not
     */
    std::optional<T> try_pop() {
        std::unique_lock lk(_mx);
        if (_item_queue.empty()) return {};
        std::optional<T> out (std::move(_item_queue.front()));
        _item_queue.pop();
        return out;
    }

    ///determines whether queue is empty
    /**
     * @retval true queue is empty
     * @retval false queue is not empty
     * @note in MT environment, this value can be inaccurate. If you want
     * to check queue before pop, it is better to use try_pop()
     */
    bool empty() const {
        std::lock_guard _(_mx);
        return _item_queue.empty();
    }
    ///retrieve current size of the queue
    /**
     * @return current size of the queue
     */
    std::size_t size() const {
        std::lock_guard _(_mx);
        return _item_queue.size();
    }
    ///remove all items from the queue
    void clear() {
        std::lock_guard _(_mx);
        _item_queue = {};
    }
    ///close the queue
    /** closed queue means, that any awaiting coroutine
     * immediately receives BrokenPromise. Any attempt to
     * pop() also recives BrokenPromise when the queue is empty.
     * In thi state, it is still possible to push and pop items
     * but without blocking.
     */
    void close() {
        std::unique_lock lk(_mx);
        auto z = std::move(_awaiters);
        _closed = true;
        lk.unlock();
        while (!z.empty()) z.pop(); //break all promises
    }

    ///Reactivates the queue
    void reopen() {
        std::lock_guard _(_mx);
        _closed = false;
    }


protected:
    mutable std::mutex _mx;
    QueueImpl _item_queue;
    std::queue<typename Future<T>::Promise> _awaiters;
    bool _closed = false;
};

template<typename Fn>
class AsyncTransferExecution {
public:
    constexpr AsyncTransferExecution(Fn fn):_fn(std::move(fn)) {}
    constexpr bool await_ready() const {return false;}
    void await_suspend(std::coroutine_handle<> h) const {
        _fn(h);
    }
    constexpr void await_resume() const {};

protected:
    Fn _fn;
};

template<typename T>
AsyncTransferExecution(T x) -> AsyncTransferExecution<T>;

}

#endif
