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
 *   ./test_sender_http http://192.168.1.10:9222 test_session_1 500 65535 stun:stun.l.google.com:19302
 *   传输500MB文件，每个消息块65535字节
 */

#include "rtc/rtc.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <map>
#include <curl/curl.h>
#include <cctype>
#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

using namespace rtc;
using namespace std;
using namespace chrono_literals;

using chrono::duration_cast;
using chrono::milliseconds;
using chrono::steady_clock;
using chrono::seconds;

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

    // URL编码辅助函数
    string urlEncode(const string& str) {
        if (!curl) return str;
        char* encoded = curl_easy_escape(curl, str.c_str(), str.length());
        if (!encoded) return str;
        string result(encoded);
        curl_free(encoded);
        return result;
    }
    
    // 使用GET方法发送数据（通过查询参数）
    string httpGetWithParams(const string& endpoint, const map<string, string>& params, long* httpCode = nullptr) {
        if (!curl) {
            cerr << "[HTTP] 错误: CURL对象未初始化" << endl;
            if (httpCode) *httpCode = 0;
            return "";
        }
        
        lock_guard<mutex> lock(curlMutex);
        string response;
        string url = serverUrl + endpoint;
        
        // 构建查询参数
        if (!params.empty()) {
            url += "?";
            bool first = true;
            for (const auto& [key, value] : params) {
                if (!first) url += "&";
                url += key + "=" + urlEncode(value);
                first = false;
            }
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // 设置超时时间
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        
        CURLcode res;
        try {
            res = curl_easy_perform(curl);
        } catch (...) {
            cerr << "[HTTP] curl_easy_perform 抛出异常" << endl;
            if (httpCode) *httpCode = 0;
            return "";
        }
        
        if (res != CURLE_OK) {
            cerr << "[HTTP] GET失败: " << curl_easy_strerror(res) << " (URL: " << url << ")" << endl;
            if (httpCode) *httpCode = 0;
            return "";
        }
        
        if (httpCode) {
            CURLcode infoRes = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, httpCode);
            if (infoRes != CURLE_OK) {
                cerr << "[HTTP] 警告: 无法获取HTTP状态码: " << curl_easy_strerror(infoRes) << endl;
                *httpCode = 0;
            }
        }
        
        return response;
    }

    string httpGet(const string& endpoint, long* httpCode = nullptr) {
        cout << "[HTTP] httpGet 开始，endpoint: " << endpoint << endl;
        cout.flush();
        
        if (!curl) {
            cerr << "[HTTP] 错误: CURL对象未初始化" << endl;
            cerr.flush();
            if (httpCode) *httpCode = 0;
            return "";
        }
        
        cout << "[HTTP] 获取锁..." << endl;
        cout.flush();
        lock_guard<mutex> lock(curlMutex);  // 保护curl对象访问
        cout << "[HTTP] 已获取锁" << endl;
        cout.flush();
        
        string response;
        string url = serverUrl + endpoint;
        cout << "[HTTP] 准备请求URL: " << url << endl;
        cout.flush();
        
        cout << "[HTTP] 设置CURL选项..." << endl;
        cout.flush();
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // 设置超时时间（避免无限等待）
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 连接超时10秒
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);        // 总超时10秒
        
        cout << "[HTTP] 执行curl_easy_perform..." << endl;
        cout.flush();
        
        CURLcode res;
        try {
            res = curl_easy_perform(curl);
        } catch (...) {
            cerr << "[HTTP] curl_easy_perform 抛出异常" << endl;
            cerr.flush();
            if (httpCode) *httpCode = 0;
            return "";
        }
        
        cout << "[HTTP] curl_easy_perform 返回，结果: " << res << " (" << curl_easy_strerror(res) << ")" << endl;
        cout.flush();
        
        if (res != CURLE_OK) {
            cerr << "[HTTP] GET失败: " << curl_easy_strerror(res) << " (URL: " << url << ")" << endl;
            cerr.flush();
            if (httpCode) *httpCode = 0;
            return "";
        }
        
        if (httpCode) {
            CURLcode infoRes = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, httpCode);
            if (infoRes != CURLE_OK) {
                cerr << "[HTTP] 警告: 无法获取HTTP状态码: " << curl_easy_strerror(infoRes) << endl;
                *httpCode = 0;  // 设置为0表示未知状态码
            }
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
        cout << "[信令] 准备发送Offer，SDP长度: " << sdp.length() << endl;
        cout.flush();
        
        string endpoint = "/session/" + sessionId + "/offer";
        map<string, string> params;
        params["sdp"] = sdp;
        
        cout << "[信令] 发送Offer到: " << serverUrl << endpoint << endl;
        cout.flush();
        
        long httpCode = 0;
        string response = httpGetWithParams(endpoint, params, &httpCode);
        
        if (httpCode == 0) {
            cerr << "[错误] 发送Offer失败：无法连接到服务器" << endl;
            cerr.flush();
            return;
        }
        
        if (httpCode != 200) {
            cerr << "[错误] 发送Offer失败：HTTP状态码 " << httpCode << endl;
            if (!response.empty()) {
                cerr << "[错误] 响应内容: " << response.substr(0, min(response.length(), size_t(200))) << endl;
            }
            cerr.flush();
            return;
        }
        
        if (response.find("\"error\"") != string::npos) {
            cerr << "[错误] 发送Offer失败：服务器返回错误" << endl;
            cerr << "[错误] 响应内容: " << response.substr(0, min(response.length(), size_t(200))) << endl;
            cerr.flush();
            return;
        }
        
        cout << "[信令] 已发送Offer到服务器 (HTTP " << httpCode << ")" << endl;
        if (!response.empty()) {
            cout << "[信令] 服务器响应: " << response.substr(0, min(response.length(), size_t(100))) << endl;
        }
        cout.flush();
    }

    string getAnswer() {
        string endpoint = "/session/" + sessionId + "/answer";
        bool firstCheck = true;
        bool serverReachable = false;
        
        cout << "[信令] 开始获取Answer，端点: " << serverUrl << endpoint << endl;
        cout.flush();
        
        for (int i = 0; i < 60; ++i) {
            long httpCode = -1;  // 初始化为-1，便于区分未设置的情况
            string response;
            
            try {
                cout << "[信令] 第 " << (i+1) << " 次尝试获取Answer..." << endl;
                cout.flush();
                
                response = httpGet(endpoint, &httpCode);
                
                cout << "[信令] httpGet 返回，状态码: " << httpCode << ", 响应长度: " << response.length() << endl;
                if (!response.empty() && response.length() < 500) {
                    cout << "[信令] 响应内容: " << response << endl;
                }
                cout.flush();
            } catch (const exception& e) {
                cerr << "[错误] httpGet异常: " << e.what() << endl;
                cerr.flush();
                return "";
            } catch (...) {
                cerr << "[错误] httpGet发生未知异常" << endl;
                cerr.flush();
                return "";
            }
            
            // 如果 httpGet 返回空且状态码为0，说明连接失败
            if (response.empty() && httpCode == 0) {
                cerr << "[错误] 无法连接到信令服务器，httpGet 返回空响应" << endl;
                cerr.flush();
                if (firstCheck) {
                    return "";  // 第一次就失败，直接返回
                }
                // 不是第一次，继续重试
                this_thread::sleep_for(1s);
                continue;
            }
            
            // 检查响应内容
            bool hasError = !response.empty() && response.find("\"error\"") != string::npos;
            bool hasSdp = !response.empty() && response.find("\"sdp\"") != string::npos;
            
            // 检查服务器是否可达
            if (firstCheck) {
                cout << "[信令] 首次检查，HTTP状态码: " << httpCode << ", 响应长度: " << response.length() << endl;
                if (!response.empty()) {
                    cout << "[信令] 响应内容: " << response.substr(0, min(response.length(), size_t(200))) << endl;
                }
                cout.flush();
                
                if (httpCode == 0 || httpCode == -1) {
                    cerr << "[错误] 无法连接到信令服务器: " << serverUrl << endl;
                    cerr << "[错误] 请确认信令服务器是否正在运行" << endl;
                    cerr << "[错误] 响应内容: " << (response.empty() ? "(空)" : response.substr(0, 200)) << endl;
                    cerr.flush();
                    return "";
                } else if (httpCode == 200 && hasSdp) {
                    serverReachable = true;
                    cout << "[信令] 信令服务器连接正常，已收到Answer" << endl;
                    cout.flush();
                } else if (httpCode == 404 || (httpCode == 200 && hasError)) {
                    serverReachable = true;
                    cout << "[信令] 信令服务器连接正常，等待接收端发送Answer..." << endl;
                    if (hasError) {
                        cout << "[信令] 服务器响应: " << response.substr(0, min(response.length(), size_t(100))) << endl;
                    }
                    cout.flush();
                } else {
                    cerr << "[错误] 信令服务器返回错误状态码: " << httpCode << endl;
                    cerr << "[错误] 响应内容: " << (response.empty() ? "(空)" : response.substr(0, 200)) << endl;
                    cerr.flush();
                    return "";
                }
                firstCheck = false;
            }
            
            // 如果响应包含错误信息，继续等待（不返回）
            if (hasError && !hasSdp) {
                // 404 或包含 error 的响应，继续等待
                this_thread::sleep_for(1s);
                if (i % 5 == 0) {
                    cout << "[信令] 等待远程Answer... (" << i << "秒)" << endl;
                }
                continue;
            }
            
            if (hasSdp) {
                // 简单解析JSON
                size_t sdpStart = response.find("\"sdp\":\"") + 7;
                size_t sdpEnd = response.find("\"", sdpStart);
                if (sdpEnd != string::npos) {
                    string sdp = response.substr(sdpStart, sdpEnd - sdpStart);
                    // 恢复所有转义字符
                    size_t pos = 0;
                    while ((pos = sdp.find("\\", pos)) != string::npos && pos < sdp.length() - 1) {
                        char next = sdp[pos + 1];
                        switch (next) {
                            case 'n': sdp.replace(pos, 2, "\n"); break;
                            case 'r': sdp.replace(pos, 2, "\r"); break;
                            case 't': sdp.replace(pos, 2, "\t"); break;
                            case '"': sdp.replace(pos, 2, "\""); break;
                            case '\\': sdp.replace(pos, 2, "\\"); pos++; break;  // 跳过下一个字符，避免重复处理
                            default: pos++; continue;  // 未知转义序列，跳过
                        }
                    }
                    cout << "[信令] 已获取远程Answer" << endl;
                    return sdp;
                }
            } else {
                // 其他情况（404 或包含 error），继续等待
            this_thread::sleep_for(1s);
            if (i % 5 == 0) {
                cout << "[信令] 等待远程Answer... (" << i << "秒)" << endl;
                }
                continue;
            }
        }
        
        cerr << "[错误] 超时: 60秒内未能获取到接收端的Answer" << endl;
        cerr << "[错误] 请确认接收端是否已启动并连接到信令服务器" << endl;
        cerr.flush();
        return "";
    }

    void addCandidate(const string& candidate, const string& mid, bool isSender) {
        string endpoint = "/session/" + sessionId + "/candidate/" + (isSender ? "sender" : "receiver");
        map<string, string> params;
        params["candidate"] = candidate;
        params["mid"] = mid;
        httpGetWithParams(endpoint, params);
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
            
            // 恢复所有转义字符
            size_t escPos = 0;
            while ((escPos = candidate.find("\\", escPos)) != string::npos && escPos < candidate.length() - 1) {
                char next = candidate[escPos + 1];
                switch (next) {
                    case 'n': candidate.replace(escPos, 2, "\n"); break;
                    case 'r': candidate.replace(escPos, 2, "\r"); break;
                    case 't': candidate.replace(escPos, 2, "\t"); break;
                    case '"': candidate.replace(escPos, 2, "\""); break;
                    case '\\': candidate.replace(escPos, 2, "\\"); escPos++; break;
                    default: escPos++; continue;
                }
            }
            
            candidates.push_back({mid, candidate});
            pos = midEnd;
        }
        
        return candidates;
    }
};

void runSender(const string& serverUrl, const string& sessionId, 
               size_t fileSizeMB, size_t chunkSize, size_t bufferThreshold,
               const string& stunServer) {
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
    PLOGI << "Sender initialized: session=" << sessionId << ", chunkSize=" << chunkSize << ", bufferThreshold=" << bufferThreshold;

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
                &sentBytes, &sentChunks, totalBytes, chunkSize, bufferThreshold]() {
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
                if (dc->bufferedAmount() < bufferThreshold) {
                    size_t remaining = totalBytes - currentSent;
                    size_t toSend = min(remaining, chunkSize);
                    
                    if (toSend == chunkSize) {
                        dc->send(chunkData);
                        cout << "[发送] 已推进 " << toSend << " 字节, buffered=" << dc->bufferedAmount() << endl;
                        PLOGD << "Sent chunk " << toSend << " bytes, buffered=" << dc->bufferedAmount();
                    } else {
                        // 发送最后一个不完整的块
                        binary lastChunk(chunkData.begin(), chunkData.begin() + toSend);
                        dc->send(lastChunk);
                        cout << "[发送] 已发送最后一块 " << toSend << " 字节, buffered=" << dc->bufferedAmount() << endl;
                        PLOGD << "Sent last chunk " << toSend << " bytes, buffered=" << dc->bufferedAmount();
                    }
                    
                    currentSent += toSend;
                    sentBytes += toSend;
                    sentChunks++;
                    
                    // 每发送10MB显示一次进度
                    if (sentBytes.load() % (10 * 1024 * 1024) < chunkSize) {
                        double progress = (sentBytes.load() * 100.0) / totalBytes;
                        cout << "[进度] " << fixed << setprecision(1) << progress 
                             << "% (" << (sentBytes.load() / 1024 / 1024) << " MB)" << endl;
                        PLOGI << "Progress " << fixed << setprecision(1) << progress << "% (" << (sentBytes.load() / 1024 / 1024) << " MB)";
                    }
                } else {
                    static auto lastBufferLog = steady_clock::now();
                    auto now = steady_clock::now();
                    if (duration_cast<seconds>(now - lastBufferLog) >= 1s) {
                        cout << "[发送] 缓冲量过大, bufferedAmount=" << dc->bufferedAmount() << endl;
                        PLOGW << "Buffer overshoot: " << dc->bufferedAmount();
                        lastBufferLog = now;
                    }
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
                              &sentBytes, &sentChunks, totalBytes, chunkSize, bufferThreshold]() {
        auto dc = wdc.lock();
        if (!dc)
            return;

        try {
            size_t currentSent = sentBytes.load();
            while (dc->isOpen() && currentSent < totalBytes && dc->bufferedAmount() < bufferThreshold) {
                size_t remaining = totalBytes - currentSent;
                size_t toSend = min(remaining, chunkSize);
                
                if (toSend == chunkSize) {
                    dc->send(chunkData);
                    PLOGD << "Buffered low send chunk " << toSend << " bytes, buffered=" << dc->bufferedAmount();
                } else {
                    binary lastChunk(chunkData.begin(), chunkData.begin() + toSend);
                    dc->send(lastChunk);
                    PLOGD << "Buffered low send last chunk " << toSend << " bytes, buffered=" << dc->bufferedAmount();
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
    cout.flush();  // 确保日志立即输出
    
    cout << "[信令] 准备调用 getAnswer()..." << endl;
    cout.flush();

    // 读取远程answer
    string answerSdp;
    try {
        cout << "[信令] 开始调用 signaling.getAnswer()..." << endl;
        cout.flush();
        
        answerSdp = signaling.getAnswer();
        
        cout << "[信令] getAnswer() 返回，结果长度: " << answerSdp.length() << endl;
        cout.flush();
    } catch (const exception& e) {
        cerr << "[错误] getAnswer() 异常: " << e.what() << endl;
        cerr.flush();
        cerr << "[错误] 程序退出" << endl;
        cerr.flush();
        return;
    } catch (...) {
        cerr << "[错误] getAnswer() 发生未知异常" << endl;
        cerr.flush();
        cerr << "[错误] 程序退出" << endl;
        cerr.flush();
        return;
    }
    
    if (answerSdp.empty()) {
        cerr << "[错误] 未能获取远程Answer，程序退出" << endl;
        cerr << "[提示] 请确保:" << endl;
        cerr << "  1. 信令服务器正在运行 (http://localhost:9227)" << endl;
        cerr << "  2. 接收端已启动并连接到信令服务器" << endl;
        cerr << "  3. 接收端和发送端使用相同的会话ID: " << sessionId << endl;
        cerr.flush();
        return;
    }
    
    cout << "[信令] 成功获取Answer，准备设置远程描述..." << endl;
    cout.flush();

    pc.setRemoteDescription(Description(answerSdp));
    cout << "[信令] 已设置远程Answer" << endl;

    // 持续读取并添加远程候选者
    thread candidateReader([&pc, &signaling]() {
        set<string> addedCandidates;
        int idleCount = 0;

        while (true) {
            auto state = pc.state();
            if (state == PeerConnection::State::Closed ||
                state == PeerConnection::State::Connected ||
                state == PeerConnection::State::Failed ||
                state == PeerConnection::State::Disconnected) {
                break;
            }

            auto candidates = signaling.getRemoteCandidates(true);
            bool hasNew = false;
            for (const auto& [mid, candidate] : candidates) {
                string key = mid + "|" + candidate;
                if (addedCandidates.find(key) == addedCandidates.end()) {
                    try {
                        pc.addRemoteCandidate(Candidate(candidate, mid));
                        addedCandidates.insert(key);
                        hasNew = true;
                    } catch (...) {
                        // 忽略重复添加的错误
                    }
                }
            }

            if (hasNew) {
                idleCount = 0;
            } else {
                idleCount = min(idleCount + 1, 20);
            }

            auto waitDuration = idleCount > 5 ? 1s : 500ms;
            this_thread::sleep_for(waitDuration);
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
    string serverUrl = "http://localhost:9227";
    string sessionId = "test_session_1";
    size_t fileSizeMB = 500;  // 默认500MB
    size_t chunkSize = 65536;  // 默认64KB
    size_t bufferThreshold = 10 * 1024 * 1024;  // 默认10MB
    string stunServer;

    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::debug, &consoleAppender);

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
    }
    if (argc > 5) {
        bufferThreshold = atoi(argv[5]);
    }
    if (argc > 6) {
        stunServer = argv[6];
    }

    // 初始化CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        runSender(serverUrl, sessionId, fileSizeMB, chunkSize, bufferThreshold, stunServer);
        curl_global_cleanup();
        return 0;
    } catch (const std::exception &e) {
        cerr << "传输失败: " << e.what() << endl;
        curl_global_cleanup();
        return -1;
    }
}

