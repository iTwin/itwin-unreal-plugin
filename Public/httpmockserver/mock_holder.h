/**
 * \file
 * Provides MockServerHolder class, having interface required by the
 * httpmock::TestEnvironment class. This holder is suitable to pass running
 * instance of a MockServer into the TestEnvironment. This is not possible
 * directly using MockServer itself, as it does not allow copy/move operations.
 */
#pragma once

#include <memory>
#include <stdexcept>
#include <type_traits>
#include "mock_server.h"

namespace httpmock {


class MockServerHolder: public IMockServer {
    /// Instance of a mocking server.
    std::unique_ptr<MockServer> held;
  public:
    MockServerHolder(std::unique_ptr<MockServer> &&server)
      : held(std::move(server))
    {
        // valid instance is a must
        if (! held) {
            throw std::runtime_error("A valid mock server instance required!");
        }
    }

    MockServerHolder &operator=(const MockServerHolder &) = default;
    MockServerHolder(const MockServerHolder &) = default;
    MockServerHolder(MockServerHolder &&) = default;
    ~MockServerHolder() = default;

    virtual void start() override {
        held->start();
    }

    virtual void stop() override {
        held->stop();
    }

    virtual bool isRunning() const override {
        return held->isRunning();
    }

    /**
     * MockServer pointer access method.
     * Beware, validity is limited by the holder lifetime.
     */
    const std::unique_ptr<MockServer> &operator->() const {
        return held;
    }
};


}
