#ifndef STACKTRACE_H
#define STACKTRACE_H

#include <cstdlib>
#include <exception>
#include <iostream>

#ifdef _WIN32
#include <signal.h>

const struct catchSignals_t
{
    int signo;
    const char *name;
} catchSignals[] = {{SIGSEGV, "SIGSEGV"}, {SIGILL, "SIGILL"}, {SIGFPE, "SIGFPE"},
                    {SIGABRT, "SIGABRT"}};

void doBacktrace(int signo)
{
    std::cerr << "Received signal " << signo << std::endl;
    std::cerr << "(Backtrace not available on Windows)" << std::endl;
    exit(1);
}

bool registerBacktraceHandlers()
{
    bool result = true;
    for (auto i : catchSignals)
    {
        result = result && (signal(i.signo, doBacktrace) != SIG_ERR);
        if (!result)
            std::cerr << "Failed to install signal:" << i.name;
    }
    return result;
}

#else  // Linux / Kobo

#include <execinfo.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

void doBacktrace(int signo);

const struct catchSignals_t
{
    int signo;
    const char *name;
} catchSignals[] = {{SIGSEGV, "SIGSEGV"}, {SIGILL, "SIGILL"}, {SIGFPE, "SIGFPE"},
                    {SIGABRT, "SIGABRT"}, {SIGBUS, "SIGBUS"}, {SIGUSR2, "SIGUSR2"}};

bool registerBacktraceHandlers()
{
    bool result = true;

    for (auto i : catchSignals)
    {
        result = result && (signal(i.signo, doBacktrace) != SIG_ERR);

        if (!result)
            std::cerr << "Failed to install signal:" << i.name;
    }

    return result;
};

void doBacktrace(int signo)
{
    std::cerr << "Received signal " << signo << std::endl;
    void *array[50];
    int size = backtrace(array, 50);

    std::cerr << " backtrace returned " << size << " frames\n\n";

    char **messages = backtrace_symbols(array, size);

    for (int i = 0; i < size && messages != NULL; ++i)
    {
        std::cerr << "[bt]: (" << i << ") " << messages[i] << std::endl;
    }
    std::cerr << std::endl;

    free(messages);

    // Restore framebuffer depth on Kobo before exiting
    // The app changes fb depth; if we crash without restoring, Nickel gets wrong resolution
    system("/mnt/onboard/.adds/UltimateMangaReader/fbdepth -d 32 2>/dev/null");

    exit(0);
}

#endif  // _WIN32

#endif  // STACKTRACE_H
