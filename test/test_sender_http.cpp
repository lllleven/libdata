#include "http_signaling.hpp"
#include "rtc/rtc.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>

template <class T>
constexpr weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) {
    return ptr;
}

using namespace std::chrono_literals;
using namespace rtc;

namespace {

constexpr uint16_t defaultIcePortBegin = 9300;
constexpr uint16_t defaultIcePortEnd = 9400;

void printUsage(const char *program) {
    std::cerr << "用法: " << program << " <signaling_url> <session_id> <file_mb> <chunk_bytes> [stun_server]\n"
                 "示例: ./test_sender_http http://signaling:9355 test_session 500 65535 stun:stun.l.google.com:19302\n";
}

double bytesToMb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printUsage(argv[0]);
        return -1;
    }

    libdatachannel::CurlGlobalGuard curlGuard;
    InitLogger(LogLevel::Warning);
    Preload();

    const std::string signalingUrl = argv[1];
    const std::string sessionId = argv[2];
    uint64_t fileMb = 0;
    uint64_t chunkSize = 0;

    try {
        fileMb = std::stoull(argv[3]);
        chunkSize = std::stoull(argv[4]);
    } catch (const std::exception &e) {
        std::cerr << "参数解析失败: " << e.what() << "\n";
        printUsage(argv[0]);
        return -1;
    }

    if (chunkSize == 0 || chunkSize > 65535) {
        std::cerr << "chunk_bytes 必须在 1 ~ 65535 之间\n";
        return -1;
    }

    const std::optional<std::string> stunServer = (argc >= 6 ? std::optional<std::string>(argv[5]) : std::nullopt);
    const uint64_t totalBytes = fileMb * 1024ull * 1024ull;

    Configuration config;
    if (stunServer)
        config.iceServers.emplace_back(*stunServer);

    config.portRangeBegin = defaultIcePortBegin;
    config.portRangeEnd = defaultIcePortEnd;

    PeerConnection pc(config);
    libdatachannel::HttpSignaling signaling(signalingUrl);

    std::atomic<bool> running{true};
    std::atomic<bool> sendFinished{false};
    std::atomic<uint64_t> sentBytes{0};
    std::thread candidateThread;
    std::thread senderThread;

    std::unordered_set<std::string> seenCandidates;

    if (totalBytes == 0)
        sendFinished.store(true);

    candidateThread = std::thread([&] {
        while (running.load()) {
            try {
                for (const auto &entry : signaling.fetchReceiverCandidates(sessionId)) {
                    const std::string key = entry.candidate + "|" + entry.mid;
                    if (seenCandidates.insert(key).second) {
                        pc.addRemoteCandidate(Candidate(entry.candidate, entry.mid));
                    }
                }
            } catch (const std::exception &e) {
                std::cerr << "获取接收端候选者失败: " << e.what() << "\n";
            }
            std::this_thread::sleep_for(250ms);
        }
    });

    pc.onLocalDescription([&](Description description) {
        try {
            signaling.setOffer(sessionId, static_cast<std::string>(description));
        } catch (const std::exception &e) {
            std::cerr << "发送 Offer 失败: " << e.what() << "\n";
        }
    });

    pc.onLocalCandidate([&](Candidate candidate) {
        try {
            signaling.addSenderCandidate(sessionId, static_cast<std::string>(candidate),
                                          candidate.mid());
        } catch (const std::exception &e) {
            std::cerr << "上传发送端候选者失败: " << e.what() << "\n";
        }
    });

    pc.onStateChange([&](PeerConnection::State state) {
        std::cout << "[状态] 发送端: " << state << "\n";
    });

    auto dataChannel = pc.createDataChannel("http-benchmark");

    dataChannel->onOpen([&, dataChannelWeak = make_weak_ptr(dataChannel)]() {
        if (auto channel = dataChannelWeak.lock()) {
            std::cout << "[发送] 数据通道已打开，准备推送消息\n";
            senderThread = std::thread([&, channel]() {
                auto sendStart = std::chrono::steady_clock::now();
                while (sentBytes.load() < totalBytes && channel->isOpen()) {
                    if (channel->bufferedAmount() >= chunkSize) {
                        std::this_thread::sleep_for(2ms);
                        continue;
                    }

                    const uint64_t remaining = totalBytes - sentBytes.load();
                    const size_t sendSize = static_cast<size_t>(std::min<uint64_t>(chunkSize, remaining));
                    if (sendSize == 0)
                        break;

                    binary message(sendSize);
                    std::fill(message.begin(), message.end(), byte(0xAB));

                    try {
                        channel->send(message);
                        sentBytes += sendSize;
                    } catch (const std::exception &e) {
                        std::cerr << "数据发送失败: " << e.what() << "\n";
                        break;
                    }
                }
                sendFinished.store(true);
                auto sendEnd = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(sendEnd - sendStart);
                if (duration.count() > 0) {
                    double mb = bytesToMb(sentBytes.load());
                    double mbps = (mb * 8.0) / (duration.count() / 1000.0);
                    std::cout << "[发送] 完成 " << mb << " MB, 平均速率 "
                              << std::fixed << std::setprecision(2) << mbps << " Mbit/s\n";
                }
            });
        }
    });

    dataChannel->onClosed([]() {
        std::cout << "[发送] 数据通道已关闭\n";
    });

    auto waitForAnswer = [&]() {
        while (true) {
            try {
                auto answer = signaling.fetchAnswer(sessionId);
                if (answer) {
                    pc.setRemoteDescription(Description(*answer));
                    return;
                }
            } catch (const std::exception &e) {
                std::cerr << "获取 Answer 失败: " << e.what() << "\n";
            }
            std::this_thread::sleep_for(200ms);
        }
    };

    waitForAnswer();

    auto startTime = std::chrono::steady_clock::now();
    auto lastReport = startTime;
    while (!sendFinished.load()) {
        std::this_thread::sleep_for(250ms);
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastReport).count() >= 5) {
            const double mb = bytesToMb(sentBytes.load());
            std::cout << "[发送] 已发送 " << std::fixed << std::setprecision(2) << mb << " MB\n";
            lastReport = now;
        }
    }

    if (senderThread.joinable())
        senderThread.join();

    running.store(false);
    if (candidateThread.joinable())
        candidateThread.join();

    dataChannel->close();
    pc.close();

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    if (elapsed.count() == 0)
        elapsed = std::chrono::milliseconds(1);

    const double goodput = bytesToMb(sentBytes.load()) / (elapsed.count() / 1000.0);
    std::cout << "[总结] 总发送 " << bytesToMb(sentBytes.load()) << " MB，耗时 "
              << elapsed.count() << " ms，平均速率 " << std::fixed << std::setprecision(2)
              << goodput << " MB/s\n";

    return 0;
}

