#ifndef LIBDATACHANNEL_HTTP_SIGNALING_HPP
#define LIBDATACHANNEL_HTTP_SIGNALING_HPP

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace libdatachannel {

struct CandidateEntry {
	std::string candidate;
	std::string mid;
};

struct CurlGlobalGuard {
	CurlGlobalGuard() { curl_global_init(CURL_GLOBAL_DEFAULT); }
	~CurlGlobalGuard() { curl_global_cleanup(); }
};

class HttpSignaling {
public:
	explicit HttpSignaling(std::string base_url)
	    : base_url_(std::move(base_url)) {
		if (!base_url_.empty() && base_url_.back() == '/')
			base_url_.pop_back();
	}

	void setOffer(const std::string &session, const std::string &sdp) const {
		httpGet(buildPath(session, "/offer"), {{"sdp", sdp}}, false);
	}

	std::optional<std::string> fetchOffer(const std::string &session) const {
		auto payload = httpGet(buildPath(session, "/offer"), {}, true);
		return decodeSdp(payload);
	}

	void setAnswer(const std::string &session, const std::string &sdp) const {
		httpGet(buildPath(session, "/answer"), {{"sdp", sdp}}, false);
	}

	std::optional<std::string> fetchAnswer(const std::string &session) const {
		auto payload = httpGet(buildPath(session, "/answer"), {}, true);
		return decodeSdp(payload);
	}

	void addSenderCandidate(const std::string &session,
	                        const std::string &candidate,
	                        const std::string &mid) const {
		httpGet(buildPath(session, "/candidate/sender"),
		        {{"candidate", candidate}, {"mid", mid}}, false);
	}

	void addReceiverCandidate(const std::string &session,
	                          const std::string &candidate,
	                          const std::string &mid) const {
		httpGet(buildPath(session, "/candidate/receiver"),
		        {{"candidate", candidate}, {"mid", mid}}, false);
	}

	std::vector<CandidateEntry> fetchSenderCandidates(const std::string &session) const {
		auto payload = httpGet(buildPath(session, "/candidate/sender"), {}, true);
		return parseCandidates(payload);
	}

	std::vector<CandidateEntry> fetchReceiverCandidates(const std::string &session) const {
		auto payload = httpGet(buildPath(session, "/candidate/receiver"), {}, true);
		return parseCandidates(payload);
	}

	void clearSession(const std::string &session) const {
		httpDelete(buildPath(session, ""), true);
	}

private:
	static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userdata) {
		auto *buffer = static_cast<std::string *>(userdata);
		buffer->append(static_cast<char *>(contents), size * nmemb);
		return size * nmemb;
	}

	std::string buildPath(const std::string &session, const std::string &suffix) const {
		return "/session/" + urlEncode(session) + suffix;
	}

	std::optional<std::string> decodeSdp(const std::string &payload) const {
		if (payload.empty())
			return std::nullopt;
		try {
			auto document = nlohmann::json::parse(payload);
			if (document.contains("sdp") && document["sdp"].is_string()) {
				return document["sdp"].get<std::string>();
			}
		} catch (const nlohmann::json::parse_error &) {
			// fall through
		}
		return std::nullopt;
	}

	std::vector<CandidateEntry> parseCandidates(const std::string &payload) const {
		std::vector<CandidateEntry> entries;
		if (payload.empty())
			return entries;
		try {
			auto document = nlohmann::json::parse(payload);
			if (!document.contains("candidates") || !document["candidates"].is_array())
				return entries;

			for (const auto &item : document["candidates"]) {
				if (!item.contains("candidate") || !item.contains("mid"))
					continue;
				if (!item["candidate"].is_string() || !item["mid"].is_string())
					continue;
				entries.push_back({item["candidate"].get<std::string>(),
				                   item["mid"].get<std::string>()});
			}
		} catch (const nlohmann::json::parse_error &) {
			// ignore malformed responses
		}
		return entries;
	}

	std::string httpGet(const std::string &endpoint,
	                    const std::map<std::string, std::string> &params,
	                    bool allow_not_found) const {
		std::string url = base_url_ + endpoint;
		if (!params.empty()) {
			url.push_back('?');
			bool first = true;
			for (const auto &[key, value] : params) {
				if (!first)
					url.push_back('&');
				first = false;
				url += urlEncode(key);
				url.push_back('=');
				url += urlEncode(value);
			}
		}

		CURL *curl = curl_easy_init();
		if (!curl)
			throw std::runtime_error("Failed to initialize libcurl");

		std::string response;
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

		auto code = curl_easy_perform(curl);
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		curl_easy_cleanup(curl);

		if (code != CURLE_OK)
			throw std::runtime_error("Signaling request failed: " + std::string(curl_easy_strerror(code)));

		if (http_code == 404 && allow_not_found)
			return {};

		if (http_code >= 400)
			throw std::runtime_error("Signaling request returned HTTP " + std::to_string(http_code));

		return response;
	}

	void httpDelete(const std::string &endpoint, bool ignore_not_found) const {
		std::string url = base_url_ + endpoint;
		CURL *curl = curl_easy_init();
		if (!curl)
			throw std::runtime_error("Failed to initialize libcurl");

		std::string response;
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

		auto code = curl_easy_perform(curl);
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		curl_easy_cleanup(curl);

		if (code != CURLE_OK)
			throw std::runtime_error("Signaling request failed: " + std::string(curl_easy_strerror(code)));

		if (http_code == 404 && ignore_not_found)
			return;

		if (http_code >= 400)
			throw std::runtime_error("Signaling DELETE returned HTTP " + std::to_string(http_code));
	}

	std::string urlEncode(const std::string &value) const {
		CURL *curl = curl_easy_init();
		if (!curl)
			throw std::runtime_error("Failed to initialize libcurl for encoding");
		char *encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
		if (!encoded) {
			curl_easy_cleanup(curl);
			throw std::runtime_error("Failed to encode URL component");
		}
		std::string result(encoded);
		curl_free(encoded);
		curl_easy_cleanup(curl);
		return result;
	}

	std::string base_url_;
};

} // namespace libdatachannel

#endif

