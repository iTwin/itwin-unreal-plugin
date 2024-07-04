/**
 * \file
 * Implementation of mock HTTP server using libmicrohttpd.
 */
#include <stdexcept>
#include <iostream>
#include <microhttpd.h>
#include "httpmockserver/mock_server.h"


// libmicrohttpd 0.9.71 brings incompatibility by changing int to enum.
// Let's use MHD_RESULT instead - defined by libmicrohttpd version.
#if MHD_VERSION >= 0x00097002
#define MHD_RESULT enum MHD_Result
#else
#define MHD_RESULT int
#endif


namespace httpmock {


IMockServer::~IMockServer() = default;


class MockServer::Server {
    /// Reference to the origin MockServer class instance.
    MockServer &mock;
    /// MicroHTTPD server instance.
    std::unique_ptr<MHD_Daemon, void(*)(MHD_Daemon*)> daemon;
  public:
    /// Initialize server with no daemon running.
    explicit Server(MockServer &mock)
      : mock(mock), daemon(nullptr, &MHD_stop_daemon)
    {}

    ~Server() {
        stop();
    }

    void start(int port) {
        if (daemon) {
            throw std::runtime_error("MockServer has been already started!");
        }

        daemon.reset(MHD_start_daemon(
                MHD_USE_SELECT_INTERNALLY, port, NULL, NULL,
                &static_handlerCallback, this, MHD_OPTION_END));

        if (!daemon) {
            throw std::runtime_error("MockServer has failed to start!");
        }
    }

    void stop() {
        daemon.reset();
    }

    bool isRunning() const {
        return daemon != nullptr;
    }
  private:
    // no copy allowed
    Server(const Server &);
    Server &operator=(const Server &);

    /// Return key-value arguments in the vector
    template <typename OutputType>
    static std::vector<OutputType> getConnectionArgs(
            struct MHD_Connection *connection);

    /**
     * Iterate key-value arguments (initiated by getConnectionArgs()). Store
     * iterated data into output vector container.
     * \param outputContainer  pointer to the std::vector<OutputType> container.
     */
    template <typename OutputType>
    static MHD_RESULT keyValueIterator(
            void *outputContainer, enum MHD_ValueKind,
            const char *key, const char *value);

    /**
     * Callback for MHD_AccessHandlerCallback().
     * \param cls  Server class instance pointer is passed and
     *             cls->handlerCallback() is called passing rest arguments.
     */
    static MHD_RESULT static_handlerCallback(
            void *cls, struct MHD_Connection *connection,
            const char *url, const char *method, const char *version,
            const char *upload_data, size_t *upload_data_size, void **con_cls);

    /**
     * Callback for MHD_AccessHandlerCallback() registred through
     * static_handlerCallback() for specific Server instance.
     * Parses MHD connection data and calls MockServer::responseHandler().
     */
    MHD_RESULT handlerCallback(
            struct MHD_Connection *connection,
            const char *url, const char *method, const char *version,
            const char *upload_data, size_t *upload_data_size, void **con_cls);
};


MockServer::MockServer(int port)
    : server(new MockServer::Server(*this)), port(port)
{}


MockServer::~MockServer() {
    server->stop();
}


void MockServer::start() {
    server->start(port);
}


void MockServer::stop() {
    server->stop();
}


bool MockServer::isRunning() const {
    return server->isRunning();
}


int MockServer::getPort() const {
    return port;
}


template <typename T>
struct KeyValueIteratorTrait {
    static const MHD_ValueKind arg_kind
        = static_cast<MHD_ValueKind>(
          MHD_HEADER_KIND
        | MHD_COOKIE_KIND
        | MHD_POSTDATA_KIND
        | MHD_GET_ARGUMENT_KIND
        | MHD_FOOTER_KIND);
};

/// Specialization for MockServer::UrlArg, we want to iterate GET_ARGUMENTS only.
template <>
struct KeyValueIteratorTrait<MockServer::UrlArg> {
    static const MHD_ValueKind arg_kind = MHD_GET_ARGUMENT_KIND;
};

/// Specialization for MockServer::Header, we want to iterate headers only.
template <>
struct KeyValueIteratorTrait<MockServer::Header> {
    static const MHD_ValueKind arg_kind = MHD_HEADER_KIND;
};


template <typename OutputType>
MHD_RESULT MockServer::Server::keyValueIterator(
        void *cls, enum MHD_ValueKind, const char *key, const char *value) {
    std::vector<OutputType> *collector
        = static_cast<std::vector<OutputType> *>(cls);
    if (!collector) {
        return MHD_NO;
    }
    if (value) {
        collector->push_back(OutputType(key, value));
    } else {
        collector->push_back(OutputType(key));
    }
    return MHD_YES;
}


template <typename OutputType>
std::vector<OutputType> MockServer::Server::getConnectionArgs(
        struct MHD_Connection *connection) {
    if (!connection) {
        return {};
    }
    std::vector<OutputType> args;
    MHD_get_connection_values(
            connection, KeyValueIteratorTrait<OutputType>::arg_kind,
            &keyValueIterator<OutputType>, &args);
    return args;
}


MHD_RESULT MockServer::Server::static_handlerCallback(
        void *cls, struct MHD_Connection *connection,
        const char *url, const char *method, const char *version,
        const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    return ((MockServer::Server*) cls)->handlerCallback(
            connection, url, method, version, upload_data, upload_data_size, con_cls);
}


MHD_RESULT MockServer::Server::handlerCallback(
        MHD_Connection *connection,
        const char *url, const char *method, const char * /*version*/,
        const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    std::string *data = (std::string *) *con_cls;
    // TODO maybe just for method=="POST"?
    if (data == nullptr) {
        data = new std::string();
        *con_cls = data;
        return MHD_YES;
    }

    // read upload data, if any
    if (data && upload_data_size && *upload_data_size) {
        data->append(std::string(upload_data, *upload_data_size));
        *upload_data_size = 0;
        return MHD_YES;
    }

    // swap data from pointer passed among handler calls
    std::string receivedData;
    if (data) {
        receivedData = *data;
        delete data;
    }
    // call mock virtual method
    std::vector<Header> headersReceived = getConnectionArgs<Header>(connection);
    std::vector<UrlArg> urlArguments = getConnectionArgs<UrlArg>(connection);
    // invoke the MockServer::responseHandler() callback
    Response mockResponse = mock.responseHandler(
            url, method, receivedData, urlArguments, headersReceived);
    // prepare server response
    struct MHD_Response *response = MHD_create_response_from_buffer(
            mockResponse.body.size(), (void*) mockResponse.body.data(),
            MHD_RESPMEM_MUST_COPY);
    // set headers response requested to do so
    for (const Header &header: mockResponse.headers) {
        MHD_add_response_header(response, header.key.c_str(), header.value.c_str());
    }
    MHD_RESULT ret = MHD_queue_response(connection, mockResponse.status, response);
    MHD_destroy_response(response);
    return ret;
}


} // namespace httpmock
