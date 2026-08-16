#include "http_client.h"
#include <curl/curl.h>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace astro_mount { namespace http {

namespace {

// Curl write callback: append received data to a std::string.
struct WriteCtx { std::string* out; };

size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteCtx*>(userdata);
    ctx->out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Curl read callback for SMTP (uploads the email body).
struct ReadCtx { const std::string* data; size_t pos{0}; };

size_t readCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<ReadCtx*>(userdata);
    size_t avail = size * nmemb;
    size_t remaining = ctx->data->size() - ctx->pos;
    size_t n = std::min(avail, remaining);
    if (n > 0) {
        std::memcpy(ptr, ctx->data->data() + ctx->pos, n);
        ctx->pos += n;
    }
    return n;
}

bool successfulStatus(CURL* curl) {
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    return code >= 200 && code < 300;
}

} // anonymous namespace

bool get(const std::string& url, std::string& response, int timeout_seconds,
         const std::string& user_agent, const std::vector<std::string>& extra_headers) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    WriteCtx ctx{&response};
    struct curl_slist* headers = nullptr;
    for (const auto& h : extra_headers) {
        headers = curl_slist_append(headers, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    bool ok = (res == CURLE_OK) && successfulStatus(curl);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}

bool post(const std::string& url, const std::string& body,
          const std::map<std::string, std::string>& headers,
          int timeout_seconds, const std::string& user_agent,
          const std::string& method) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist* hdrs = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        hdrs = curl_slist_append(hdrs, h.c_str());
    }
    // Ensure Content-Type for JSON payloads.
    if (headers.find("Content-Type") == headers.end()) {
        hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    CURLcode res = curl_easy_perform(curl);
    bool ok = (res == CURLE_OK) && successfulStatus(curl);

    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return ok;
}

bool smtpSend(const std::string& smtp_host, int smtp_port, bool use_tls,
              const std::string& username, const std::string& password,
              const std::string& from,
              const std::vector<std::string>& recipients,
              const std::string& subject, const std::string& body,
              int timeout_seconds) {
    if (recipients.empty() || from.empty()) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string scheme = use_tls ? "smtps" : "smtp";
    std::string url = scheme + "://" + smtp_host + ":" + std::to_string(smtp_port);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));

    struct curl_slist* rcpts = nullptr;
    for (const auto& r : recipients) {
        rcpts = curl_slist_append(rcpts, r.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpts);

    // RFC 5322 message: headers + blank line + body.
    std::string message = "From: " + from + "\r\n"
                          "To: " + (recipients.empty() ? from : recipients[0]) + "\r\n"
                          "Subject: " + subject + "\r\n"
                          "Date: \r\n"
                          "\r\n" + body + "\r\n";

    ReadCtx ctx{&message, 0};
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCb);
    curl_easy_setopt(curl, CURLOPT_READDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    if (!username.empty()) curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
    if (!password.empty()) curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
    if (!use_tls) {
        // Opportunistic STARTTLS upgrade when the server supports it.
        curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_TRY));
    }

    CURLcode res = curl_easy_perform(curl);
    bool ok = (res == CURLE_OK);

    curl_slist_free_all(rcpts);
    curl_easy_cleanup(curl);
    return ok;
}

}} // namespace astro_mount::http
