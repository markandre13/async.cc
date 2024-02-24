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

async<void> f2() {
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

async<void> fx(bool wait) {
    log("fx enter");
    if (wait) {
        log("fx co_await");
        auto v = co_await my_interlock.suspend(10);
        log("fx co_await got {}", v);
    }
    log("fx leave");
    co_return;
}

async<void> wait() { co_await std::suspend_always(); }
async<void> no_wait() { co_return; }

async<void> no_wait_void() { co_return; }
async<unsigned> no_wait_unsigned(unsigned value) { co_return value; }

async<void> no_wait_void_throw() {
    throw runtime_error("yikes");
    co_return;
}
async<unsigned> no_wait_unsigned_throw(unsigned value) {
    throw runtime_error(format("yikes {}", value));
    co_return value;
}

async<void> wait_void(unsigned id) {
    co_await my_interlock.suspend(id);
    co_return;
}
async<unsigned> wait_unsigned(unsigned id) {
    auto v = co_await my_interlock.suspend(id);
    co_return v;
}

async<void> wait_void_throw(unsigned id) {
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

async<void> fy(bool wait) {
    log("fy enter");
    co_await fx(wait);
    log("fy leave");
    co_return;
}

kaffeeklatsch_spec([] {
    beforeEach([] {
        logger.clear();
        resetCounters();
    });
    describe("coroutine", [] {
        beforeEach([] {  // TODO: there should only be one
            logger.clear();
            resetCounters();
        });
        // TODO: kaffeeklatsch doesn't run beforeEach & afterEach as part of the
        // test yet. afterEach([] {
        //     expect(cppasync::async_use_counter).to.equal(0);
        //     expect(cppasync::promise_use_counter).to.equal(0);
        //     expect(cppasync::awaitable_use_counter).to.equal(0);
        // });
        it("handles single method, no suspend", [] {
            { fx(false).no_wait(); }
            expect(logger).to.equal(vector<string>{
                "fx enter",
                "fx leave",
            });
            expect(cppasync::async_use_counter).to.equal(0);
            expect(cppasync::promise_use_counter).to.equal(0);
            expect(cppasync::awaitable_use_counter).to.equal(0);
        });

        it("handles single method, one suspend", [] {
            { fx(true).no_wait(); }
            log("resume");
            my_interlock.resume(10, 2001);
            expect(logger).to.equal(vector<string>{
                "fx enter",
                "fx co_await",
                "resume",
                "fx co_await got 2001",
                "fx leave",
            });
            expect(cppasync::async_use_counter).to.equal(0);
            expect(cppasync::promise_use_counter).to.equal(0);
            expect(cppasync::awaitable_use_counter).to.equal(0);
        });

        it("handles two nested methods, no suspend", [] {
            { fy(true).no_wait(); }
            log("resume");
            my_interlock.resume(10, 2001);
            // println("==========================================");
            // for (auto &l : logger) {
            //     println("{}", l);
            // }
            // println("==========================================");
            expect(logger).to.equal(vector<string>{"fy enter", "fx enter", "fx co_await", "resume", "fx co_await got 2001", "fx leave", "fy leave"});
            expect(cppasync::async_use_counter).to.equal(0);
            expect(cppasync::promise_use_counter).to.equal(0);
            expect(cppasync::awaitable_use_counter).to.equal(0);
        });

        it("handles two nested methods, with suspend", [] {
            { fy(false).no_wait(); }
            expect(logger).to.equal(vector<string>{"fy enter", "fx enter", "fx leave", "fy leave"});
            expect(cppasync::async_use_counter).to.equal(0);
            expect(cppasync::promise_use_counter).to.equal(0);
            expect(cppasync::awaitable_use_counter).to.equal(0);
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
            expect(cppasync::async_use_counter).to.equal(0);
            expect(cppasync::promise_use_counter).to.equal(0);
            expect(cppasync::awaitable_use_counter).to.equal(0);
        });
    });
    describe("calling from sync", [] {
        it("destroying a finished async will not throw", [] {
            { auto async = no_wait(); }
            expect(cppasync::async_use_counter).to.equal(0);
            expect(cppasync::promise_use_counter).to.equal(0);
            expect(cppasync::awaitable_use_counter).to.equal(0);
        });
        it("destroying an unfinished async will throw an unfinished_promise exception", [] {
            expect([] {
                { auto async = wait(); }
            }).to.throw_(unfinished_promise());
            expect(cppasync::async_use_counter).to.equal(0);
            expect(cppasync::promise_use_counter).to.equal(0);
            expect(cppasync::awaitable_use_counter).to.equal(0);
        });
        it("destroying a unfinished async will not throw when uncoupled", [] {
            {
                auto async = wait();
                async.no_wait();
            }
            expect(cppasync::async_use_counter).to.equal(0);
            expect(cppasync::promise_use_counter).to.equal(1);
            expect(cppasync::awaitable_use_counter).to.equal(0);
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
                expect(cppasync::async_use_counter).to.equal(0);
                expect(cppasync::promise_use_counter).to.equal(0);
                expect(cppasync::awaitable_use_counter).to.equal(0);
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
                expect(cppasync::async_use_counter).to.equal(0);
                expect(cppasync::promise_use_counter).to.equal(0);
                expect(cppasync::awaitable_use_counter).to.equal(0);
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
                expect(cppasync::async_use_counter).to.equal(0);
                expect(cppasync::promise_use_counter).to.equal(0);
                expect(cppasync::awaitable_use_counter).to.equal(0);
            });
            it("void", [] {
                bool thenExecuted = false;
                {
                    no_wait().then([&]() {
                        thenExecuted = true;
                    });
                }
                expect(thenExecuted).to.beTrue();
                expect(cppasync::async_use_counter).to.equal(0);
                expect(cppasync::promise_use_counter).to.equal(0);
                expect(cppasync::awaitable_use_counter).to.equal(0);
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
                        [&](std::exception &error) {
                            failExecuted = true;
                            out = error.what();
                        });
                }
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beFalse();

                my_interlock.resume(10, 20);
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beTrue();
                expect(out).to.equal("yikes 20");
                expect(cppasync::async_use_counter).to.equal(0);
                expect(cppasync::promise_use_counter).to.equal(0);
                expect(cppasync::awaitable_use_counter).to.equal(0);
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
                        [&](std::exception &error) {
                            failExecuted = true;
                            out = error.what();
                        });
                }
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beFalse();
                my_interlock.resume(10, 20);
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beTrue();
                expect(out).to.equal("yikes 20");
                expect(cppasync::async_use_counter).to.equal(0);
                expect(cppasync::promise_use_counter).to.equal(0);
                expect(cppasync::awaitable_use_counter).to.equal(0);
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
                        [&](std::exception &error) {
                            failExecuted = true;
                            out = error.what();
                        });
                }
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beTrue();
                expect(out).to.equal("yikes 10");
                expect(cppasync::async_use_counter).to.equal(0);
                expect(cppasync::promise_use_counter).to.equal(0);
                expect(cppasync::awaitable_use_counter).to.equal(0);
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
                        [&](std::exception &error) {
                            failExecuted = true;
                            out = error.what();
                        });
                }
                expect(thenExecuted).to.beFalse();
                expect(failExecuted).to.beTrue();
                expect(out).to.equal("yikes");
                expect(cppasync::async_use_counter).to.equal(0);
                expect(cppasync::promise_use_counter).to.equal(0);
                expect(cppasync::awaitable_use_counter).to.equal(0);
            });
            xit("T&", [] {});
        });
    });
});

int main(int argc, char *argv[]) { return kaffeeklatsch::run(argc, argv); }
