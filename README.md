# cpptask

a lightweight c++20 coroutine task class

>  "you can have peace. or you can have freedom. don't ever count on having both at once."
>  -- robert a. heinlein

while c++20/23 coroutines provide a lot of freedom to accommodate many different use cases and hardware platforms, actually _using_ them does not provide much peace of mind.

a lot of examples on the web do use a class named 'task' along with coroutines, but it's actually not part of the c++ standard. my guess that it's a reference to the cppcoro library.

this file contains a variation of cppcoro's 'task' class and tweaks it to be usable without the rest of the cppcoro library.

 ### class task

_task_ is the class to be returned from coroutines.

it is basically a copy of https://github.com/andreasbuhr/cppcoro's task with two changes:

* while one can call a coroutine from other coroutines like this
  ```c++
  task<> fun0() {
    co_await fun1();
  }
  ```
  one can also call a coroutine from synchronous code
  ```c++
  int fun0() {
    fun1().no_wait();
  }
  ```
  with no_wait() preventing the coroutine's state from being deleted along with the task in case it is still running and instead deleted it once it's finished.
 
 * the coroutine is immediately executed.

 ### class interlock

 _interlock<KEY, VALUE>_ is the class to be used to suspend and resume coroutines:

```c++
auto value = co_await interlock.suspend(key);
```

will suspend the execution of the coroutine until

```c++
interlock.resume(key, value);
```

resumes it along with providing a value.

### TODO

- [ ] for the full 'javascript' experience, add then() and catch() methods to 'task'
- [ ] try to move the 'return !m_coroutine.done();' from awaitable_base from
     await_suspend() into await_ready() to improve performance.
- [ ] test exception handling
- [ ] task<T&> not yet included in tests
