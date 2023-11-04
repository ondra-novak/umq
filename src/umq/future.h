#ifndef SRC_UMQ_FUTURE_H_
#define SRC_UMQ_FUTURE_H_

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <variant>
#include <functional>
#include <concepts>
#include <memory>

using std::__exception_ptr::exception_ptr;

namespace umq {

namespace _helper {

template<typename X>
struct ExtractObjectType;

template<typename Obj, typename Ret, typename ... Args>
struct ExtractObjectType<Ret (Obj::*)(Args...) noexcept> {
    using value = Obj;
};

template<typename T, typename R, typename ... Args>
concept InvokableWithResult = std::is_same_v<std::invoke_result_t<T, Args...>, R>;


}

///Construct future type which supports coroutines, callbacks and member calls
template<typename T>
class Future {
public:

    class BrokerPromise: public std::exception {
    public:
        const char *what() const noexcept override {return "Broken promise (umq::Future::Promise)";}
    };

    class NotReady: public std::exception {
    public:
        const char *what() const noexcept override {return "Future is not resolved yet (umq::Future)";}
    };
    class AlreadyPending: public std::exception {
    public:
        const char *what() const noexcept override {return "Future is already pending (umq::Future)";}
    };


    ///Promise type
    class Promise {
    public:
        Promise():_ptr(nullptr) {}
        Promise(Future *f):_ptr(f) {}
        Promise(Promise &&other):_ptr(other._ptr.exchange(nullptr, std::memory_order_relaxed)) {}
        Promise(const Promise &other) = delete;
        ~Promise() {
            if (*this) reject();
        }

        Promise &operator=(Promise &&other) noexcept {
            if (this != &other) {
                auto x = other._ptr.exchange(nullptr, std::memory_order_relaxed);
                if (*this) reject();
                _ptr.store(x, std::memory_order_relaxed);
            }
            return *this;
        }
        Promise &operator=(const Promise &other) = delete;

        ///resolve promise
        template<typename ... Args>
        void operator()(Args && ... args) {
            auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
            if (x) x->resolve(std::forward<Args>(args)...);
        }

        ///reject promise with exception
        template<typename Exception>
        void reject(Exception && except) {
            auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
            if (x) x->reject(std::forward<Exception>(except));
        }

        explicit operator bool() const {
            return _ptr.load(std::memory_order_relaxed) != nullptr;
        }

        ///Reject under catch handler
        void reject() {
            auto x = _ptr.exchange(nullptr, std::memory_order_relaxed);
            if (x) {
                auto exp = std::current_exception();
                if (exp) {
                    x->reject(std::move(exp));
                } else {
                    x->reject(BrokerPromise());
                }
            }
        }




    protected:
        std::atomic<Future *> _ptr;
    };

    static constexpr bool is_void = std::is_void_v<T>;
    using value_type = T;

    using FastCallback = void (*)(Future *fut, void *user_ptr);
    using Callback = std::function<void(Future *fut)>;
    using VoidlessType = std::conditional_t<is_void, bool, T>;
    using RetVal = std::add_lvalue_reference_t<T>;


    Future():_state(dormant) {}
    Future(const VoidlessType &val):_state(resolved), _value(val) {}
    Future(const std::exception_ptr &val):_state(resolved), _value(val) {}
    Future(VoidlessType &&val):_state(resolved), _value(std::move(val)) {}
    Future(std::exception_ptr &&val):_state(resolved), _value(std::move(val)) {}
    ~Future() {
        assert(_state == dormant || _state == resolved);
    }

    template<_helper::InvokableWithResult<void, Promise> Fn>
    Future(Fn &&fn):_state(unresolved) {
        fn(Promise(this));
    }

    Promise get_promise() {
        State need = State::dormant;
        if (_state.compare_exchange_strong(need, State::unresolved,std::memory_order_relaxed)) {
            return Promise(this);
        }
        throw AlreadyPending();
    }


    RetVal await_resume() {
        if (_state != resolved) throw NotReady();
        if (std::holds_alternative<std::exception_ptr>(_value)) {
            std::rethrow_exception(std::get<std::exception_ptr>(_value));
        }
        if constexpr(std::is_void_v<T>) {
            return;
        } else {
            return std::get<VoidlessType>(_value);
        }
    }

    RetVal wait() {
        _state.wait(unresolved, std::memory_order_acquire);
        return await_resume();

    }

    RetVal get() {
        return wait();
    }

    operator VoidlessType &() {
        if constexpr(is_void) {
            wait();
            return std::get<VoidlessType>(_value);

        } else {
            return wait();
        }
    }

    ///Invoke member call when operation finished asynchronously
    /**Member function is not invoked, when future is already resolved
     *
     * @tparam fn pointer to member function &Fn::method(Future *)
     * @param ptr pointer to object
     * @retval true success, operation will continue asynchronously
     * @retval false failure, operation is complete already
     */
    template<auto fn>
    bool invoke_async(typename _helper::ExtractObjectType<decltype(fn)>::value *ptr) {
        _user_ptr = ptr;
        _consument.template emplace<FastCallback>([](Future *f, void *user_ptr){
            auto ptr = reinterpret_cast<typename _helper::ExtractObjectType<decltype(fn)>::value *>(user_ptr);
            (ptr->*fn)(f);
        });
        State st = unresolved;
        return _state.compare_exchange_strong(st, awaited, std::memory_order_acquire);
    }

    ///Invoke member call when operation finishes
    /**
     * @tparam fn pointer to member function &Fn::method(Future *)
     * @param ptr pointer to object
     */
    template<auto fn>
    void invoke(typename _helper::ExtractObjectType<decltype(fn)>::value *ptr) {
        if (!invoke_async<fn>(ptr)) (ptr->*fn)(this);
    }

    template<std::invocable<Future *> Fn>
    bool callback_async(Fn &&fn) {
        _consument.template emplace<Callback>(std::forward<Fn>(fn));
        State st = unresolved;
        return _state.compare_exchange_strong(st, awaited, std::memory_order_acquire);

    }

    template<std::invocable<Future *> Fn>
    void callback(Fn &&fn) {
        if (!callback_async(std::forward<Fn>(fn))) {
            std::get<Callback>(_consument)(this);
        }
    }

    bool callback_async(FastCallback cb, void *user_ptr) {
        _user_ptr = user_ptr;
        _consument.template emplace<FastCallback>(cb);
        State st = unresolved;
        return _state.compare_exchange_strong(st, awaited, std::memory_order_acquire);

    }

    void callback(FastCallback cb, void *user_ptr) {
        if (!callback_async(cb, user_ptr)) {
            cb(this, user_ptr);
        }
    }

    template<std::invocable<Future *> Fn>
    void operator>>(Fn &&fn) {
        callback(std::forward<Fn>(fn));
    }

    bool await_ready() const {
        return _state.load(std::memory_order_relaxed) == resolved;
    }
    bool await_suspend(std::coroutine_handle<> h) {
        _consument.template emplace<std::coroutine_handle<> >(h);
        State st = unresolved;
        return (_state.compare_exchange_strong(st, awaited, std::memory_order_acquire));
    }

    template<_helper::InvokableWithResult<Future<T> > Fn>
    void operator << (Fn &&fn) {
        std::destroy_at(this);
        try {
            new(this) Future(fn());
        } catch (...) {
            new(this) Future(std::current_exception());
        }
    }

    bool is_pending() const {
        State st = _state.load(std::memory_order_relaxed);
        return  st == State::unresolved || st == State::awaited;
    }


protected:

    enum State {
        ///future is dormant, no promise is in fly
        dormant,
        ///future is not resolved and not awaited
        unresolved,
        ///future is resolved, don't need to be awaited
        resolved,
        ///future is awaited, so resolve immediately
        awaited
    };
    std::atomic<State> _state = {dormant};

    std::variant<std::monostate, VoidlessType, std::exception_ptr> _value;
    std::variant<std::monostate, std::coroutine_handle<>, FastCallback, Callback> _consument;
    void *_user_ptr = nullptr;

    template<typename X, typename ... Args>
    void resolve_gen(Args && ... args) {
        if constexpr(is_void) {
            _value = true;
        } else {
            _value.template emplace<X>(std::forward<Args>(args)...);
        }
        State st = _state.exchange(resolved, std::memory_order_release);
        if (st == awaited) {
            switch (_consument.index()) {
                default:
                case 0: ; break;
                case 1: std::get<std::coroutine_handle<> >(_consument).resume(); break;
                case 2: std::get<FastCallback>(_consument)(this, _user_ptr); break;
                case 3: std::get<Callback>(_consument)(this); break;
            }
        } else {
            _state.notify_all();
        }
    }

    template<typename ... Args>
    void resolve(Args && ... args) {
        resolve_gen<T>(std::forward<Args>(args)...);
    }
    template<typename Exception>
    void reject(Exception && args) {
        resolve_gen<std::exception_ptr>(std::make_exception_ptr(std::forward<Exception>(args)));
    }
    void reject(const std::exception_ptr &ptr) {
        resolve_gen<std::exception_ptr>(ptr);
    }
    void reject(std::exception_ptr &&ptr) {
        resolve_gen<std::exception_ptr>(std::move(ptr));
    }


};

template<typename T>
using Promise = typename Future<T>::Promise;


///SharedFuture is less efficient but sharable future
/**
 * SharedFuture act as Future, but it can be shared by copying. It is always allocated
 * at heap. It also can be left unresolved, which causes that memory is released
 * once the future is resolved
 * @tparam T type of future
 */
template<typename T>
class SharedFuture {

    class RefCntFuture: public Future<T> {
    public:
        using Future<T>::Future;

        void add_ref() {_refcnt.fetch_add(1, std::memory_order_relaxed);}
        bool release_ref() {return _refcnt.fetch_sub(1, std::memory_order_release) < 1;}
    protected:
        std::atomic<unsigned int> _refcnt;
    };


public:

    using value_type = T;
    using VoidlessType = typename Future<T>::VoidlessType;
    using Promise = typename Future<T>::Promise;
    using RetVal = typename Future<T>::RetVal;
    using FastCallback = typename Future<T>::FastCallback;

    SharedFuture():_ptr(nullptr) {}
    SharedFuture(const VoidlessType &val):_ptr(init(val)) {}
    SharedFuture(const std::exception_ptr &val):_ptr(init(val)) {}
    SharedFuture(VoidlessType &&val):_ptr(init(std::move(val))) {}
    SharedFuture(std::exception_ptr &&val):_ptr(init(std::move(val))) {}

    template<_helper::InvokableWithResult<Future<T> > Fn>
    SharedFuture(Fn &&fn):_ptr(init()) {
        (*_ptr) << std::forward<Fn>(fn);
    }

    ~SharedFuture() {
        reset();
    }
    SharedFuture(const SharedFuture &other):_ptr(other._ptr) {_ptr->add_ref();}
    SharedFuture(SharedFuture &&other):_ptr(other._ptr) {other._ptr = nullptr;}
    SharedFuture &operator=(const SharedFuture &other) {
        if (this != &other) {
            reset();
            _ptr = other._ptr;
            _ptr->add_ref();
        }
        return *this;
    }
    SharedFuture &operator=(SharedFuture &&other) {
        if (this != &other) {
            reset();
            _ptr = other._ptr;
            other._ptr = nullptr;
        }
        return *this;
    }

    void reset() {
        if (_ptr && _ptr->release_ref()) {
            if (_ptr->is_pending()) {
                _ptr->callback([](Future<T> *f, void *){
                    delete static_cast<RefCntFuture *>(f);
                }, nullptr);
            } else {
                delete _ptr;
            }
        }
    }

    template<_helper::InvokableWithResult<void, Promise> Fn>
    SharedFuture(Fn &&fn):_ptr(init(std::forward<Fn>(fn))) {}

    RetVal await_resume() {return _ptr->await_resume();}
    RetVal wait() {return _ptr->wait();}
    RetVal get() {return _ptr->get();}
    operator VoidlessType &() {return *_ptr;}
    template<auto fn>
    bool invoke_async(typename _helper::ExtractObjectType<decltype(fn)>::value *ptr) {
        return _ptr->template invoke_async<fn>(ptr);
    }
    template<auto fn>
    void invoke(typename _helper::ExtractObjectType<decltype(fn)>::value *ptr) {
        return _ptr->template invoke<fn>(ptr);
    }
    template<std::invocable<Future<T> *> Fn>
    bool callback_async(Fn &&fn) {
        return _ptr->callback_async(std::forward<Fn>(fn));
    }
    template<std::invocable<Future<T> *> Fn>
    void callback(Fn &&fn) {
        return _ptr->callback(std::forward<Fn>(fn));
    }
    bool callback_async(FastCallback cb, void *user_ptr) {
        return _ptr->callback_async(cb, user_ptr);

    }

    void callback(FastCallback cb, void *user_ptr) {
        return _ptr->callback(cb, user_ptr);
    }

    template<std::invocable<Future<T> *> Fn>
    void operator>>(Fn &&fn) {
        callback(std::forward<Fn>(fn));
    }

    bool await_ready() const {
        return _ptr->await_ready();
    }
    bool await_suspend(std::coroutine_handle<> h) {
        return _ptr->await_suspend(h);
    }

    template<_helper::InvokableWithResult<Future<T> > Fn>
    void operator << (Fn &&fn) {
        reset();
        _ptr = init();
        (*_ptr) << std::forward<Fn>(fn);
    }

    bool is_pending() const {
        return _ptr->is_pending();
    }

    Promise get_promise() {
        reset();
        _ptr = init();
        return _ptr->get_promise();
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

}



#endif /* SRC_UMQ_FUTURE_H_ */
