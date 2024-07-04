# C++ HTTP mock server

Library for easier HTTP clients testing using simply defined HTTP mock server.

## Dependencies

  * [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) (package `libmicrohttpd-dev` on debian based systems).
  * [cpr](https://github.com/whoshuu/cpr) (tests only, downloaded as git submodules by default).
  * [gtest](https://github.com/google/googletest) (tests only, downloaded as git submodules by default).
  * CMake build system.
  * C++11 compiler (G++ 4.9 tested).

### CMake options

CPR and GTest libraries are by default downloaded as git submodules, but it can be used with system-wide libraries
when following cmake options are used:

  * `-DUSE_SYSTEM_CPR=ON` - Use libcpr from system, do not build it from submodule. Default OFF.
    * `-DUSE_SYSTEM_CURL=ON` - Use libcurl for libcpr from system, do not build it on its own. Default OFF.
  * `-DUSE_SYSTEM_GTEST=ON` - Use Google Test from system, do not build it from submodule. Default OFF.
  * `-DUSE_ALL_SYSTEM_LIBS=ON` - Use both libcpr and GTest libraries from system paths. Default OFF.

## Build

If you are not using CPR and GTest from system, it is necessary to initialize git submodules:

```sh
git submodule update --init --recursive
```

Run following commands to build the library:

```sh
mkdir build && cd build
cmake ..
make
```

Custom installation directory can be specified using `cmake -DCMAKE_INSTALL_PREFIX:PATH=some/path` option.
By default the `/usr/local/` will be used on most systems.
Installation of the library and headers into defined prefix path is possible with `make install` command.

## How to use it

Here is the quick sample how to use mock server in GTests:

```c++
#include <gtest/gtest.h>
#include <string>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/test_environment.h>


class HTTPMock: public httpmock::MockServer {
  public:
    /// Create HTTP server on port 9200
    explicit HTTPMock(int port = 9200): MockServer(port) {}
  private:

    /// Handler called by MockServer on HTTP request.
    Response responseHandler(
            const std::string &url,
            const std::string &method,
            const std::string &data,
            const std::vector<UrlArg> &urlArguments,
            const std::vector<Header> &headers)
    {
        if (method == "POST" && matchesPrefix(url, "/example")) {
            // Do something and return response
            return Response(500, "Fake HTTP response");
        }
        // Return "URI not found" for the undefined methods
        return Response(404, "Not Found");
    }

    /// Return true if \p url starts with \p str.
    bool matchesPrefix(const std::string &url, const std::string &str) const {
        return url.substr(0, str.size()) == str;
    }
};


TEST(MyTest, dummyTest) {
    // Here should be implementation of test case using HTTP server.
    // HTTP requests are processed by HTTPMock::responseHandler(...)
    // I. e.: when HTTP POST request is sent on localhost:9200/example, then
    // response with status code 500 and body "Fake HTTP response" is returned.
}


int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new httpmock::TestEnvironment<HTTPMock>());
    return RUN_ALL_TESTS();
}
```

If you do not want to rely on hardcoded port number, you can use port searching
class. Searching for unused port is done in 1000 iterations by default.

```c++
// ... class HTTPMock is same as above

/// Server started in the main().
static httpmock::TestEnvironment<httpmock::MockServerHolder>* mock_server_env = nullptr;

TEST(MyTest, dummyTest) {
    assert(nullptr != mock_server_env);
    const int port = mock_server_env->getMock()->getPort();
    // Here should be implementation of test case using HTTP server.
    // HTTP requests are processed by HTTPMock::responseHandler(...)
    // I. e.: when HTTP POST request is sent on localhost:$port/example, then
    // response with status code 500 and body "Fake HTTP response" is returned.
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    // startup the server for the tests
    ::testing::Environment * const env = ::testing::AddGlobalTestEnvironment(
            httpmock::createMockServerEnvironment<HTTPMock>(9200));
    // set global env pointer
    mock_server_env
        = dynamic_cast<httpmock::TestEnvironment<httpmock::MockServerHolder> *>(env);
    return RUN_ALL_TESTS();
}
```

# License

Library is licensed under the MIT License.
