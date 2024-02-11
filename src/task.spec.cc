#define _COROUTINE_DEBUG 1
#include "task.hh"

#include <kaffeeklatsch.hh>
using namespace kaffeeklatsch;

import std;

using namespace std;
using namespace cpptask;

namespace cpptask {

#ifdef _COROUTINE_DEBUG
    unsigned promise_sn_counter = 0;
    unsigned task_sn_counter = 0;
    unsigned awaitable_sn_counter = 0;
    unsigned promise_use_counter = 0;
    unsigned task_use_counter = 0;
    unsigned awaitable_use_counter = 0;
#endif

}

vector<string> logger;
template <class... Args>
void log(std::format_string<Args...> fmt, Args &&...args) {
    cout << format(fmt, args...) << endl;
    logger.push_back(format(fmt, args...));
}

interlock<unsigned, unsigned> my_interlock;

task<const char *> f3() {
    log("f3 enter");
    log("f3 co_await");
    auto v = co_await my_interlock.suspend(10);
    log("f3 co_await got {}", v);
    log("f3 leave");
    co_return "hello";
}

task<> f2() {
    log("f2 enter");
    auto text = co_await f3();
    log("expect 'hello', got '{}'", text);
    log("f2 leave");
    co_return;
}

task<double> f1() {
    log("f1 enter");
    co_await f2();
    log("f1 middle");
    co_await f2();
    log("f1 leave");
    co_return 3.1415;
}

task<int> f0() {
    log("f0 enter");
    double pi = co_await f1();
    log("expect 3.1415, got {}", pi);
    log("f0 leave");
    co_return 10;
}

task<> fx(bool wait) {
    log("fx enter");
    if (wait) {
        log("fx co_await");
        auto v = co_await my_interlock.suspend(10);
        log("fx co_await got {}", v);
    }
    log("fx leave");
    co_return;
}

task<> fy(bool wait) {
    log("fy enter");
    co_await fx(wait);
    log("fy leave");
    co_return;
}

kaffeeklatsch_spec([] {
    describe("coroutine", [] {
        beforeEach([] {
            logger.clear();
            resetCounters();
        });
        // TODO: kaffeeklatsch doesn't run beforeEach & afterEach as part of the test yet.
        // afterEach([] {
        //     expect(cpptask::task_use_counter).to.equal(0);
        //     expect(cpptask::promise_use_counter).to.equal(0);
        //     expect(cpptask::awaitable_use_counter).to.equal(0);
        // });
        it("handles single method, no suspend", [] {
            { fx(false).no_wait(); }
            expect(logger).to.equal(vector<string>{
                "fx enter",
                "fx leave",
            });
            expect(cpptask::task_use_counter).to.equal(0);
            expect(cpptask::promise_use_counter).to.equal(0);
            expect(cpptask::awaitable_use_counter).to.equal(0);
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
            expect(cpptask::task_use_counter).to.equal(0);
            expect(cpptask::promise_use_counter).to.equal(0);
            expect(cpptask::awaitable_use_counter).to.equal(0);
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
            expect(cpptask::task_use_counter).to.equal(0);
            expect(cpptask::promise_use_counter).to.equal(0);
            expect(cpptask::awaitable_use_counter).to.equal(0);
        });

        it("handles two nested methods, with suspend", [] {
            { fy(false).no_wait(); }
            expect(logger).to.equal(vector<string>{"fy enter", "fx enter", "fx leave", "fy leave"});
            expect(cpptask::task_use_counter).to.equal(0);
            expect(cpptask::promise_use_counter).to.equal(0);
            expect(cpptask::awaitable_use_counter).to.equal(0);
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
            expect(cpptask::task_use_counter).to.equal(0);
            expect(cpptask::promise_use_counter).to.equal(0);
            expect(cpptask::awaitable_use_counter).to.equal(0);
        });
    });
});

int main(int argc, char *argv[]) { return kaffeeklatsch::run(argc, argv); }
