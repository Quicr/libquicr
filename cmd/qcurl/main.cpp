/*
 *  main.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      Main file for the qcurl client.
 *
 *  Portability Issues:
 *      None.
 */

#include <iostream>
#include <stdlib.h>
//#ifndef _WIN32
//#include <sys/types.h>
//#include <unistd.h>
//#endif
#include <csignal>
#include <sstream>
#include <cstdint>
#include "options_parser.h"
#include "quicr/quicr_client.h"
#include "publisher.h"
#include "subscriber.h"
#include "fake_transport_delegate.h"

// Module-level variables used to control program execution
namespace qcurl
{
static std::atomic<bool> terminate(false);      // Termination flag
static std::condition_variable event;           // Main thread waits on this
static const char *termination_reason = nullptr;// Termination reason
}

/*
 *  SignalHandler
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
static void SignalHandler(int signal_number)
{
    // If termination is in process, just return
    if (qcurl::terminate) return;

    // Indicate that the process should terminate
    qcurl::terminate = true;

    // Set the termination reason string
    switch (signal_number)
    {
        case SIGINT:
            qcurl::termination_reason = "Interrupt signal received";
            break;

#ifndef _WIN32
        case SIGHUP:
            qcurl::termination_reason = "Hangup signal received";
            break;

        case SIGQUIT:
            qcurl::termination_reason = "Quit signal received";
            break;
#endif

        default:
            qcurl::termination_reason = "Unknown signal received";
            break;
    }

    // Notify the main execution thread to terminate
    qcurl::event.notify_one();
}

/*
 *  InstallSignalHandlers
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
static void InstallSignalHandlers()
{
#ifdef _WIN32
    if (signal(SIGINT, SignalHandler) == SIG_ERR)
    {
        std::cerr << "Failed to install SIGINT handler" << std::endl;
    }
#else
    struct sigaction sa = {};

    // Configure the sigaction struct
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Catch SIGHUP (signal 1)
    if (sigaction(SIGHUP, &sa, NULL) == -1)        // 1
    {
        std::cerr << "Failed to install SIGHUP handler" << std::endl;
    }

    // Catch SIGINT (signal 2)
    if (sigaction(SIGINT, &sa, NULL) == -1)        // 2
    {
        std::cerr << "Failed to install SIGINT handler" << std::endl;
    }

    // Catch SIGQUIT (signal 3)
    if (sigaction(SIGQUIT, &sa, NULL) == -1)        // 3
    {
        std::cerr << "Failed to install SIGQUIT handler" << std::endl;
    }
#endif
}

/*
 *  main
 *
 *  Description:
 *      This is the main routine for the qcurl tool.
 *
 *  Parameters:
 *      argc [in]
 *          The number of arguments passed to the program.
 *
 *      argv [in]
 *          An array containing the arguments passed to the program.
 *
 *  Returns:
 *      Zero if the program exists normally or non-zero otherwise.
 *
 *  Comments:
 *      None.
 */
int main(int argc, char *argv[])
{
    std::mutex main_mutex;                      // Mutex to control termination
    int result_code = EXIT_SUCCESS;             // Result code to return
    bool debug_mode = false;                    // Debugging enabled
    std::string remote_host;                    // Remote hostname or address
    std::uint16_t remote_port;                  // Remote host's port
    bool publisher_role = false;                // Acting as publisher?
    std::shared_ptr<Publisher> publisher;       // Publisher object
    std::shared_ptr<Subscriber> subscriber;     // Subscriber object
    std::shared_ptr<quicr::QuicRClient> client; // Quicr client

    // clang-format off
    // Specify the program options
    cantina::CommandOptions command_options =
    {
    //   Name                   Req?    Arg?    Default     Description
        {"help",                false,  false,  "",         "Help with program usage"},
        {"debug-mode",          false,  false,  "",         "Debug mode enabled (verbose output)"},
        {"host",                true,   true,   "localhost", "Name or address of remote server"},
        {"port",                true,   true,   "33434",     "Remote port"},
        {"publisher",           false,  false,  "",         "Specify that the client is a publisher"}
    };
    // clang-format on

    // Create the OptionsParser object with the specified command options
    cantina::OptionsParser options_parser(command_options);

    // Process program options
    try
    {
        // Parse the user-provided command-line options
        options_parser.Parse(argc, argv, {"help"});

        // Is help requested?
        if (options_parser.IsOptionSet("help"))
        {
            options_parser.PrintUsage(argv[0], false);
            std::exit(result_code);
        }

        // Enable debugging?
        if (options_parser.IsOptionSet("debug-mode")) debug_mode = true;

        // Get the hostname and port
        remote_host = options_parser.GetOptionValue("host");
        try
        {
            std::string port_string;
            remote_port = std::stoi(options_parser.GetOptionValue("port"));
        }
        catch (...)
        {
            std::cerr << "Unable to parse port number" << std::endl;
            std::exit(EXIT_FAILURE);
        }

        // Operating as a publisher?
        if (options_parser.IsOptionSet("publisher")) publisher_role = true;
    }
    catch (const cantina::OptionsParserException &e)
    {
        std::cerr << e.what() << std::endl;
        options_parser.PrintUsage(argv[0], false);
        std::exit(EXIT_FAILURE);
    }
    catch (const std::out_of_range &e)
    {
        std::cerr << e.what() << std::endl;
        options_parser.PrintUsage(argv[0], false);
        std::exit(EXIT_FAILURE);
    }

    // Install a signal handlers to catch operating system signals
    InstallSignalHandlers();

    try
    {
        // Callback function to allow for graceful termination
        auto task_complete =
                    [&]()
                    {
                        std::lock_guard<std::mutex> lock(main_mutex);
                        qcurl::terminate = true;
                        qcurl::event.notify_one();
                        if (!qcurl::termination_reason)
                        {
                            qcurl::termination_reason = "Task completed";
                        };
                    };

        // Create a transport object
        TransportRemote remote = {remote_host,
                                  remote_port,
                                  qtransport::TransportProtocol::UDP};

        FakeTransportDelegate fake_transport_delegate; //! REMOVE LATER 
        LogHandler fake_log_hander; //! REMOVE LATER
        auto transport = qtransport::ITransport::make_client_transport(
            remote,
            fake_transport_delegate,
            fake_log_hander);

        // Launch the subscriber or publisher object
        if (publisher_role)
        {
            // Create the publisher
            publisher = std::make_shared<Publisher>(task_complete);

            // Create the client
            client = std::make_shared<quicr::QuicRClient>(*transport,
                                                          publisher);

            // Initiate the publisher
            publisher->Run(client);
        }
        else
        {
            subscriber = std::make_shared<Subscriber>(task_complete);

            // Create the client
            client = std::make_shared<quicr::QuicRClient>(*transport,
                                                          subscriber);

            // Initiate the publisher
            subscriber->Run(client);
        }

        // Lock the mutex so that main can then wait on it
        std::unique_lock<std::mutex> lock(main_mutex);

        // Wait until told to terminate
        qcurl::event.wait(lock, [&]() { return qcurl::terminate == true; });

        // Unlock the mutex
        lock.unlock();

        // Terminate objects
        subscriber.reset();
        publisher.reset();

        if (debug_mode)
        {
            // Log why the program was terminated
            if (!qcurl::termination_reason)
            {
                qcurl::termination_reason = "Unknown reason";
            }

            std::cout << "Program termination reason: "
                      << qcurl::termination_reason
                      << std::endl;
        }
    }
    catch (const std::invalid_argument &e)
    {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected exception: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr << "Unexpected exception" << std::endl;
        result_code = EXIT_FAILURE;
    }

    if (debug_mode)
    {
        std::cout << "qcurl exiting with result: " << result_code << std::endl;
    }

    return result_code;
}
