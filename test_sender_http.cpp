/**
 * libdatachannel 性能测试 - 发送端 (HTTP信令版本)
 * 
 * 在服务器1上运行此程序作为发送端
 * 
 * 编译:
 *   g++ -std=c++17 test_sender_http.cpp -o test_sender_http -I./include -L./build -ldatachannel -pthread -lssl -lcrypto -lcurl
 * 
 * 使用方法:
 *   ./test_sender_http [信令服务器URL] [会话ID] [文件大小(MB)] [消息块大小(字节)] [STUN服务器]
 * 
 * 示例:
 *   ./test_sender_http http://192.168.1.10:9355 test_session_1 500 65535 stun:stun.l.google.com:19302
 *   传输500MB文件，每个消息块65535字节
 */

#include "rtc/rtc.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <iomanip>
#include <sstream>
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

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* data) {
        data->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    string httpPost(const string& endpoint, const string& jsonData) {
        string response;
        string url = serverUrl + endpoint;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            cerr << "[HTTP] POST失败: " << curl_easy_strerror(res) << endl;
            return "";
        }
        
        return response;
    }

    string httpGet(const string& endpoint) {
        string response;
        string url = serverUrl + endpoint;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        CURLcode res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            cerr << "[HTTP] GET失败: " << curl_easy_strerror(res) << endl;
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

    void setOffer(const string& sdp) {
        stringstream json;
        json << "{\"sdp\":\"" << sdp << "\"}";
        // 转义JSON字符串中的特殊字符
        string jsonStr = json.str();
        // 简单处理：替换换行符
        size_t pos = 0;
        while ((pos = jsonStr.find("\n", pos)) != string::npos) {
            jsonStr.replace(pos, 1, "\\n");
            pos += 2;
        }
        
        string endpoint = "/session/" + sessionId + "/offer";
        string response = httpPost(endpoint, jsonStr);
        cout << "[信令] 已发送Offer到服务器" << endl;
    }

    string getAnswer() {
        string endpoint = "/session/" + sessionId + "/answer";
        for (int i = 0; i < 60; ++i) {
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
                    cout << "[信令] 已获取远程Answer" << endl;
                    return sdp;
                }
            }
            this_thread::sleep_for(1s);
            if (i % 5 == 0) {
                cout << "[信令] 等待远程Answer... (" << i << "秒)" << endl;
            }
        }
        return "";
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
        
        // 简单解析JSON数组（这里需要更完善的JSON解析，但为了简单先这样）
        // 实际使用中建议使用JSON库如nlohmann/json
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

void runSender(const string& serverUrl, const string& sessionId, 
               size_t fileSizeMB, size_t chunkSize, const string& stunServer) {
    size_t totalBytes = fileSizeMB * 1024 * 1024;  // 转换为字节
    
    cout << "========================================" << endl;
    cout << "libdatachannel 文件传输 - 发送端" << endl;
    cout << "========================================" << endl;
    cout << "信令服务器: " << serverUrl << endl;
    cout << "会话ID: " << sessionId << endl;
    cout << "文件大小: " << fileSizeMB << " MB (" << totalBytes << " 字节)" << endl;
    cout << "消息块大小: " << chunkSize << " 字节" << endl;
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

    atomic<bool> offerSent(false);

    // 当生成本地描述时，发送到服务器
    pc.onLocalDescription([&signaling, &offerSent](Description sdp) {
        signaling.setOffer(string(sdp));
        offerSent = true;
    });

    // 当生成本地候选者时，发送到服务器
    pc.onLocalCandidate([&signaling](Candidate candidate) {
        signaling.addCandidate(candidate.candidate(), candidate.mid(), true);
    });

    pc.onStateChange([](PeerConnection::State state) { 
        cout << "[状态] " << state << endl; 
    });

    pc.onGatheringStateChange([](PeerConnection::GatheringState state) {
        cout << "[ICE收集] " << state << endl;
    });

    // 准备测试数据块
    binary chunkData(chunkSize);
    fill(chunkData.begin(), chunkData.end(), byte(0xFF));

    atomic<size_t> sentBytes = 0;
    atomic<size_t> sentChunks = 0;
    steady_clock::time_point startTime, openTime, transferStartTime, transferEndTime;

    // 创建数据通道
    startTime = steady_clock::now();
    auto dc = pc.createDataChannel("file_transfer");

    dc->onOpen([wdc = make_weak_ptr(dc), &chunkData, &openTime, &transferStartTime, &startTime,
                &sentBytes, &sentChunks, totalBytes, chunkSize]() {
        auto dc = wdc.lock();
        if (!dc)
            return;

        openTime = steady_clock::now();
        transferStartTime = steady_clock::now();
        
        auto connectDuration = duration_cast<milliseconds>(openTime - startTime);
        cout << "[连接] 建立连接耗时: " << connectDuration.count() << " ms" << endl;
        cout << "[传输] 开始发送文件 (" << (totalBytes / 1024 / 1024) << " MB)..." << endl;

        // 发送文件数据
        try {
            size_t currentSent = 0;
            while (dc->isOpen() && currentSent < totalBytes) {
                if (dc->bufferedAmount() == 0) {
                    size_t remaining = totalBytes - currentSent;
                    size_t toSend = min(remaining, chunkSize);
                    
                    if (toSend == chunkSize) {
                        dc->send(chunkData);
                    } else {
                        // 发送最后一个不完整的块
                        binary lastChunk(chunkData.begin(), chunkData.begin() + toSend);
                        dc->send(lastChunk);
                    }
                    
                    currentSent += toSend;
                    sentBytes += toSend;
                    sentChunks++;
                    
                    // 每发送10MB显示一次进度
                    if (sentBytes.load() % (10 * 1024 * 1024) < chunkSize) {
                        double progress = (sentBytes.load() * 100.0) / totalBytes;
                        cout << "[进度] " << fixed << setprecision(1) << progress 
                             << "% (" << (sentBytes.load() / 1024 / 1024) << " MB)" << endl;
                    }
                } else {
                    this_thread::sleep_for(1ms);
                }
            }
            cout << "[传输] 文件发送完成" << endl;
        } catch (const std::exception &e) {
            cout << "[错误] 发送失败: " << e.what() << endl;
        }
    });

    // 当缓冲区降低时继续发送
    dc->onBufferedAmountLow([wdc = make_weak_ptr(dc), &chunkData, 
                              &sentBytes, &sentChunks, totalBytes, chunkSize]() {
        auto dc = wdc.lock();
        if (!dc)
            return;

        try {
            size_t currentSent = sentBytes.load();
            while (dc->isOpen() && currentSent < totalBytes && dc->bufferedAmount() == 0) {
                size_t remaining = totalBytes - currentSent;
                size_t toSend = min(remaining, chunkSize);
                
                if (toSend == chunkSize) {
                    dc->send(chunkData);
                } else {
                    binary lastChunk(chunkData.begin(), chunkData.begin() + toSend);
                    dc->send(lastChunk);
                }
                
                currentSent += toSend;
                sentBytes += toSend;
                sentChunks++;
            }
        } catch (const std::exception &e) {
            cout << "[错误] 发送失败: " << e.what() << endl;
        }
    });

    dc->onClosed([]() { 
        cout << "[DataChannel] 已关闭" << endl; 
    });

    // 等待offer发送
    int attempts = 10;
    while (!offerSent && attempts--) {
        this_thread::sleep_for(500ms);
    }

    if (!offerSent) {
        cerr << "[错误] 未能发送Offer" << endl;
        return;
    }

    cout << "[信令] 等待接收端响应..." << endl;

    // 读取远程answer
    string answerSdp = signaling.getAnswer();
    if (answerSdp.empty()) {
        cerr << "[错误] 未能获取远程Answer" << endl;
        return;
    }

    pc.setRemoteDescription(Description(answerSdp));
    cout << "[信令] 已设置远程Answer" << endl;

    // 持续读取并添加远程候选者
    thread candidateReader([&pc, &signaling]() {
        set<string> addedCandidates;
        while (pc.state() != PeerConnection::State::Closed) {
            auto candidates = signaling.getRemoteCandidates(true);
            for (const auto& [mid, candidate] : candidates) {
                string key = mid + "|" + candidate;
                if (addedCandidates.find(key) == addedCandidates.end()) {
                    try {
                        pc.addRemoteCandidate(Candidate(candidate, mid));
                        addedCandidates.insert(key);
                    } catch (...) {
                        // 忽略重复添加的错误
                    }
                }
            }
            this_thread::sleep_for(500ms);
        }
    });

    // 等待连接建立
    attempts = 60;
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

    if (!dc->isOpen()) {
        cerr << "[错误] 数据通道未能打开" << endl;
        candidateReader.detach();
        return;
    }

    cout << "[传输] 连接已建立，开始文件传输..." << endl;

    // 等待文件传输完成
    int maxWaitTime = 300;  // 最多等待5分钟
    while (sentBytes.load() < totalBytes && dc->isOpen() && maxWaitTime > 0) {
        this_thread::sleep_for(1s);
        maxWaitTime--;
        
        // 每10秒显示一次进度
        if (maxWaitTime % 10 == 0) {
            size_t currentSent = sentBytes.load();
            double progress = (currentSent * 100.0) / totalBytes;
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - transferStartTime);
            
            cout << "[进度] " << fixed << setprecision(1) << progress 
                 << "% (" << (currentSent / 1024 / 1024) << " MB / " 
                 << (totalBytes / 1024 / 1024) << " MB)";
            
            if (elapsed.count() > 0) {
                double mbps = (currentSent * 8.0) / (elapsed.count() / 1000.0) / 1000000.0;
                double mbps_data = (currentSent * 1.0) / (elapsed.count() / 1000.0) / 1000000.0;
                cout << " - " << mbps_data << " MB/s (" << mbps << " Mbps)";
            }
            cout << endl;
        }
    }
    
    transferEndTime = steady_clock::now();
    
    // 等待最后的数据传输完成
    this_thread::sleep_for(2s);

    // 关闭连接
    dc->close();
    this_thread::sleep_for(1s);
    pc.close();
    candidateReader.join();

    // 打印最终统计
    size_t totalSent = sentBytes.load();
    size_t totalChunks = sentChunks.load();
    auto totalElapsed = duration_cast<milliseconds>(transferEndTime - transferStartTime);

    cout << "\n========================================" << endl;
    cout << "最终统计结果 (发送端)" << endl;
    cout << "========================================" << endl;
    cout << "文件大小: " << (totalBytes / 1024.0 / 1024.0) << " MB" << endl;
    cout << "实际发送: " << (totalSent / 1024.0 / 1024.0) << " MB" << endl;
    cout << "传输时长: " << totalElapsed.count() / 1000.0 << " 秒" << endl;
    cout << "发送块数: " << totalChunks << endl;
    
    if (totalElapsed.count() > 0) {
        double mbps = (totalSent * 8.0) / (totalElapsed.count() / 1000.0) / 1000000.0;
        double mbps_data = (totalSent * 1.0) / (totalElapsed.count() / 1000.0) / 1000000.0;
        double chunkRate = totalChunks * 1000.0 / totalElapsed.count();
        cout << "平均传输速率: " << fixed << setprecision(2) << mbps_data << " MB/s, " << mbps << " Mbps" << endl;
        cout << "平均块速率: " << fixed << setprecision(2) << chunkRate << " 块/秒" << endl;
    }

    if (auto addr = pc.localAddress())
        cout << "本地地址: " << *addr << endl;
    if (auto addr = pc.remoteAddress())
        cout << "远程地址: " << *addr << endl;

    cout << "========================================" << endl;

    rtc::Cleanup();
}

int main(int argc, char **argv) {
    string serverUrl = "http://localhost:9355";
    string sessionId = "test_session_1";
    size_t fileSizeMB = 500;  // 默认500MB
    size_t chunkSize = 65535;  // 默认65535字节
    string stunServer;

    if (argc > 1) {
        serverUrl = argv[1];
    }
    if (argc > 2) {
        sessionId = argv[2];
    }
    if (argc > 3) {
        fileSizeMB = atoi(argv[3]);
        if (fileSizeMB <= 0) {
            cerr << "错误: 文件大小必须大于0 MB" << endl;
            return -1;
        }
    }
    if (argc > 4) {
        chunkSize = atoi(argv[4]);
        if (chunkSize <= 0 || chunkSize > 65535) {
            cerr << "错误: 消息块大小必须在1-65535字节之间" << endl;
            return -1;
        }
    }
    if (argc > 5) {
        stunServer = argv[5];
    }

    // 初始化CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        runSender(serverUrl, sessionId, fileSizeMB, chunkSize, stunServer);
        curl_global_cleanup();
        return 0;
    } catch (const std::exception &e) {
        cerr << "传输失败: " << e.what() << endl;
        curl_global_cleanup();
        return -1;
    }
}

