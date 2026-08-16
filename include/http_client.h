#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <map>
#include <vector>

/**
 * @brief Minimal HTTP/SMTP client built on libcurl (P8/P9).
 *
 * Used by the weather API sources (OpenWeatherMap, Weather.gov, IMGW) for
 * HTTP GET and by the notification channels (webhook POST, email SMTP).
 */
namespace astro_mount { namespace http {

/// Perform an HTTP GET request via libcurl.
/// @return true on success (HTTP 2xx); response body in `response`.
bool get(const std::string& url, std::string& response,
         int timeout_seconds = 10,
         const std::string& user_agent = "astro-mount-controller/1.0",
         const std::vector<std::string>& extra_headers = {});

/// Perform an HTTP POST (or custom method) request via libcurl (JSON body).
/// @return true on success (HTTP 2xx).
bool post(const std::string& url, const std::string& body,
          const std::map<std::string, std::string>& headers = {},
          int timeout_seconds = 10,
          const std::string& user_agent = "astro-mount-controller/1.0",
          const std::string& method = "POST");

/// Send an email via SMTP using libcurl (supports STARTTLS + auth).
/// @return true on success.
bool smtpSend(const std::string& smtp_host, int smtp_port, bool use_tls,
              const std::string& username, const std::string& password,
              const std::string& from,
              const std::vector<std::string>& recipients,
              const std::string& subject, const std::string& body,
              int timeout_seconds = 30);

}} // namespace astro_mount::http
#endif // HTTP_CLIENT_H
