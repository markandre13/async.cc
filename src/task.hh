#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <map>
#include <print>
#include <stdexcept>
#include <functional>

namespace cpptask {

template <typename T>
class task;

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
extern unsigned task_sn_counter;
extern unsigned awaitable_sn_counter;
extern unsigned promise_use_counter;
extern unsigned task_use_counter;
extern unsigned awaitable_use_counter;
extern std::coroutine_handle<> global_continuation;
unsigned getSNforHandle(std::coroutine_handle<> handle);
inline void resetCounters() { promise_sn_counter = task_sn_counter = awaitable_sn_counter = promise_use_counter = task_use_counter = awaitable_use_counter = 0; }
#endif

namespace detail {

class task_promise_base {
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
                        // is set later... maybe either task must deleted it when there never was a suspend or when the
                        // associated task had an no_wait() call, then the promise was marked for deletion and it's being
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
        const std::function<void()> *then = nullptr;
#ifdef _COROUTINE_DEBUG
        unsigned sn;
        task_promise_base() noexcept {
            ++promise_use_counter;
            sn = ++promise_sn_counter;
            // std::println("  create task_promise_base #{}", sn);
        }
        ~task_promise_base() {
            if (then) {
                (*then)();
            }
            --promise_use_counter;
            std::println("promise #{}: destroyed", sn);
        }
#else
        task_promise_base() noexcept {}
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
inline unsigned getSNforHandle(std::coroutine_handle<> handle) { return ((std::coroutine_handle<detail::task_promise_base>*)&handle)->promise().sn; }
#endif

namespace detail {

// the VALUE specialisation of task_promise_base
template <typename T>
class task_promise final : public task_promise_base {
    public:
        task_promise() noexcept {}
        ~task_promise() {
            switch (m_resultType) {
                case result_type::value:
                    m_value.~T();
                    break;
                case result_type::exception:
                    m_exception.~exception_ptr();
                    break;
                default:
                    break;
            }
        }
        task<T> get_return_object() noexcept;
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

    private:
        enum class result_type { empty, value, exception };
        result_type m_resultType = result_type::empty;
        union {
                T m_value;
                std::exception_ptr m_exception;
        };
};

// the VOID specialisation of task_promise_base
template <>
class task_promise<void> : public task_promise_base {
    public:
        task_promise() noexcept {
#ifdef _COROUTINE_DEBUG
        // std::println("  create task_promise<> #{}", sn);
#endif
        } task<void> get_return_object() noexcept;
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

    private:
        std::exception_ptr m_exception;
};

// the REFERENCE specialisation of task_promise_base
template <typename T>
class task_promise<T&> : public task_promise_base {
    public:
        task_promise() noexcept = default;
        task<T&> get_return_object() noexcept;
        void unhandled_exception() noexcept { m_exception = std::current_exception(); }
        void return_value(T& value) noexcept { m_value = std::addressof(value); }
        T& result() {
            if (m_exception) {
                std::rethrow_exception(m_exception);
            }
            return *m_value;
        }

    private:
        T* m_value = nullptr;
        std::exception_ptr m_exception;
};
}  // namespace detail

template <typename T = void>
class [[nodiscard]] task {
    public:
        using promise_type = detail::task_promise<T>;
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
        task() noexcept : m_coroutine(nullptr) {
#ifdef _COROUTINE_DEBUG
            ++task_use_counter;
            sn = ++task_sn_counter;
            // std::println("  create task<>() #{}", sn);
#endif
        }
        explicit task(handle_type coroutine) : m_coroutine(coroutine) {
#ifdef _COROUTINE_DEBUG
            ++task_use_counter;
            sn = ++task_sn_counter;
            // std::println("  create task<>(coroutine) #{}", sn);
#endif
        }
        task(task&& t) noexcept : m_coroutine(t.m_coroutine) {
#ifdef _COROUTINE_DEBUG
            ++task_use_counter;
            sn = ++task_sn_counter;
            // std::println("  create task<>()&& #{} from #{}", sn, t.sn);
#endif
            t.m_coroutine = nullptr;
        }

    private:
        task(const task&) = delete;
        task& operator=(const task&) = delete;

    public:
        ~task() noexcept(false) {
#ifdef _COROUTINE_DEBUG
            --task_use_counter;
            if (m_coroutine) {
                std::println("task #{} destroyed, also destroy promise #{}", sn, getSNforHandle(m_coroutine));
                if (!m_coroutine.done()) {
                    std::println("task #{} destroyed, also destroy promise #{} BUT IT'S NOT DONE", sn, getSNforHandle(m_coroutine));
                }
            } else {
                std::println("task #{} destroyed, no promise to destroy", sn);
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

        task& operator=(task&& other) noexcept {
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
            std::println("task #{} co_await&: create awaitable #{} for promise #{}", sn, asn, getSNforHandle(m_coroutine));
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
            std::println("task #{} co_await&&: create awaitable #{} for promise #{}", sn, asn, getSNforHandle(m_coroutine));
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
        task<T>& then(const std::function<void()> &callback) {
            if (!m_coroutine.done()) {
                m_coroutine.promise().drop = true;
                m_coroutine.promise().then = &callback;
                m_coroutine = nullptr;
            } else {
                callback();
            }
            return *this;
        }
};

namespace detail {

template <typename T>
task<T> task_promise<T>::get_return_object() noexcept {
    auto t = task<T>{std::coroutine_handle<task_promise>::from_promise(*this)};
#ifdef _COROUTINE_DEBUG
    std::println("before entering function: created task<T> #{} with promise #{}", t.sn, sn);
#endif
    return t;
}

inline task<void> task_promise<void>::get_return_object() noexcept {
    auto t = task<void>{std::coroutine_handle<task_promise>::from_promise(*this)};
#ifdef _COROUTINE_DEBUG
    std::println("before entering function: created task<void> #{} with promise #{}", t.sn, sn);
#endif
    return t;
}

template <typename T>
task<T&> task_promise<T&>::get_return_object() noexcept {
    auto t = task<T&>{std::coroutine_handle<task_promise>::from_promise(*this)};
#ifdef _COROUTINE_DEBUG
    std::println("before entering function: created task<T&> #{} with promise #{}", t.sn, sn);
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
                bool await_suspend(std::coroutine_handle<detail::task_promise<T>> awaitingCoroutine) noexcept {
#ifdef _COROUTINE_DEBUG
                    std::println("interlock::awaitable::await_suspend()");
#endif
                    _this->suspended[id] = *((std::coroutine_handle<detail::task_promise_base>*)&awaitingCoroutine);
                    return true;
                }
                V await_resume() {
#ifdef _COROUTINE_DEBUG
                    std::println("interlock::awaitable::await_resume() return result");
#endif
                    auto it = _this->result.find(id);
                    if (it == _this->result.end()) {
                        throw broken_resume("broken resume: did not find value");
                    }
                    return it->second;
                }

            private:
                K id;
                interlock* _this;
        };

        // TODO: use only one map
        std::map<K, std::coroutine_handle<detail::task_promise_base>> suspended;
        std::map<K, V> result;

    public:
        auto suspend(K id) { return awaiter{id, this}; }
        void resume(K id, V result) {
            auto it = suspended.find(id);
            if (it == suspended.end()) {
                throw broken_resume("broken resume: did not find task");
            }
            auto continuation = it->second;
            suspended.erase(it);
            if (!continuation.done()) {
                this->result[id] = result;
                std::println("interlock::resume() -> resume promise #{}", getSNforHandle(continuation));
                continuation.resume();
            }
        }
};

}  // namespace cpptask
