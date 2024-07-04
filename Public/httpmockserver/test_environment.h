/**
 * \file
 * Test environment extensions for GTests.
 *
 * Linker dependencies: -lgtest
 */

#pragma once
#include <gtest/gtest.h>
#include <type_traits>
#include "mock_server.h"
#include "mock_holder.h"
#include "port_searcher.h"


namespace httpmock {


template <class HTTPMock>
class TestEnvironment: public ::testing::Environment {
    HTTPMock httpServer;
  public:
    TestEnvironment()
      : httpServer()
    {
        static_assert(std::is_base_of<MockServer, HTTPMock>::value,
                      "An instance derived from httpmock::MockServer required!");
    }

    TestEnvironment(HTTPMock &&mock)
      : httpServer(std::move(mock))
    {
        // We want an interface of the IMockServer to be fulfilled.
        static_assert(std::is_base_of<IMockServer, HTTPMock>::value,
                      "An instance derived from httpmock::IMockServer required!");
    }

    virtual ~TestEnvironment() {}

    virtual void SetUp() {
        if (!httpServer.isRunning()) {
            httpServer.start();
        }
    }

    virtual void TearDown() {
        httpServer.stop();
    }

    const HTTPMock &getMock() const {
        return httpServer;
    }
};


/**
 * Return new environment with running MockServer heir.
 * Searches for first free port in range <startPort, startPort + tryCount>.
 * Caller is responsible for destruction of returned resources.
 */
template <typename HTTPMock>
::testing::Environment *createMockServerEnvironment(
        unsigned startPort = 8080, unsigned tryCount = 1000)
{
    return new TestEnvironment<MockServerHolder>(
            getFirstRunningMockServer<HTTPMock>(startPort, tryCount));
}



} // namespace httpmock
