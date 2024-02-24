#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <map>
#include <print>
#include <stdexcept>

namespace cppasync {

template <typename T>
class async;

class broken_promise : public std::logic_error {
    public:
        broken_promise() : std::logic_error("broken promise") {}
        explicit broken_promise(const std::string& what) : logic_error(what) {}
        explicit broken_promise(const char* what) : logic_error(what) {}
};

class broken_resume : public std::logic_error {
    public:
        broken_resume() : std::logic_error("broken resume") {}
        explicit broken_resume(const std::string& what) : logic_error(what) {}
        explicit broken_resume(const char* what) : logic_error(what) {}
};

class unfinished_promise : public std::logic_error {
    public:
        unfinished_promise() : std::logic_error("unfinished promise") {}
        explicit unfinished_promise(const std::string& what) : logic_error(what) {}
        explicit unfinished_promise(const char* what) : logic_error(what) {}
};

#ifdef _COROUTINE_DEBUG
extern unsigned promise_sn_counter;
extern unsigned async_sn_counter;
extern unsigned awaitable_sn_counter;
extern unsigned promise_use_counter;
extern unsigned async_use_counter;
extern unsigned awaitable_use_counter;
extern std::coroutine_handle<> global_continuation;
unsigned getSNforHandle(std::coroutine_handle<> handle);
inline void resetCounters() {
    promise_sn_counter = async_sn_counter = awaitable_sn_counter = promise_use_counter = async_use_counter = awaitable_use_counter = 0;
}
#endif

namespace detail {

class async_promise_base {
        std::coroutine_handle<> m_parent;
        struct final_awaitable {
#ifdef _COROUTINE_DEBUG
                unsigned sn;
                final_awaitable(unsigned sn) : sn(sn) {
                    ++awaitable_use_counter;
                    std::println("  awaitable #{} created", sn);
                }
                ~final_awaitable() {
                    --awaitable_use_counter;
                    std::println("awaitable #{}: destroyed", sn);
                }
                bool await_ready() const noexcept {
                    std::println("final_awaitable #{}: await_ready -> false", sn);
                    return false;
                }
#else
                bool await_ready() const noexcept { return false; }
#endif
                template <typename PROMISE>
                std::coroutine_handle<> await_suspend(std::coroutine_handle<PROMISE> coro) noexcept {
                    auto continuation = coro.promise().m_parent;
                    if (!continuation) {
#ifdef _COROUTINE_DEBUG
                        std::println("final_awaitable #{}: await_suspend() -> done, consider destroying promise #{}", sn, getSNforHandle(coro));
#endif
                        // this is the right location to destroy coro but "if (!continuation)" can't be used as the parent
                        // is set later... maybe either async must deleted it when there never was a suspend or when the
                        // associated async had an no_wait() call, then the promise was marked for deletion and it's being
                        // deleted here?
                        if (coro.promise().drop && coro.done()) {
                            coro.destroy();
                        }
                        return std::noop_coroutine();
                    }
                    coro.promise().m_parent = nullptr;
#ifdef _COROUTINE_DEBUG
                    std::println("awaitable #{}: await_suspend() -> continue with promise #{}", sn, getSNforHandle(continuation));
#endif
                    return continuation;
                }
                void await_resume() noexcept {
#ifdef _COROUTINE_DEBUG
                    std::println("awaitable #{}: await_resume()", sn);
#endif
                }
        };

    public:
        bool drop = false;
        const std::function<void(std::exception& error)>* fail = nullptr;
#ifdef _COROUTINE_DEBUG
        unsigned sn;
        async_promise_base() noexcept {
            ++promise_use_counter;
            sn = ++promise_sn_counter;
            // std::println("  create async_promise_base #{}", sn);
        }
        ~async_promise_base() {
            --promise_use_counter;
            std::println("promise #{}: destroyed", sn);
        }
#else
        async_promise_base() noexcept {}
#endif

        // set the coroutine to proceed with after this coroutine is finished
        void set_parent(std::coroutine_handle<> parent) noexcept { m_parent = parent; }

        std::suspend_never initial_suspend() { return {}; }
        final_awaitable final_suspend() noexcept {
#ifdef _COROUTINE_DEBUG
            auto asn = ++awaitable_sn_counter;
            std::println("promise #{}: final_suspend() -> create and return final awaitable #{}", sn, asn);
            return {asn};
#else
            return {};
#endif
        }
};
}  // namespace detail

#ifdef _COROUTINE_DEBUG
inline unsigned getSNforHandle(std::coroutine_handle<> handle) { return ((std::coroutine_handle<detail::async_promise_base>*)&handle)->promise().sn; }
#endif

namespace detail {

// the VALUE specialisation of async_promise_base
template <typename T>
class async_promise final : public async_promise_base {
    public:
        async_promise() noexcept {}
        ~async_promise() {
            switch (m_resultType) {
                case result_type::value:
                    if (then != nullptr) {
                        (*then)(m_value);
                    }
                    m_value.~T();
                    break;
                case result_type::exception:
                    if (fail != nullptr) {
                        try {
                            std::rethrow_exception(m_exception);
                        } catch (std::exception& e) {
                            (*fail)(e);
                        } catch (...) {
                        }
                    }
                    m_exception.~exception_ptr();
                    break;
                default:
                    break;
            }
        }
        async<T> get_return_object() noexcept;
        void unhandled_exception() noexcept {
            ::new (static_cast<void*>(std::addressof(m_exception))) std::exception_ptr(std::current_exception());
            m_resultType = result_type::exception;
        }

        template <typename VALUE, typename = std::enable_if_t<std::is_convertible_v<VALUE&&, T>>>
        void return_value(VALUE&& value) noexcept(std::is_nothrow_constructible_v<T, VALUE&&>) {
            ::new (static_cast<void*>(std::addressof(m_value))) T(std::forward<VALUE>(value));
            m_resultType = result_type::value;
        }

        T& result() & {
            if (m_resultType == result_type::exception) {
                std::rethrow_exception(m_exception);
            }
            assert(m_resultType == result_type::value);
            return m_value;
        }

        using rvalue_type = std::conditional_t<std::is_arithmetic_v<T> || std::is_pointer_v<T>, T, T&&>;
        rvalue_type result() && {
            if (m_resultType == result_type::exception) {
                std::rethrow_exception(m_exception);
            }
            assert(m_resultType == result_type::value);
            return std::move(m_value);
        }

        const std::function<void(const T response)>* then = nullptr;

    private:
        enum class result_type { empty, value, exception };
        result_type m_resultType = result_type::empty;
        union {
                T m_value;
                std::exception_ptr m_exception;
        };
};

// the VOID specialisation of async_promise_base
template <>
class async_promise<void> : public async_promise_base {
    public:
        async_promise() noexcept {
#ifdef _COROUTINE_DEBUG
            // std::println("  create async_promise<> #{}", sn);
#endif
        }
        ~async_promise() {
            if (m_exception) {
                if (fail != nullptr) {
                    try {
                        std::rethrow_exception(m_exception);
                    } catch (std::exception& e) {
                        (*fail)(e);
                    } catch (...) {
                    }
                }
            } else {
                if (then != nullptr) {
                    (*then)();
                }
            }
        }
        async<void> get_return_object() noexcept;
        void return_void() noexcept {
#ifdef _COROUTINE_DEBUG
            std::println("promise #{}: return_void()", sn);
#endif
        }
        void unhandled_exception() noexcept { m_exception = std::current_exception(); }
        void result() {
#ifdef _COROUTINE_DEBUG
            std::println("promise #{}: result()", sn);
#endif
            if (m_exception) {
                std::rethrow_exception(m_exception);
            }
        }

        const std::function<void()>* then = nullptr;

    private:
        std::exception_ptr m_exception;
};

// the REFERENCE specialisation of async_promise_base
template <typename T>
class async_promise<T&> : public async_promise_base {
    public:
        async_promise() noexcept = default;
        ~async_promise() {
            // if (then != nullptr) {
            //     (*then)(m_value);
            // }
        }
        async<T&> get_return_object() noexcept;
        void unhandled_exception() noexcept { m_exception = std::current_exception(); }
        void return_value(T& value) noexcept { m_value = std::addressof(value); }
        T& result() {
            if (m_exception) {
                std::rethrow_exception(m_exception);
            }
            return *m_value;
        }
        const std::function<void(const T& response)>* then = nullptr;

    private:
        T* m_value = nullptr;
        std::exception_ptr m_exception;
};
}  // namespace detail

template <typename T>
class [[nodiscard]] async {
    public:
        using promise_type = detail::async_promise<T>;
        using handle_type = std::coroutine_handle<promise_type>;
        using value_type = T;

    private:
        handle_type m_coroutine;
#ifdef _COROUTINE_DEBUG
    public:
        unsigned sn;

    private:
#endif
        struct awaitable_base {
                handle_type m_coroutine;
                unsigned sn;
#ifdef _COROUTINE_DEBUG
                awaitable_base(handle_type coroutine, unsigned sn) noexcept : m_coroutine(coroutine), sn(sn) {
                    ++awaitable_use_counter;
                    // std::println("   create awaitable_base(coroutine) #{}", this->sn);
                }
                ~awaitable_base() { --awaitable_use_counter; }
                bool await_ready() const noexcept {
                    std::println("awaitable #{} for promise #{}: await_ready() -> false", this->sn, getSNforHandle(m_coroutine));
                    return false;
                }
#else
                awaitable_base(handle_type coroutine) noexcept : m_coroutine(coroutine) {}
                bool await_ready() const noexcept { return false; }
#endif
                bool await_suspend(std::coroutine_handle<> parent) noexcept {
#ifdef _COROUTINE_DEBUG
                    std::println("awaitable #{} for promise #{}: await_suspend() -> set promise #{} as parent and suspend", this->sn,
                                 getSNforHandle(m_coroutine), getSNforHandle(parent));
#endif
                    m_coroutine.promise().set_parent(parent);
                    return !m_coroutine.done();
                }
        };

    public:
        async() noexcept : m_coroutine(nullptr) {
#ifdef _COROUTINE_DEBUG
            ++async_use_counter;
            sn = ++async_sn_counter;
            // std::println("  create async<>() #{}", sn);
#endif
        }
        explicit async(handle_type coroutine) : m_coroutine(coroutine) {
#ifdef _COROUTINE_DEBUG
            ++async_use_counter;
            sn = ++async_sn_counter;
            // std::println("  create async<>(coroutine) #{}", sn);
#endif
        }
        async(async&& t) noexcept : m_coroutine(t.m_coroutine) {
#ifdef _COROUTINE_DEBUG
            ++async_use_counter;
            sn = ++async_sn_counter;
            // std::println("  create async<>()&& #{} from #{}", sn, t.sn);
#endif
            t.m_coroutine = nullptr;
        }

    private:
        async(const async&) = delete;
        async& operator=(const async&) = delete;

    public:
        ~async() noexcept(false) {
#ifdef _COROUTINE_DEBUG
            --async_use_counter;
            if (m_coroutine) {
                std::println("async #{} destroyed, also destroy promise #{}", sn, getSNforHandle(m_coroutine));
                if (!m_coroutine.done()) {
                    std::println("async #{} destroyed, also destroy promise #{} BUT IT'S NOT DONE", sn, getSNforHandle(m_coroutine));
                }
            } else {
                std::println("async #{} destroyed, no promise to destroy", sn);
            }
#endif
            if (m_coroutine) {
                if (!m_coroutine.done()) {
                    m_coroutine.destroy();
                    throw unfinished_promise();
                }
                m_coroutine.destroy();
            }
        }

        async& operator=(async&& other) noexcept {
            if (std::addressof(other) != this) {
                if (m_coroutine) {
                    m_coroutine.destroy();
                }
                m_coroutine = other.m_coroutine;
                other.m_coroutine = nullptr;
            }
            return *this;
        }

        auto operator co_await() const& noexcept {
            struct awaitable : awaitable_base {
                    using awaitable_base::awaitable_base;
                    decltype(auto) await_resume() {
                        if (!this->m_coroutine) {
                            throw broken_promise{};
                        }
#ifdef _COROUTINE_DEBUG
                        std::println("awaitable #{} for co_await()&: await_resume() -> return value from promise #{}", this->sn,
                                     getSNforHandle(this->m_coroutine));
#endif
                        return this->m_coroutine.promise().result();
                    }
            };
#ifdef _COROUTINE_DEBUG
            auto asn = ++awaitable_sn_counter;
            std::println("async #{} co_await&: create awaitable #{} for promise #{}", sn, asn, getSNforHandle(m_coroutine));
            return awaitable{m_coroutine, asn};
#else
            return awaitable{m_coroutine};
#endif
        }
        auto operator co_await() const&& noexcept {
            struct awaitable : awaitable_base {
                    using awaitable_base::awaitable_base;
                    decltype(auto) await_resume() {
                        if (!this->m_coroutine) {
                            throw broken_promise{};
                        }
#ifdef _COROUTINE_DEBUG
                        std::println("awaitable #{} for co_await()&&: await_resume() -> return value from promise #{}", this->sn,
                                     getSNforHandle(this->m_coroutine));
#endif
                        return std::move(this->m_coroutine.promise()).result();
                    }
            };
#ifdef _COROUTINE_DEBUG
            auto asn = ++awaitable_sn_counter;
            std::println("async #{} co_await&&: create awaitable #{} for promise #{}", sn, asn, getSNforHandle(m_coroutine));
            return awaitable{m_coroutine, asn};
#else
            return awaitable{m_coroutine};
#endif
        }
        void no_wait() {
            if (!m_coroutine.done()) {
                m_coroutine.promise().drop = true;
                m_coroutine = nullptr;
            }
        }
        async<T>& then(const std::function<void(T response)>& callback) {
            if (m_coroutine) {
                if (!m_coroutine.done()) {
                    m_coroutine.promise().then = &callback;
                    m_coroutine.promise().drop = true;
                    m_coroutine = nullptr;
                } else {
                    callback(m_coroutine.promise().result());  // THIS THROWS!!!
                }
            }
            return *this;
        }
        async<T>& thenOrCatch(const std::function<void(T response)>& response_cb, const std::function<void(std::exception& error)>& exception_cb) {
            if (m_coroutine) {
                if (!m_coroutine.done()) {
#ifdef _COROUTINE_DEBUG
                    std::println("async<T>::thenOrCatch(): decouple from promise and set fail callback");
#endif
                    m_coroutine.promise().then = &response_cb;
                    m_coroutine.promise().fail = &exception_cb;
                    m_coroutine.promise().drop = true;
                    m_coroutine = nullptr;
                } else {
#ifdef _COROUTINE_DEBUG
                    std::println("async<T>::thenOrCatch(): run ");
#endif
                    try {
                        response_cb(m_coroutine.promise().result());
                    } catch (std::exception& ex) {
                        exception_cb(ex);
                    }
                }
#ifdef _COROUTINE_DEBUG
            } else {
                std::println("async<T>::thenOrCatch(): no coroutine");
#endif
            }
            return *this;
        }
};

template <>
class [[nodiscard]] async<void> {
    public:
        using promise_type = detail::async_promise<void>;
        using handle_type = std::coroutine_handle<promise_type>;

    private:
        handle_type m_coroutine;
#ifdef _COROUTINE_DEBUG
    public:
        unsigned sn;

    private:
#endif
        struct awaitable_base {
                handle_type m_coroutine;
                unsigned sn;
#ifdef _COROUTINE_DEBUG
                awaitable_base(handle_type coroutine, unsigned sn) noexcept : m_coroutine(coroutine), sn(sn) {
                    ++awaitable_use_counter;
                    // std::println("   create awaitable_base(coroutine) #{}", this->sn);
                }
                ~awaitable_base() { --awaitable_use_counter; }
                bool await_ready() const noexcept {
                    std::println("awaitable #{} for promise #{}: await_ready() -> false", this->sn, getSNforHandle(m_coroutine));
                    return false;
                }
#else
                awaitable_base(handle_type coroutine) noexcept : m_coroutine(coroutine) {}
                bool await_ready() const noexcept { return false; }
#endif
                bool await_suspend(std::coroutine_handle<> parent) noexcept {
#ifdef _COROUTINE_DEBUG
                    std::println("awaitable #{} for promise #{}: await_suspend() -> set promise #{} as parent and suspend", this->sn,
                                 getSNforHandle(m_coroutine), getSNforHandle(parent));
#endif
                    m_coroutine.promise().set_parent(parent);
                    return !m_coroutine.done();
                }
        };

    public:
        async() noexcept : m_coroutine(nullptr) {
#ifdef _COROUTINE_DEBUG
            ++async_use_counter;
            sn = ++async_sn_counter;
            // std::println("  create async<>() #{}", sn);
#endif
        }
        explicit async(handle_type coroutine) : m_coroutine(coroutine) {
#ifdef _COROUTINE_DEBUG
            ++async_use_counter;
            sn = ++async_sn_counter;
            // std::println("  create async<>(coroutine) #{}", sn);
#endif
        }
        async(async&& t) noexcept : m_coroutine(t.m_coroutine) {
#ifdef _COROUTINE_DEBUG
            ++async_use_counter;
            sn = ++async_sn_counter;
            // std::println("  create async<>()&& #{} from #{}", sn, t.sn);
#endif
            t.m_coroutine = nullptr;
        }

    private:
        async(const async&) = delete;
        async& operator=(const async&) = delete;

    public:
        ~async() noexcept(false) {
#ifdef _COROUTINE_DEBUG
            --async_use_counter;
            if (m_coroutine) {
                std::println("async #{} destroyed, also destroy promise #{}", sn, getSNforHandle(m_coroutine));
                if (!m_coroutine.done()) {
                    std::println("async #{} destroyed, also destroy promise #{} BUT IT'S NOT DONE", sn, getSNforHandle(m_coroutine));
                }
            } else {
                std::println("async #{} destroyed, no promise to destroy", sn);
            }
#endif
            if (m_coroutine) {
                if (!m_coroutine.done()) {
                    if (!m_coroutine.promise().drop) {
                        m_coroutine.destroy();
                        throw unfinished_promise();
                    }
                } else {
                    m_coroutine.destroy();
                }
            }
        }

        async& operator=(async&& other) noexcept {
            if (std::addressof(other) != this) {
                if (m_coroutine) {
                    m_coroutine.destroy();
                }
                m_coroutine = other.m_coroutine;
                other.m_coroutine = nullptr;
            }
            return *this;
        }

        auto operator co_await() const& noexcept {
            struct awaitable : awaitable_base {
                    using awaitable_base::awaitable_base;
                    decltype(auto) await_resume() {
                        if (!this->m_coroutine) {
                            throw broken_promise{};
                        }
#ifdef _COROUTINE_DEBUG
                        std::println("awaitable #{} for co_await()&: await_resume() -> return value from promise #{}", this->sn,
                                     getSNforHandle(this->m_coroutine));
#endif
                        return this->m_coroutine.promise().result();
                    }
            };
#ifdef _COROUTINE_DEBUG
            auto asn = ++awaitable_sn_counter;
            std::println("async #{} co_await&: create awaitable #{} for promise #{}", sn, asn, getSNforHandle(m_coroutine));
            return awaitable{m_coroutine, asn};
#else
            return awaitable{m_coroutine};
#endif
        }
        auto operator co_await() const&& noexcept {
            struct awaitable : awaitable_base {
                    using awaitable_base::awaitable_base;
                    decltype(auto) await_resume() {
                        if (!this->m_coroutine) {
                            throw broken_promise{};
                        }
#ifdef _COROUTINE_DEBUG
                        std::println("awaitable #{} for co_await()&&: await_resume() -> return value from promise #{}", this->sn,
                                     getSNforHandle(this->m_coroutine));
#endif
                        return std::move(this->m_coroutine.promise()).result();
                    }
            };
#ifdef _COROUTINE_DEBUG
            auto asn = ++awaitable_sn_counter;
            std::println("async #{} co_await&&: create awaitable #{} for promise #{}", sn, asn, getSNforHandle(m_coroutine));
            return awaitable{m_coroutine, asn};
#else
            return awaitable{m_coroutine};
#endif
        }
        void no_wait() {
            if (!m_coroutine.done()) {
                m_coroutine.promise().drop = true;
                m_coroutine = nullptr;
            }
        }
        async<void>& then(const std::function<void()>& callback) {
            if (!m_coroutine.done()) {
                m_coroutine.promise().drop = true;
                m_coroutine.promise().then = &callback;
                m_coroutine = nullptr;
            } else {
                callback();
            }
            return *this;
        }
        async<void>& thenOrCatch(const std::function<void()>& response_cb, const std::function<void(std::exception& error)>& exception_cb) {
            if (m_coroutine) {
                if (!m_coroutine.done()) {
#ifdef _COROUTINE_DEBUG
                    std::println("async<void>::thenOrCatch(): decouple from promise #{} and set fail callback", getSNforHandle(m_coroutine));
#endif
                    m_coroutine.promise().then = &response_cb;
                    m_coroutine.promise().fail = &exception_cb;
                    m_coroutine.promise().drop = true;
                    m_coroutine = nullptr;
                } else {
#ifdef _COROUTINE_DEBUG
                    std::println("async<void>::thenOrCatch(): run ");
#endif
                    try {
                        m_coroutine.promise().result();
                        response_cb();
                    } catch (std::exception& ex) {
                        exception_cb(ex);
                    }
                }
#ifdef _COROUTINE_DEBUG
            } else {
                std::println("async<void>::thenOrCatch(): no coroutine");
#endif
            }
            return *this;
        }
};

namespace detail {

template <typename T>
async<T> async_promise<T>::get_return_object() noexcept {
    auto t = async<T>{std::coroutine_handle<async_promise>::from_promise(*this)};
#ifdef _COROUTINE_DEBUG
    std::println("before entering function: created async<T> #{} with promise #{}", t.sn, sn);
#endif
    return t;
}

inline async<void> async_promise<void>::get_return_object() noexcept {
    auto t = async<void>{std::coroutine_handle<async_promise>::from_promise(*this)};
#ifdef _COROUTINE_DEBUG
    std::println("before entering function: created async<void> #{} with promise #{}", t.sn, sn);
#endif
    return t;
}

template <typename T>
async<T&> async_promise<T&>::get_return_object() noexcept {
    auto t = async<T&>{std::coroutine_handle<async_promise>::from_promise(*this)};
#ifdef _COROUTINE_DEBUG
    std::println("before entering function: created async<T&> #{} with promise #{}", t.sn, sn);
#endif
    return t;
}

}  // namespace detail

template <typename K, typename V>
class interlock {
    private:
        class awaiter {
            public:
                awaiter(K id, interlock* _this) : id(id), _this(_this) {}
                bool await_ready() const noexcept { return false; }
                template <typename T>
                bool await_suspend(std::coroutine_handle<detail::async_promise<T>> awaitingCoroutine) noexcept {
#ifdef _COROUTINE_DEBUG
                    std::println("interlock::awaitable::await_suspend()");
#endif
                    _this->m_suspended[id] = *((std::coroutine_handle<detail::async_promise_base>*)&awaitingCoroutine);
                    return true;
                }
                V await_resume() {
#ifdef _COROUTINE_DEBUG
                    std::println("interlock::awaitable::await_resume() return result");
#endif
                    auto it = _this->m_result.find(id);
                    if (it == _this->m_result.end()) {
                        throw broken_resume("broken resume: did not find value");
                    }
                    return it->second;
                }

            private:
                K id;
                interlock* _this;
        };

        // TODO: use only one map
        std::map<K, std::coroutine_handle<detail::async_promise_base>> m_suspended;
        std::map<K, V> m_result;

    public:
        auto suspend(K id) { return awaiter{id, this}; }
        void resume(K id, V result) {
            auto it = m_suspended.find(id);
            if (it == m_suspended.end()) {
                throw broken_resume("broken resume: did not find async");
            }
            auto continuation = it->second;
            m_suspended.erase(it);
            if (!continuation.done()) {
                this->m_result[id] = result;
#ifdef _COROUTINE_DEBUG
                std::println("interlock::resume() -> resume promise #{}", getSNforHandle(continuation));
#endif
                continuation.resume();
            }
        }
};

}  // namespace cppasync
