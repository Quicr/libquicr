// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <condition_variable>
#include <csignal>
#include <iostream>
#include <mutex>

namespace moq_example {
    std::mutex main_mutex;                     // Main's mutex
    bool terminate{ false };                   // Termination flag
    std::condition_variable cv;                // Main thread waits on this
    const char* termination_reason{ nullptr }; // Termination reason
};

/*
 *  signalHandler
 *
 *  Description:
 *      This function will handle operating system signals related to
 *      termination and then instruct the main thread to terminate.
 *
 *  Parameters:
 *      signal_number [in]
 *          The signal caught.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
signalHandler(int signal_number)
{
    const auto lock = std::lock_guard<std::mutex>(moq_example::main_mutex);

    // If termination is in process, just return
    if (moq_example::terminate) {
        return;
    }

    // Indicate that the process should terminate
    moq_example::terminate = true;

    // Set the termination reason string
    switch (signal_number) {
        case SIGINT:
            moq_example::termination_reason = "Interrupt signal received";
            break;

#ifndef _WIN32
        case SIGHUP:
            moq_example::termination_reason = "Hangup signal received";
            break;

        case SIGQUIT:
            moq_example::termination_reason = "Quit signal received";
            break;
#endif

        default:
            moq_example::termination_reason = "Unknown signal received";
            break;
    }

    // Notify the main execution thread to terminate
    moq_example::cv.notify_one();
}

/*
 *  installSignalHandlers
 *
 *  Description:
 *      This function will install the signal handlers for SIGINT, SIGQUIT,
 *      etc. so that the process can be terminated in a controlled fashion.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
installSignalHandlers()
{
#ifdef _WIN32
    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        std::cerr << "Failed to install SIGINT handler" << std::endl;
    }
#else
    struct sigaction sa = {};

    // Configure the sigaction struct
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Catch SIGHUP (signal 1)
    if (sigaction(SIGHUP, &sa, nullptr) == -1) {
        std::cerr << "Failed to install SIGHUP handler" << std::endl;
    }

    // Catch SIGINT (signal 2)
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        std::cerr << "Failed to install SIGINT handler" << std::endl;
    }

    // Catch SIGQUIT (signal 3)
    if (sigaction(SIGQUIT, &sa, nullptr) == -1) {
        std::cerr << "Failed to install SIGQUIT handler" << std::endl;
    }
#endif
}
