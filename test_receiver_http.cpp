/**
 * libdatachannel 性能测试 - 接收端 (HTTP信令版本)
 * 
 * 在服务器2上运行此程序作为接收端
 * 
 * 编译:
 *   g++ -std=c++17 test_receiver_http.cpp -o test_receiver_http -I./include -L./build -ldatachannel -pthread -lssl -lcrypto -lcurl
 * 
 * 使用方法:
 *   ./test_receiver_http [信令服务器URL] [会话ID] [预期文件大小(MB)] [STUN服务器]
 * 
 * 示例:
 *   ./test_receiver_http http://192.168.1.10:9355 test_session_1 500 stun:stun.l.google.com:19302
 *   接收500MB文件
 */

#include "rtc/rtc.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <set>
#include <curl/curl.h>

using namespace rtc;
using namespace std;
using namespace chrono_literals;

using chrono::duration_cast;
using chrono::milliseconds;
using chrono::steady_clock;

template <class T> weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }

class SignalingHttpClient {
private:
    string serverUrl;
    string sessionId;
    CURL* curl;
    mutex curlMutex;  // 保护curl对象的互斥锁

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* data) {
        data->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    string httpPost(const string& endpoint, const string& jsonData) {
        if (!curl) {
            cerr << "[HTTP] 错误: CURL对象未初始化" << endl;
            return "";
        }
        
        lock_guard<mutex> lock(curlMutex);  // 保护curl对象访问
        string response;
        string url = serverUrl + endpoint;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // 设置超时时间
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 连接超时10秒
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);        // 总超时10秒
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            cerr << "[HTTP] POST失败: " << curl_easy_strerror(res) << " (URL: " << url << ")" << endl;
            return "";
        }
        
        return response;
    }

    string httpGet(const string& endpoint) {
        if (!curl) {
            cerr << "[HTTP] 错误: CURL对象未初始化" << endl;
            return "";
        }
        
        lock_guard<mutex> lock(curlMutex);  // 保护curl对象访问
        string response;
        string url = serverUrl + endpoint;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // 设置超时时间
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 连接超时10秒
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);        // 总超时10秒
        
        CURLcode res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            cerr << "[HTTP] GET失败: " << curl_easy_strerror(res) << " (URL: " << url << ")" << endl;
            return "";
        }
        
        return response;
    }

public:
    SignalingHttpClient(const string& url, const string& session) 
        : serverUrl(url), sessionId(session) {
        curl = curl_easy_init();
        if (!curl) {
            throw runtime_error("无法初始化CURL");
        }
    }

    ~SignalingHttpClient() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    string getOffer() {
        string endpoint = "/session/" + sessionId + "/offer";
        for (int i = 0; i < 120; ++i) {
            string response = httpGet(endpoint);
            if (!response.empty() && response.find("\"sdp\"") != string::npos) {
                // 简单解析JSON
                size_t sdpStart = response.find("\"sdp\":\"") + 7;
                size_t sdpEnd = response.find("\"", sdpStart);
                if (sdpEnd != string::npos) {
                    string sdp = response.substr(sdpStart, sdpEnd - sdpStart);
                    // 恢复换行符
                    size_t pos = 0;
                    while ((pos = sdp.find("\\n", pos)) != string::npos) {
                        sdp.replace(pos, 2, "\n");
                        pos += 1;
                    }
                    cout << "[信令] 已获取远程Offer" << endl;
                    return sdp;
                }
            }
            this_thread::sleep_for(1s);
            if (i % 10 == 0) {
                cout << "[信令] 等待远程Offer... (" << i << "秒)" << endl;
            }
        }
        return "";
    }

    void setAnswer(const string& sdp) {
        cout << "[信令] 准备发送Answer，SDP长度: " << sdp.length() << endl;
        cout.flush();
        
        stringstream json;
        json << "{\"sdp\":\"" << sdp << "\"}";
        string jsonStr = json.str();
        // 转义JSON字符串中的特殊字符
        size_t pos = 0;
        while ((pos = jsonStr.find("\n", pos)) != string::npos) {
            jsonStr.replace(pos, 1, "\\n");
            pos += 2;
        }
        
        string endpoint = "/session/" + sessionId + "/answer";
        cout << "[信令] 发送Answer到: " << serverUrl << endpoint << endl;
        cout.flush();
        
        string response = httpPost(endpoint, jsonStr);
        
        if (!response.empty()) {
            cout << "[信令] 服务器响应: " << response.substr(0, min(response.length(), size_t(100))) << endl;
        }
        cout << "[信令] 已发送Answer到服务器" << endl;
        cout.flush();
    }

    void addCandidate(const string& candidate, const string& mid, bool isSender) {
        stringstream json;
        json << "{\"candidate\":\"" << candidate << "\",\"mid\":\"" << mid << "\"}";
        string jsonStr = json.str();
        
        string endpoint = "/session/" + sessionId + "/candidate/" + (isSender ? "sender" : "receiver");
        httpPost(endpoint, jsonStr);
    }

    vector<pair<string, string>> getRemoteCandidates(bool isSender) {
        vector<pair<string, string>> candidates;
        string endpoint = "/session/" + sessionId + "/candidate/" + (isSender ? "receiver" : "sender");
        string response = httpGet(endpoint);
        
        // 简单解析JSON数组
        size_t pos = 0;
        while ((pos = response.find("\"candidate\":\"", pos)) != string::npos) {
            size_t candStart = pos + 14;
            size_t candEnd = response.find("\"", candStart);
            if (candEnd == string::npos) break;
            
            size_t midStart = response.find("\"mid\":\"", candEnd);
            if (midStart == string::npos) break;
            midStart += 7;
            size_t midEnd = response.find("\"", midStart);
            if (midEnd == string::npos) break;
            
            string candidate = response.substr(candStart, candEnd - candStart);
            string mid = response.substr(midStart, midEnd - midStart);
            
            // 恢复转义字符
            size_t escPos = 0;
            while ((escPos = candidate.find("\\n", escPos)) != string::npos) {
                candidate.replace(escPos, 2, "\n");
                escPos += 1;
            }
            
            candidates.push_back({mid, candidate});
            pos = midEnd;
        }
        
        return candidates;
    }
};

void printStats(size_t receivedBytes, milliseconds elapsed, const string& label) {
    if (elapsed.count() > 0) {
        double mbps = (receivedBytes * 8.0) / (elapsed.count() / 1000.0) / 1000000.0;
        double mbps_data = (receivedBytes * 1.0) / (elapsed.count() / 1000.0) / 1000000.0;
        cout << fixed << setprecision(2);
        cout << label << ": " 
             << receivedBytes / 1024.0 / 1024.0 << " MB, "
             << mbps_data << " MB/s, "
             << mbps << " Mbps" << endl;
    }
}

void runReceiver(const string& serverUrl, const string& sessionId, 
                 size_t expectedFileSizeMB, const string& stunServer) {
    size_t expectedBytes = expectedFileSizeMB * 1024 * 1024;  // 转换为字节
    
    cout << "========================================" << endl;
    cout << "libdatachannel 文件传输 - 接收端" << endl;
    cout << "========================================" << endl;
    cout << "信令服务器: " << serverUrl << endl;
    cout << "会话ID: " << sessionId << endl;
    cout << "预期文件大小: " << expectedFileSizeMB << " MB (" << expectedBytes << " 字节)" << endl;
    if (!stunServer.empty()) {
        cout << "STUN服务器: " << stunServer << endl;
    }
    cout << "========================================" << endl;

    rtc::InitLogger(LogLevel::Warning);
    rtc::Preload();

    SignalingHttpClient signaling(serverUrl, sessionId);

    Configuration config;
    if (!stunServer.empty()) {
        config.iceServers.emplace_back(stunServer);
    }
    config.mtu = 1500;
    config.portRangeBegin = 9300;
    config.portRangeEnd = 9400;

    PeerConnection pc(config);

    atomic<size_t> receivedBytes = 0;
    atomic<size_t> receivedChunks = 0;
    steady_clock::time_point transferStartTime, transferEndTime;

    // 接收数据通道
    shared_ptr<DataChannel> dc;
    pc.onDataChannel([&dc, &receivedBytes, &receivedChunks, &transferStartTime, expectedBytes](shared_ptr<DataChannel> incoming) {
        dc = incoming;
        cout << "[DataChannel] 接收到数据通道: \"" << dc->label() << "\"" << endl;

        dc->onOpen([&transferStartTime]() {
            transferStartTime = steady_clock::now();
            cout << "[DataChannel] 已打开，开始接收文件..." << endl;
        });

        dc->onMessage([&receivedBytes, &receivedChunks, expectedBytes](variant<binary, string> message) {
            if (holds_alternative<binary>(message)) {
                const auto &bin = get<binary>(message);
                receivedBytes += bin.size();
                receivedChunks++;
                
                // 每接收10MB显示一次进度
                if (receivedBytes.load() % (10 * 1024 * 1024) < bin.size()) {
                    double progress = (receivedBytes.load() * 100.0) / expectedBytes;
                    cout << "[进度] " << fixed << setprecision(1) << progress 
                         << "% (" << (receivedBytes.load() / 1024 / 1024) << " MB / " 
                         << (expectedBytes / 1024 / 1024) << " MB)" << endl;
                }
            }
        });

        dc->onClosed([]() { 
            cout << "[DataChannel] 已关闭" << endl; 
        });
    });

    // 当生成本地描述时，发送到服务器
    pc.onLocalDescription([&signaling](Description sdp) {
        cout << "[信令] onLocalDescription 回调被触发，准备发送Answer" << endl;
        cout.flush();
        signaling.setAnswer(string(sdp));
    });

    // 当生成本地候选者时，发送到服务器
    pc.onLocalCandidate([&signaling](Candidate candidate) {
        signaling.addCandidate(candidate.candidate(), candidate.mid(), false);
    });

    pc.onStateChange([](PeerConnection::State state) { 
        cout << "[状态] " << state << endl; 
    });

    pc.onGatheringStateChange([](PeerConnection::GatheringState state) {
        cout << "[ICE收集] " << state << endl;
    });

    cout << "[信令] 等待发送端Offer..." << endl;
    cout.flush();

    // 读取远程offer
    string offerSdp = signaling.getOffer();
    if (offerSdp.empty()) {
        cerr << "[错误] 未能获取远程Offer" << endl;
        cerr.flush();
        return;
    }

    cout << "[信令] 已获取Offer，SDP长度: " << offerSdp.length() << endl;
    cout.flush();

    cout << "[信令] 设置远程Offer..." << endl;
    cout.flush();
    pc.setRemoteDescription(Description(offerSdp));
    cout << "[信令] 已设置远程Offer，等待生成Answer..." << endl;
    cout.flush();
    
    // 给一点时间让 Answer 生成
    this_thread::sleep_for(500ms);

    // 持续读取并添加远程候选者
    thread candidateReader([&pc, &signaling]() {
        set<string> addedCandidates;
        while (pc.state() != PeerConnection::State::Closed) {
            auto candidates = signaling.getRemoteCandidates(false);
            for (const auto& [mid, candidate] : candidates) {
                string key = mid + "|" + candidate;
                if (addedCandidates.find(key) == addedCandidates.end()) {
                    try {
                        pc.addRemoteCandidate(Candidate(candidate, mid));
                        addedCandidates.insert(key);
                    } catch (...) {
                        // 忽略错误
                    }
                }
            }
            this_thread::sleep_for(500ms);
        }
    });

    // 等待连接建立
    int attempts = 60;
    while (pc.state() != PeerConnection::State::Connected && attempts--) {
        this_thread::sleep_for(1s);
        if (attempts % 10 == 0) {
            cout << "[等待] 等待连接建立... (" << (60 - attempts) << "秒)" << endl;
        }
    }

    if (pc.state() != PeerConnection::State::Connected) {
        cerr << "[错误] 未能建立连接，当前状态: " << pc.state() << endl;
        candidateReader.detach();
        return;
    }

    // 等待数据通道打开
    attempts = 10;
    while ((!dc || !dc->isOpen()) && attempts--) {
        this_thread::sleep_for(1s);
    }

    if (!dc || !dc->isOpen()) {
        cerr << "[错误] 数据通道未能打开" << endl;
        candidateReader.detach();
        return;
    }

    cout << "[传输] 连接已建立，开始接收文件..." << endl;

    // 等待文件接收完成
    int maxWaitTime = 600;  // 最多等待10分钟
    while (receivedBytes.load() < expectedBytes && dc && dc->isOpen() && maxWaitTime > 0) {
        this_thread::sleep_for(1s);
        maxWaitTime--;
        
        // 每10秒显示一次进度
        if (maxWaitTime % 10 == 0) {
            size_t currentReceived = receivedBytes.load();
            double progress = (currentReceived * 100.0) / expectedBytes;
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - transferStartTime);
            
            cout << "[进度] " << fixed << setprecision(1) << progress 
                 << "% (" << (currentReceived / 1024 / 1024) << " MB / " 
                 << (expectedBytes / 1024 / 1024) << " MB)";
            
            if (elapsed.count() > 0) {
                double mbps = (currentReceived * 8.0) / (elapsed.count() / 1000.0) / 1000000.0;
                double mbps_data = (currentReceived * 1.0) / (elapsed.count() / 1000.0) / 1000000.0;
                cout << " - " << mbps_data << " MB/s (" << mbps << " Mbps)";
            }
            cout << endl;
        }
    }
    
    transferEndTime = steady_clock::now();
    
    // 等待最后的数据传输完成
    this_thread::sleep_for(2s);

    // 关闭连接
    if (dc && dc->isOpen()) {
        dc->close();
    }
    this_thread::sleep_for(1s);
    pc.close();
    candidateReader.join();

    // 打印最终统计
    size_t totalReceived = receivedBytes.load();
    size_t totalChunks = receivedChunks.load();
    auto totalElapsed = duration_cast<milliseconds>(transferEndTime - transferStartTime);

    cout << "\n========================================" << endl;
    cout << "最终统计结果 (接收端)" << endl;
    cout << "========================================" << endl;
    cout << "预期文件大小: " << (expectedBytes / 1024.0 / 1024.0) << " MB" << endl;
    cout << "实际接收: " << (totalReceived / 1024.0 / 1024.0) << " MB" << endl;
    cout << "传输时长: " << totalElapsed.count() / 1000.0 << " 秒" << endl;
    cout << "接收块数: " << totalChunks << endl;
    printStats(totalReceived, totalElapsed, "总吞吐量");
    
    if (totalElapsed.count() > 0 && totalChunks > 0) {
        double avgChunkRate = totalChunks * 1000.0 / totalElapsed.count();
        cout << "平均块速率: " << fixed << setprecision(2) << avgChunkRate << " 块/秒" << endl;
    }
    
    // 验证完整性
    if (totalReceived == expectedBytes) {
        cout << "文件完整性: ✓ 验证通过" << endl;
    } else if (totalReceived < expectedBytes) {
        cout << "文件完整性: ✗ 数据不完整 (缺少 " 
             << ((expectedBytes - totalReceived) / 1024.0 / 1024.0) << " MB)" << endl;
    } else {
        cout << "文件完整性: ⚠ 接收数据超出预期 (多出 " 
             << ((totalReceived - expectedBytes) / 1024.0 / 1024.0) << " MB)" << endl;
    }

    if (auto addr = pc.localAddress())
        cout << "本地地址: " << *addr << endl;
    if (auto addr = pc.remoteAddress())
        cout << "远程地址: " << *addr << endl;

    Candidate local, remote;
    if (pc.getSelectedCandidatePair(&local, &remote)) {
        cout << "本地候选: " << local << endl;
        cout << "远程候选: " << remote << endl;
    }

    cout << "========================================" << endl;

    rtc::Cleanup();
}

int main(int argc, char **argv) {
    string serverUrl = "http://localhost:9227";
    string sessionId = "test_session_1";
    size_t expectedFileSizeMB = 500;  // 默认500MB
    string stunServer;

    if (argc > 1) {
        serverUrl = argv[1];
    }
    if (argc > 2) {
        sessionId = argv[2];
    }
    if (argc > 3) {
        expectedFileSizeMB = atoi(argv[3]);
        if (expectedFileSizeMB <= 0) {
            cerr << "错误: 预期文件大小必须大于0 MB" << endl;
            return -1;
        }
    }
    if (argc > 4) {
        stunServer = argv[4];
    }

    // 初始化CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        runReceiver(serverUrl, sessionId, expectedFileSizeMB, stunServer);
        curl_global_cleanup();
        return 0;
    } catch (const std::exception &e) {
        cerr << "接收失败: " << e.what() << endl;
        curl_global_cleanup();
        return -1;
    }
}

