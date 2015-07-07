#include <stdio.h>
#include <chrono>
#include <thread>

using std::chrono::microseconds;

#if defined(__linux__)
#include <sys/prctl.h>
#endif

volatile int g_thread_2_continuing = 0;

void *
thread_1_func (void *input)
{
    // Waiting to be released by the debugger.
    while (!g_thread_2_continuing) // Another thread will change this value
    {
        std::this_thread::sleep_for(microseconds(1));
    }

    // Return
    return NULL;  // Set third breakpoint here
}

void *
thread_2_func (void *input)
{
    // Waiting to be released by the debugger.
    int child_thread_continue = 0;
    while (!child_thread_continue) // The debugger will change this value
    {
        std::this_thread::sleep_for(microseconds(1));  // Set second breakpoint here
    }

    // Release thread 1
    g_thread_2_continuing = 1;

    // Return
    return NULL;
}

int main(int argc, char const *argv[])
{
#if defined(__linux__)
    // Immediately enable any ptracer so that we can allow the stub attach
    // operation to succeed.  Some Linux kernels are locked down so that
    // only an ancestor process can be a ptracer of a process.  This disables that
    // restriction.  Without it, attach-related stub tests will fail.
#if defined(PR_SET_PTRACER) && defined(PR_SET_PTRACER_ANY)
    int prctl_result;

    // For now we execute on best effort basis.  If this fails for
    // some reason, so be it.
    prctl_result = prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    (void) prctl_result;
#endif
#endif

    // Create a new thread
    std::thread thread_1(thread_1_func, nullptr);

    // Waiting to be attached by the debugger.
    int main_thread_continue = 0;
    while (!main_thread_continue) // The debugger will change this value
    {
        std::this_thread::sleep_for(microseconds(1));  // Set first breakpoint here
    }

    // Create another new thread
    std::thread thread_2(thread_2_func, nullptr);

    // Wait for the threads to finish.
    thread_1.join();
    thread_2.join();

    printf("Exiting now\n");
}