// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/handlers/publish_track_handler.h>
#include <quicr/session_callbacks.h>
#include <quicr/session_manager.h>

#include <iostream>

class SimpleClientCallbacks : public quicr::Session::ClientCallbacks
{
    struct Private
    {
        explicit Private() = default;
    };

  public:
    SimpleClientCallbacks(Private) {}

    static std::shared_ptr<SimpleClientCallbacks> Create()
    {
        return std::make_shared<SimpleClientCallbacks>(Private{});
    }

    quicr::Expected<void, quicr::Error<quicr::ErrorCode>> SetupReceived(
      const std::shared_ptr<quicr::Session>&,
      const quicr::SetupAttributes& setup_attributes) override
    {
        std::cerr << "Setup received from '" << setup_attributes.endpoint_id << "'" << std::endl;
        return {};
    }
};

int
main()
try {
    quicr::ClientConfig config;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = "";
    config.transport_config.tls_key_filename = "";

    std::cerr << "Enter relay URI (prefixed with either moq:// or moqt://): ";
    std::getline(std::cin, config.connect_uri);

    quicr::SessionManager session_mgr;
    auto weak_session = session_mgr.AddTransport(config, SimpleClientCallbacks::Create());

    if (auto session = weak_session.lock()) {
        while (session->GetStatus() != quicr::Session::Status::kReady) {
            const auto status = session->GetStatus();
            if (status == quicr::Session::Status::kNotReady || status == quicr::Session::Status::kInternalError ||
                status == quicr::Session::Status::kInvalidParams || status == quicr::Session::Status::kNotConnected ||
                status == quicr::Session::Status::kFailedToConnect) {
                std::cerr << "Failed to connect" << std::endl;
                return EXIT_FAILURE;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        quicr::TrackNamespace track_namespace = [] {
            std::string str;
            std::cerr << "Enter track namespace entries delimited by / (e.g. test/track): ";
            std::cin >> str;

            std::vector<std::string> entries;
            std::size_t pos = 0;
            while ((pos = str.find('/')) != std::string::npos) {
                entries.emplace_back(str.substr(0, pos));
                str.erase(0, pos + 1);
            }
            entries.emplace_back(std::move(str));

            return quicr::TrackNamespace{ entries };
        }();

        quicr::FullTrackName ftn = [](const quicr::TrackNamespace& ns) {
            std::string str;
            std::cerr << "Enter a name for the track: ";
            std::cin >> str;
            return quicr::FullTrackName{ ns, quicr::AsOwnedBytes(str) };
        }(track_namespace);

        auto handler = quicr::PublishTrackHandler::Create(
          ftn, quicr::TrackMode::kStream, 127, 127, 5000, quicr::messages::Location{});

        session_mgr.AddHandler(session, handler);

        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        std::string text;
        for (;;) {
            std::cerr << "Enter text (/quit to exit): ";

            if (!std::getline(std::cin, text) || text == "/quit") {
                std::cerr << std::endl;
                break;
            }

            quicr::ObjectHeaders headers;
            handler->PublishObject(headers, quicr::AsBytes(text));
        }

        std::cerr << "Done publishing! Goodbye" << std::endl;

        return EXIT_SUCCESS;
    }

    std::cerr << "Failed to connect" << std::endl;

    return EXIT_FAILURE;
} catch (const std::exception& e) {
    std::cerr << "Caught exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
