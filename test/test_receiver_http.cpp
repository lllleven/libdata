#include "http_signaling.hpp"
#include "rtc/rtc.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <variant>

using std::shared_ptr;
using std::weak_ptr;

using namespace std::chrono_literals;
using namespace rtc;

template <class T>
constexpr weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) {
    return ptr;
}

namespace {

constexpr uint16_t defaultIcePortBegin = 9300;
constexpr uint16_t defaultIcePortEnd = 9400;

void printUsage(const char *program) {
    std::cerr << "用法: " << program << " <signaling_url> <session_id> <expected_mb> [stun_server]\n"
                 "示例: ./test_receiver_http http://signaling:9355 test_session 500 stun:stun.l.google.com:19302\n";
}

double bytesToMb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printUsage(argv[0]);
        return -1;
    }

    libdatachannel::CurlGlobalGuard curlGuard;
    InitLogger(LogLevel::Warning);
    Preload();

    const std::string signalingUrl = argv[1];
    const std::string sessionId = argv[2];
    uint64_t expectedMb = 0;

    try {
        expectedMb = std::stoull(argv[3]);
    } catch (const std::exception &e) {
        std::cerr << "参数解析失败: " << e.what() << "\n";
        printUsage(argv[0]);
        return -1;
    }

    const std::optional<std::string> stunServer = (argc >= 5 ? std::optional<std::string>(argv[4]) : std::nullopt);
    const uint64_t totalBytes = expectedMb * 1024ull * 1024ull;

    Configuration config;
    if (stunServer)
        config.iceServers.emplace_back(*stunServer);

    config.portRangeBegin = defaultIcePortBegin;
    config.portRangeEnd = defaultIcePortEnd;

    PeerConnection pc(config);
    libdatachannel::HttpSignaling signaling(signalingUrl);

    std::atomic<bool> running{true};
    std::atomic<bool> receiveFinished{false};
    std::atomic<uint64_t> receivedBytes{0};
    std::thread candidateThread;
    std::unordered_set<std::string> seenCandidates;

    candidateThread = std::thread([&] {
        while (running.load()) {
            try {
                for (const auto &entry : signaling.fetchSenderCandidates(sessionId)) {
                    const std::string key = entry.candidate + "|" + entry.mid;
                    if (seenCandidates.insert(key).second) {
                        pc.addRemoteCandidate(Candidate(entry.candidate, entry.mid));
                    }
                }
            } catch (const std::exception &e) {
                std::cerr << "获取发送端候选者失败: " << e.what() << "\n";
            }
            std::this_thread::sleep_for(250ms);
        }
    });

    pc.onLocalDescription([&](Description description) {
        try {
            signaling.setAnswer(sessionId, static_cast<std::string>(description));
        } catch (const std::exception &e) {
            std::cerr << "发送 Answer 失败: " << e.what() << "\n";
        }
    });

    pc.onLocalCandidate([&](Candidate candidate) {
        try {
            signaling.addReceiverCandidate(sessionId, static_cast<std::string>(candidate),
                                            candidate.mid());
        } catch (const std::exception &e) {
            std::cerr << "上传接收端候选者失败: " << e.what() << "\n";
        }
    });

    pc.onStateChange([&](PeerConnection::State state) {
        std::cout << "[状态] 接收端: " << state << "\n";
    });

    pc.onDataChannel([&](std::shared_ptr<DataChannel> channel) {
        channel->onOpen([]() {
            std::cout << "[接收] 数据通道已打开\n";
        });

        channel->onMessage([&](variant<binary, string> message) {
            if (std::holds_alternative<binary>(message)) {
                receivedBytes += std::get<binary>(message).size();
                if (totalBytes > 0 && receivedBytes.load() >= totalBytes)
                    receiveFinished.store(true);
            }
        });

        channel->onClosed([&]() {
            std::cout << "[接收] 数据通道已关闭\n";
            if (totalBytes == 0 || receivedBytes.load() >= totalBytes)
                receiveFinished.store(true);
        });
    });

    auto waitForOffer = [&]() {
        auto lastLog = std::chrono::steady_clock::now();
        std::cout << "[接收] 等待 Offer...\n";
        while (true) {
            try {
                auto offer = signaling.fetchOffer(sessionId);
                if (offer) {
                    pc.setRemoteDescription(Description(*offer));
                    pc.setLocalDescription(Description::Type::Answer);
                    return;
                }
            } catch (const std::exception &e) {
                std::cerr << "获取 Offer 失败: " << e.what() << "\n";
            }
            std::this_thread::sleep_for(200ms);
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLog).count() >= 5) {
                std::cout << "[接收] 仍在等待 Offer...\n";
                lastLog = now;
            }
        }
    };

    waitForOffer();

    if (totalBytes == 0)
        receiveFinished.store(true);

    auto startTime = std::chrono::steady_clock::now();
    auto lastReport = startTime;
    while (!receiveFinished.load()) {
        std::this_thread::sleep_for(250ms);
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastReport).count() >= 5) {
            const double mb = bytesToMb(receivedBytes.load());
            std::cout << "[接收] 已接收 " << std::fixed << std::setprecision(2) << mb << " MB\n";
            lastReport = now;
        }
    }

    running.store(false);
    if (candidateThread.joinable())
        candidateThread.join();

    pc.close();

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    if (elapsed.count() == 0)
        elapsed = std::chrono::milliseconds(1);

    const double goodput = bytesToMb(receivedBytes.load()) / (elapsed.count() / 1000.0);
    std::cout << "[总结] 总接收 " << bytesToMb(receivedBytes.load()) << " MB，耗时 "
              << elapsed.count() << " ms，平均速率 " << std::fixed << std::setprecision(2)
              << goodput << " MB/s\n";

    return 0;
}

