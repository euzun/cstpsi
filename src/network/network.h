// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef CSTPSI_NETWORK
#define CSTPSI_NETWORK

#include "serialization.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// Forward declaration - CVector2D is defined in params.h
typedef std::vector<std::vector<seal::Ciphertext>> CVector2D;

namespace cstpsi {

// Protocol version for compatibility checking
const uint8_t PROTOCOL_VERSION = 1;

/**
 * @enum MessageType
 * @brief Network message types for CSTPSI protocol
 */
enum class MessageType : uint8_t {
    QUERY_REQUEST   = 0x01,   // Receiver -> Sender: encrypted query powers
    QUERY_RESPONSE  = 0x02,   // Sender -> Receiver: encrypted results
    SESSION_SETUP   = 0x05,   // Receiver -> Sender: query item count
    ERROR_RESPONSE  = 0xFF    // Sender -> Receiver: error condition
};

/**
 * @enum ErrorCode
 * @brief Error codes for ERROR_RESPONSE messages
 */
enum class ErrorCode : uint32_t {
    SUCCESS                     = 0,
    INVALID_MESSAGE             = 1,
    DESERIALIZATION_ERROR       = 2,
    EVALUATION_ERROR            = 3,
    PARAMETER_MISMATCH          = 4,
    PROTOCOL_VERSION_MISMATCH   = 5
};

/**
 * Exception class for network-related errors
 */
class NetworkException : public std::exception {
public:
    explicit NetworkException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
private:
    std::string message_;
};

/**
 * Exception for network timeout conditions
 */
class NetworkTimeoutException : public NetworkException {
public:
    explicit NetworkTimeoutException(const std::string& message)
        : NetworkException("Network timeout: " + message) {}
};

/**
 * @class NetworkServer
 * @brief Server-side network interface (sender role)
 */
class NetworkServer {
public:
    NetworkServer(int port = 1212, const std::string& bind_addr = "0.0.0.0");
    ~NetworkServer();

    int receiveSessionSetup();

    // Carries nrof_real_partitions so the receiver can drop phantom SIMD-padding
    // rows before matching (default -1 = unknown/legacy).
    void sendSessionAck(int nrof_real_partitions = -1);

    std::vector<seal::Ciphertext> receiveQuery(
        std::shared_ptr<seal::SEALContext> context
    );

    void sendResponse(const CVector2D& encrypted_result);

    void sendError(ErrorCode code, const std::string& message);

    bool isRunning() const { return running_; }

    uint64_t getSentBytes() const { return bytes_sent_; }
    uint64_t getReceivedBytes() const { return bytes_received_; }
    void resetByteCounters() { bytes_sent_ = 0; bytes_received_ = 0; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    int port_;
    std::string bind_addr_;
    bool running_;
    uint64_t bytes_sent_ = 0;
    uint64_t bytes_received_ = 0;

    std::string receiveMessage();
    void sendMessage(const std::string& data);
};

/**
 * @class NetworkClient
 * @brief Client-side network interface (receiver role)
 */
class NetworkClient {
public:
    NetworkClient(
        const std::string& server_addr,
        int port = 1212,
        int timeout_ms = 30000
    );

    ~NetworkClient();

    void sendSessionSetup(int query_item_count);

    // Returns nrof_real_partitions from the server (or -1 if not provided).
    int receiveSessionAck();

    void sendQuery(const std::vector<seal::Ciphertext>& encrypted_query);

    CVector2D receiveResponse(
        std::shared_ptr<seal::SEALContext> context
    );

    bool isConnected() const { return connected_; }

    void reconnect();

    void setTimeout(int timeout_ms);

    uint64_t getSentBytes() const { return bytes_sent_; }
    uint64_t getReceivedBytes() const { return bytes_received_; }
    void resetByteCounters() { bytes_sent_ = 0; bytes_received_ = 0; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::string server_addr_;
    int port_;
    int timeout_ms_;
    bool connected_;
    uint64_t bytes_sent_ = 0;
    uint64_t bytes_received_ = 0;

    std::string receiveMessage();
    void sendMessage(const std::string& data);
    void handleErrorResponse(const std::string& data);
};

} // namespace cstpsi

#endif // CSTPSI_NETWORK
