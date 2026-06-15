/**
 * Copyright © 2026 Stefan Titze
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”),
 * to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
**/

/**
 * C++26 Coroutine Framework
 * =========================
 *
 * This file implements a minimal coroutine task type that supports both
 * value-returning (task<T>) and void (task<void>) coroutines. Yielding values is not supported.
 *
 * How C++ coroutines work (based on this implementation):
 *
 * 1. COROUTINE CALL (e.g., `task<int> result = f();`)
 *    - The compiler allocates a coroutine frame on the heap.
 *    - It finds `promise_type` via the return type: task<T>::promise_type = promise<T>.
 *    - A promise object is constructed inside the frame.
 *    - `get_return_object()` is called, which creates a task<T> wrapping a
 *      coroutine_handle that refers back to the promise/frame.
 *
 * 2. INITIAL SUSPENSION
 *    - `initial_suspend()` returns `suspend_always`, so the coroutine body does
 *      NOT execute yet. Control returns to the caller with the task object.
 *    - This makes coroutines "lazy": they only run when explicitly resumed.
 *
 * 3. RESUMPTION (e.g., `result()` or `co_await result`)
 *    - The caller invokes `task::operator()` (or `co_await` triggers `await_resume`).
 *    - This calls `coroutine_handle.resume()`, which continues the coroutine body.
 *
 * 4. CO_RETURN
 *    - `co_return <value>` calls `promise::return_value(val)` (or `return_void()`
 *      for task<void>), storing the result in the promise.
 *    - If an exception escapes, `unhandled_exception()` captures it via
 *      `std::current_exception()`.
 *
 * 5. FINAL SUSPENSION
 *    - `final_suspend()` returns `suspend_always`, keeping the coroutine frame
 *      alive so the caller can read the stored value from the promise.
 *    - The frame is NOT destroyed automatically here.
 *
 * 6. RESULT RETRIEVAL
 *    - `operator()` checks `coroutine_handle.done()`, calls `rethrow_if_failed()`
 *      to propagate any stored exception, then returns `promise.value`.
 *
 * 7. CLEANUP
 *    - The task destructor calls `coroutine_handle.destroy()`, which deallocates
 *      the coroutine frame (including the promise).
 *
 * CO_AWAIT SUPPORT
 *    - task<T> is itself an awaitable (implements await_ready, await_suspend,
 *      await_resume), so coroutines can `co_await` other tasks.
 *    - `await_ready()` returns true (the nested coroutine is eagerly resumed
 *      inside `await_resume` via `operator()`).
 *
 * Type hierarchy:
 *    promise_base<T>  -  shared suspend/exception logic
 *        promise<T>   -  stores T, implements return_value
 *        promise<void> - specialization with return_void
 *
 *    task_base<T>     -  owns coroutine_handle, awaitable interface
 *        task<T>      -  operator() resumes and returns T
 *        task<void>   -  operator() resumes, returns void
**/

#include <coroutine>
#include <exception>
#include <print>
#include <stdexcept>

template<typename T>
struct promise;

template<typename T>
class [[nodiscard]] task;

template<typename T>
using handle = std::coroutine_handle<promise<T>>;

template<typename T>
struct promise_base
{
    std::exception_ptr exception {};

    promise_base () noexcept = default;

    std::suspend_always initial_suspend () noexcept
    {
        return {};
    }

    std::suspend_always final_suspend () noexcept
    {
        /*
            When necessary you can return a more complex type. This is just an example from another project for demonstration purposes.
            Do not use it here
        
            struct [[nodiscard]] final_awaiter
            {
	            [[nodiscard]] bool await_ready () const noexcept
	            {
		            return false;
	            }

	            template<typename Promise>
	            [[nodiscard]] std::coroutine_handle<> await_suspend (std::coroutine_handle<Promise> handle) const noexcept
	            {
		            if (handle.promise ().continuation)
			            return handle.promise ().continuation;
		            return std::noop_coroutine ();
	            }

	            void await_resume () const noexcept
	            {}
            };

            return final_awaiter {};
        */

        return {};
    }

    void unhandled_exception () noexcept
    {
        this->exception = std::current_exception (); // always use this-> inside of template classes especially when templates inherit from templates
    }

    void rethrow_if_failed ()
    {
        if (this->exception)
            throw this->exception;
    }
};

// generic promise
template<typename T>
struct promise : public promise_base<T>
{
    T value {};

    promise () noexcept = default;

    task<T> get_return_object () noexcept
    {
        return task<T> {handle<T>::from_promise (*this)};
    }

    void return_value (T val) noexcept
    {
        this->value = val;
    }
};

template<typename T>
class [[nodiscard]] task_base
{
public:
    explicit task_base (handle<T> handle)
        : coroutine_handle {handle}
    {}

    virtual ~task_base ()
    {
        if (coroutine_handle)
            coroutine_handle.destroy();
    }

    bool await_ready () const noexcept
    {
        return true;
    }

    template<typename U>
    void await_suspend (handle<U>) const
    {}

protected:
    handle<T> coroutine_handle;
};

// generic task
template<typename T>
class [[nodiscard]] task : public task_base<T>
{
public:
    using promise_type = promise<T>;

    explicit task (handle<T> handle)
        : task_base<T> {handle}
    {}

    virtual ~task () = default;

    T operator() ()
    {
        if (this->coroutine_handle)
        {
            if (!this->coroutine_handle.done ())
                this->coroutine_handle.resume ();

            if (this->coroutine_handle.done ())
            {
                this->coroutine_handle.promise ().rethrow_if_failed ();
                return this->coroutine_handle.promise ().value;
            }
        }

        throw std::logic_error {"The coroutine handle is not set or the coroutine was not running."};
    }

    T await_resume ()
    {
        return this->operator() ();
    }
};

// promise specialization for void (no return value)
template<>
struct promise<void> : public promise_base<void>
{
    promise () noexcept = default;

    task<void> get_return_object () noexcept;

    void return_void () noexcept
    {}
};

// task specialization for void (no return value)
template<>
class [[nodiscard]] task<void> : public task_base<void>
{
public:
    using promise_type = promise<void>;

    explicit task (handle<void> handle)
        : task_base {handle}
    {}

    virtual ~task () = default;

    void operator() ()
    {
        if (coroutine_handle)
        {
            if (!coroutine_handle.done ())
                coroutine_handle.resume ();

            if (coroutine_handle.done ())
            {
                coroutine_handle.promise ().rethrow_if_failed ();
                return;
            }
        }

        throw std::logic_error {"The coroutine handle is not set or the coroutine was not running."};
    }

    void await_resume ()
    {
        return this->operator() ();
    }
};

task<void> promise<void>::get_return_object () noexcept
{
    return task<void> {handle<void>::from_promise (*this)};
}

// example functions
task<int> f ()
{
    co_return 42;
}

task<double> g ()
{
    co_return 1.;
}

task<void> h ()
{
    co_return;
}

task<int> foo ()
{
    task<int> myFuture = f();
    int value {co_await myFuture};
    task<double> novalue {g ()};
    novalue (); // it is ok to not use co_await here b/c I did not implement separate awaitable classes (operator () resumes the coroutine directly)
    co_await h ();
    co_return value;
}

// main cannot be a coroutine function (cannot use co_* keywords)
int main()
{
    std::println ("The value is {}", foo () ());  // awaiting foo without using co_await
    return 0;
}