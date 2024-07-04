/**
 * \file
 * Simple HTTP mock server.
 *
 * To use mock server in your own code, just inherit MockServer and implement
 * responseHandler() method. Multiple instances in single process can lead to
 * undefined behaviour.
 *
 * Linker dependencies: -lmicrohttpd
 */

#pragma once

#include <string>
#include <vector>
#include <memory>


namespace httpmock {


class IMockServer {
  public:
    virtual ~IMockServer() = 0;

    /// Allow to start the server.
    virtual void start() = 0;

    /// Allow to stop the server.
    virtual void stop() = 0;

    /// Return true if server is running.
    virtual bool isRunning() const = 0;
};


class MockServer: public IMockServer {
    class Server;
    /// Server implementation.
    std::unique_ptr<Server> server;
    /// Port to run server on.
    int port;
  private:
    MockServer(const MockServer &);
    MockServer &operator=(const MockServer &);
    // Move is not allowed as a callback is registered for moved object
    // address in the internal daemon...
    MockServer(MockServer &&);
  public:
    /// Create server instance. Server is not started by default. Use start().
    explicit MockServer(int port = 8080);
    virtual ~MockServer();

    /// Starts the server.
    virtual void start() override;
    /// Stops the server.
    virtual void stop() override;

    /// Return true if server is running.
    virtual bool isRunning() const override;

    /// Return port number server is running on.
    int getPort() const;
  protected:
    /// Key-Value storage
    struct KeyValue {
        std::string key;
        std::string value;

        explicit KeyValue(const std::string &key, const std::string &value = "")
            : key(key), value(value)
        {}
    };
    struct Header: public KeyValue {
        Header(const std::string &key, const std::string &value = "")
            : KeyValue(key, value)
        {}
    };
    /// Url argument (with optional value)
    struct UrlArg: public KeyValue {
        /// Whether the value field has been set.
        bool hasValue;

        explicit UrlArg(const std::string &key)
            : KeyValue(key), hasValue(false)
        {}
        UrlArg(const std::string &key, const std::string &value)
            : KeyValue(key, value), hasValue(true)
        {}
    };

    /// Response object
    struct Response {
        int status;             ///< Status code returned to the client
        std::string body;       ///< Body sent to the client
        std::vector<Header> headers;    ///< Response headers.

        explicit Response(int status = 200, const std::string &body = "OK")
          : status(status), body(body), headers()
        {}

        /// Add header to the response.
        Response &addHeader(const Header &h) {
            headers.push_back(h);
            return *this;
        }
    };
  private:
    /**
     * HTTP response handler.
     * \param url       URL path of the request.
     * \param method    HTTP method (GET, POST, ...).
     * \param data      Data received during the request.
     * \param urlArguments  Arguments passed in the URL (after ? sign).
     * \param headers   Headers of the HTTP request.
     */
    virtual Response responseHandler(
            const std::string &url,
            const std::string &method,
            const std::string &data,
            const std::vector<UrlArg> &urlArguments = {},
            const std::vector<Header> &headers = {}) = 0;
};


} // namespace httpmock
