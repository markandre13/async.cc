#define _COROUTINE_DEBUG 1

#include "async.hh"

#include <kaffeeklatsch.hh>
using namespace kaffeeklatsch;

using namespace std;
using namespace cppasync;

namespace cppasync {

#ifdef _COROUTINE_DEBUG
unsigned promise_sn_counter = 0;
unsigned async_sn_counter = 0;
unsigned awaitable_sn_counter = 0;
unsigned promise_use_counter = 0;
unsigned async_use_counter = 0;
unsigned awaitable_use_counter = 0;
#endif

}  // namespace cppasync

vector<string> logger;
template <class... Args>
void log(std::format_string<Args...> fmt, Args &&...args) {
    cout << format(fmt, args...) << endl;
    logger.push_back(format(fmt, args...));
}

interlock<unsigned, unsigned> my_interlock;

async<const char *> f3() {
    log("f3 enter");
    log("f3 co_await");
    auto v = co_await my_interlock.suspend(10);
    log("f3 co_await got {}", v);
    log("f3 leave");
    co_return "hello";
}

async<> f2() {
    log("f2 enter");
    auto text = co_await f3();
    log("expect 'hello', got '{}'", text);
    log("f2 leave");
    co_return;
}

async<double> f1() {
    log("f1 enter");
    co_await f2();
    log("f1 middle");
    co_await f2();
    log("f1 leave");
    co_return 3.1415;
}

async<int> f0() {
    log("f0 enter");
    double pi = co_await f1();
    log("expect 3.1415, got {}", pi);
    log("f0 leave");
    co_return 10;
}

async<> waitOnMyInterlock10IfTrue(bool wait) {
    log("waitOnMyInterlock10IfTrue enter");
    if (wait) {
        log("waitOnMyInterlock10IfTrue co_await");
        auto v = co_await my_interlock.suspend(10);
        log("waitOnMyInterlock10IfTrue co_await got {}", v);
    }
    log("waitOnMyInterlock10IfTrue leave");
    co_return;
}

async<> nestedWaitOnMyInterlock10IfTrue(bool wait) {
    log("nestedWaitOnMyInterlock10IfTrue enter");
    co_await waitOnMyInterlock10IfTrue(wait);
    log("nestedWaitOnMyInterlock10IfTrue leave");
    co_return;
}

async<> wait() { co_await std::suspend_always(); }
async<> no_wait() { co_return; }

async<> no_wait_void() { co_return; }
async<unsigned> no_wait_unsigned(unsigned value) { co_return value; }

async<> no_wait_void_throw() {
    throw runtime_error("yikes");
    co_return;
}
async<unsigned> no_wait_unsigned_throw(unsigned value) {
    throw runtime_error(format("yikes {}", value));
    co_return value;
}

async<> wait_void(unsigned id) {
    co_await my_interlock.suspend(id);
    co_return;
}
async<unsigned> wait_unsigned(unsigned id) {
    auto v = co_await my_interlock.suspend(id);
    co_return v;
}

async<> wait_void_throw(unsigned id) {
    println("wait_void_throw(): suspend");
    auto v = co_await my_interlock.suspend(id);
    println("wait_void_throw(): resume and throw");
    throw std::runtime_error(std::format("yikes {}", v));
    co_return;
}
async<unsigned> wait_unsigned_throw(unsigned id) {
    auto v = co_await my_interlock.suspend(id);
    throw std::runtime_error(std::format("yikes {}", v));
    co_return v;
}

unsigned global_value;
unsigned &global_value_ref = global_value;
async<unsigned &> wait_unsigned_ref(unsigned id) {
    global_value = co_await my_interlock.suspend(id);
    co_return global_value_ref;
}

kaffeeklatsch_spec([] {
    beforeEach([] {
        logger.clear();
        resetCounters();
    });
    afterEach([] {
        expect(cppasync::async_use_counter).to.equal(0);
        expect(cppasync::promise_use_counter).to.equal(0);
        expect(cppasync::awaitable_use_counter).to.equal(0);
    });
    describe("coroutine", [] {

        it("handles single method, no suspend", [] {
            // WHEN a calling a coroutine which does not suspend
            { waitOnMyInterlock10IfTrue(false).no_wait(); }
            // THEN it is successfully executed
            expect(logger).to.equal(vector<string>{
                "waitOnMyInterlock10IfTrue enter",
                "waitOnMyInterlock10IfTrue leave",
            });
        });

        it("handles single method, one suspend returning a result value", [] {
            // GIVEN a call a coroutine which suspeds on an interlock
            { waitOnMyInterlock10IfTrue(true).no_wait(); }
            log("resume");

            // WHEN we resume the coroutine and provide a return value of 2001
            my_interlock.resume(10, 2001);

            // THEN the coroutine got the value of 2001 and resumed it's execution
            expect(logger).to.equal(vector<string>{
                "waitOnMyInterlock10IfTrue enter",
                "waitOnMyInterlock10IfTrue co_await",
                "resume",
                "waitOnMyInterlock10IfTrue co_await got 2001",
                "waitOnMyInterlock10IfTrue leave",
            });
        });
        it("handles one single method, one suspend returning an exception", [] {

        });

        it("handles two nested methods, no suspend", [] {
            { nestedWaitOnMyInterlock10IfTrue(true).no_wait(); }
            log("resume");
            my_interlock.resume(10, 2001);
            // println("==========================================");
            // for (auto &l : logger) {
            //     println("{}", l);
            // }
            // println("==========================================");
            expect(logger).to.equal(vector<string>{
                "nestedWaitOnMyInterlock10IfTrue enter",
                "waitOnMyInterlock10IfTrue enter",
                "waitOnMyInterlock10IfTrue co_await",
                "resume",
                "waitOnMyInterlock10IfTrue co_await got 2001",
                "waitOnMyInterlock10IfTrue leave",
                "nestedWaitOnMyInterlock10IfTrue leave"}
            );
        });

        it("handles two nested methods, with suspend", [] {
            { nestedWaitOnMyInterlock10IfTrue(false).no_wait(); }
            expect(logger).to.equal(vector<string>{"nestedWaitOnMyInterlock10IfTrue enter", "waitOnMyInterlock10IfTrue enter", "waitOnMyInterlock10IfTrue leave", "nestedWaitOnMyInterlock10IfTrue leave"});
        });

        it("handles many nested methods and two suspends", [] {
            { f0().no_wait(); }
            log("resume");
            my_interlock.resume(10, 2001);
            log("resume");
            my_interlock.resume(10, 2010);

            // println("==========================================");
            // for (auto &l : logger) {
            //     println("{}", l);
            // }
            // println("==========================================");

            auto expect = vector<string>{{"f0 enter",
                                          "f1 enter",
                                          "f2 enter",
                                          "f3 enter",
                                          "f3 co_await",
                                          "resume",
                                          "f3 co_await got 2001",
                                          "f3 leave",
                                          "expect 'hello', got 'hello'",
                                          "f2 leave",
                                          "f1 middle",
                                          "f2 enter",
                                          "f3 enter",
                                          "f3 co_await",
                                          "resume",
                                          "f3 co_await got 2010",
                                          "f3 leave",
                                          "expect 'hello', got 'hello'",
                                          "f2 leave",
                                          "f1 leave",
                                          "expect 3.1415, got 3.1415",
                                          "f0 leave"}};
            expect(logger).equals(expect);
        });
    });

    describe("calling from sync", [] {
        it("destroying a finished async will not throw", [] {
            { auto async = no_wait(); }
        });
        it("destroying an unfinished async will throw an unfinished_promise exception", [] {
            expect([] {
                { auto async = wait(); }
            }).to.throw_(unfinished_promise());
        });
        it("destroying a unfinished async will not throw when uncoupled", [] {
            {
                auto async = wait();
                async.no_wait();
            }
            expect(cppasync::promise_use_counter).to.equal(1);
            cppasync::promise_use_counter = 0;
        });
    });
    describe("then(...)", [] {
        describe("will be executed after the co_await", [] {
            it("T", [] {
                bool thenExecuted = false;
                unsigned out = 0;
                {
                    wait_unsigned(10).then([&](unsigned response) {
                        thenExecuted = true;
                        out = response;
                    });
                }
                expect(thenExecuted).to.beFalse();
                my_interlock.resume(10, 20);
                expect(thenExecuted).to.beTrue();
                expect(out).to.equal(20);
            });
            it("void", [] {
                bool thenExecuted = false;
                {
                    wait_void(10).then([&]() {
                        thenExecuted = true;
                    });
                }
                expect(thenExecuted).to.beFalse();
                my_interlock.resume(10, 20);
                expect(thenExecuted).to.beTrue();
            });
            xit("T&", [] {});
        });
        describe("will be executed when there was no co_await", [] {
            it("T", [] {
                bool thenExecuted = false;
                unsigned out = 0;
                {
                    no_wait_unsigned(10).then([&](unsigned response) {
                        thenExecuted = true;
                        out = response;
                    });
                }
                expect(thenExecuted).to.beTrue();
                expect(out).to.equal(10);
            });
            it("void", [] {
                bool thenExecuted = false;
                {
                    no_wait().then([&]() {
                        thenExecuted = true;
                    });
                }
                expect(thenExecuted).to.beTrue();
            });
            xit("T&", [] {});
        });
    });
    describe("thenOrCatch(..., ...)", [] {
        describe("will be executed after the co_await", [] {
            it("T", [] {
                bool thenExecuted = false;
                bool failExecuted = false;
                string out;
                {
                    wait_unsigned_throw(10).thenOrCatch(
                        [&](unsigned) {
                            thenExecuted = true;
                        },
                        [&](std::exception_ptr eptr) {
                            try {
                                std::rethrow_exception(eptr);
                            }
                            catch(runtime_error &error) {
                                failExecuted = true;
                                out = error.what();
                            }
                        });
                }
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beFalse();

                my_interlock.resume(10, 20);
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beTrue();
                expect(out).to.equal("yikes 20");
            });
            it("void", [] {
                bool thenExecuted = false;
                bool failExecuted = false;
                string out;
                {
                    wait_void_throw(10).thenOrCatch(
                        [&]() {
                            thenExecuted = true;
                        },
                        [&](std::exception_ptr eptr) {
                            try {
                                std::rethrow_exception(eptr);
                            }
                            catch(runtime_error &error) {
                                failExecuted = true;
                                out = error.what();
                            }
                        });
                }
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beFalse();
                my_interlock.resume(10, 20);
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beTrue();
                expect(out).to.equal("yikes 20");
            });
            xit("T&", [] {});
        });
        describe("will be executed when there was no co_await", [] {
            it("T", [] {
                bool thenExecuted = false;
                bool failExecuted = false;
                string out;
                {
                    no_wait_unsigned_throw(10).thenOrCatch(
                        [&](unsigned) {
                            thenExecuted = true;
                        },
                        [&](std::exception_ptr eptr) {
                            try {
                                std::rethrow_exception(eptr);
                            }
                            catch(runtime_error &error) {
                                failExecuted = true;
                                out = error.what();
                            }
                        });
                }
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beTrue();
                expect(out).to.equal("yikes 10");
            });
            it("void", [] {
                bool thenExecuted = false;
                bool failExecuted = false;
                string out;
                {
                    no_wait_void_throw().thenOrCatch(
                        [&]() {
                            thenExecuted = true;
                        },
                        [&](std::exception_ptr eptr) {
                            try {
                                std::rethrow_exception(eptr);
                            }
                            catch(runtime_error &error) {
                                failExecuted = true;
                                out = error.what();
                            }
                        });
                }
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beTrue();
                expect(out).to.equal("yikes");
            });
            xit("T&", [] {});
        });
    });
});

int main(int argc, char *argv[]) { return kaffeeklatsch::run(argc, argv); }
