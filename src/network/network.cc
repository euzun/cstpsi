// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "network.h"
#include <stdexcept>
#include <cstring>

// Note: ZeroMQ headers may be zmq.h or zmq.hpp depending on installation
#if __has_include(<zmq.hpp>)
#include <zmq.hpp>
#elif __has_include(<zmq.h>)
#include <zmq.h>
#else
#error "ZeroMQ headers not found. Install libzmq3-dev (Ubuntu) or zeromq (macOS)"
#endif

namespace cstpsi {

// Helper: Write little-endian uint32_t
static void writeUint32LE(uint32_t value, std::string& buffer) {
    unsigned char bytes[4];
    bytes[0] = static_cast<unsigned char>(value & 0xFF);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
    buffer.append(reinterpret_cast<const char*>(bytes), 4);
}

// Helper: Read little-endian uint32_t
static uint32_t readUint32LE(const std::string& buffer, size_t offset) {
    if (offset + 4 > buffer.size()) {
        throw std::runtime_error("Buffer underflow reading uint32_t");
    }
    uint32_t value = 0;
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(buffer.data() + offset);
    value = (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
    return value;
}

// Helper: Write protocol message with framing
// Format: [version:1][type:1][length:4][payload]
static std::string buildMessage(MessageType type, const std::string& payload) {
    std::string message;

    message.push_back(static_cast<char>(PROTOCOL_VERSION));
    message.push_back(static_cast<char>(type));

    uint32_t length = static_cast<uint32_t>(payload.size());
    writeUint32LE(length, message);

    message.append(payload);

    return message;
}

// Helper: Parse protocol message
static bool parseMessage(
    const std::string& message,
    uint8_t& version,
    MessageType& type,
    std::string& payload
) {
    if (message.size() < 6) {
        throw NetworkException("Message too short");
    }

    size_t offset = 0;

    version = static_cast<uint8_t>(message[offset]);
    offset++;

    if (version != PROTOCOL_VERSION) {
        throw NetworkException("Protocol version mismatch");
    }

    type = static_cast<MessageType>(message[offset]);
    offset++;

    uint32_t length = readUint32LE(message, offset);
    offset += 4;

    if (offset + length != message.size()) {
        throw NetworkException("Message length mismatch");
    }

    payload = message.substr(offset);
    return true;
}

// ============================================================================
// NetworkServer Implementation
// ============================================================================

class NetworkServer::Impl {
public:
    zmq::context_t zmq_context;
    std::unique_ptr<zmq::socket_t> socket;
};

NetworkServer::NetworkServer(int port, const std::string& bind_addr)
    : impl_(std::make_unique<Impl>()),
      port_(port),
      bind_addr_(bind_addr),
      running_(false) {
    try {
        impl_->socket = std::make_unique<zmq::socket_t>(impl_->zmq_context, zmq::socket_type::rep);

        impl_->socket->set(zmq::sockopt::rcvtimeo, 1000);

        std::string endpoint = "tcp://" + bind_addr + ":" + std::to_string(port);
        impl_->socket->bind(endpoint);

        running_ = true;
    } catch (const zmq::error_t& e) {
        running_ = false;
        throw NetworkException(
            std::string("Failed to start server on ") + bind_addr + ":" +
            std::to_string(port) + " - " + e.what()
        );
    } catch (const std::exception& e) {
        running_ = false;
        throw NetworkException(std::string("Server initialization error: ") + e.what());
    }
}

NetworkServer::~NetworkServer() {
    try {
        if (impl_ && impl_->socket) {
            impl_->socket->close();
        }
    } catch (...) {
        // Ignore errors during cleanup
    }
    running_ = false;
}

std::string NetworkServer::receiveMessage() {
    if (!impl_ || !impl_->socket) {
        throw NetworkException("Server socket not initialized");
    }

    try {
        zmq::message_t msg;
        auto recv_result = impl_->socket->recv(msg, zmq::recv_flags::none);
        if (!recv_result) {
            // recv() returning nullopt means the socket's short rcvtimeo expired
            // (set so the idle accept loop can poll stop_flag). Surface it as a
            // timeout -- distinct from a genuine receive error -- so in-session
            // callers can keep waiting for a slow/quiet receiver instead of
            // treating a normal inter-phase gap as fatal.
            throw NetworkTimeoutException("recv timed out (no message)");
        }
        bytes_received_ += msg.size();
        return std::string(static_cast<char*>(msg.data()), msg.size());
    } catch (const zmq::error_t& e) {
        throw NetworkException(std::string("Receive error: ") + e.what());
    }
}

void NetworkServer::sendMessage(const std::string& data) {
    if (!impl_ || !impl_->socket) {
        throw NetworkException("Server socket not initialized");
    }

    try {
        impl_->socket->send(zmq::buffer(data), zmq::send_flags::none);
        bytes_sent_ += data.size();
    } catch (const zmq::error_t& e) {
        throw NetworkException(std::string("Send error: ") + e.what());
    }
}

std::vector<seal::Ciphertext> NetworkServer::receiveQuery(
    std::shared_ptr<seal::SEALContext> context) {
    try {
        std::string raw_msg = receiveMessage();

        uint8_t version;
        MessageType type;
        std::string payload;
        parseMessage(raw_msg, version, type, payload);

        if (type != MessageType::QUERY_REQUEST) {
            throw NetworkException(
                "Expected QUERY_REQUEST, got " + std::to_string(static_cast<int>(type))
            );
        }

        try {
            return SealSerializer::deserializeCiphertextVector(payload, context);
        } catch (const std::exception& e) {
            std::string error_msg = std::string("Deserialization failed: ") + e.what();
            sendError(ErrorCode::DESERIALIZATION_ERROR, error_msg);
            throw NetworkException(error_msg);
        }
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("receiveQuery error: ") + e.what());
    }
}

void NetworkServer::sendResponse(const CVector2D& encrypted_result) {
    try {
        std::string payload = SealSerializer::serializeCVector2D(encrypted_result);
        std::string message = buildMessage(MessageType::QUERY_RESPONSE, payload);
        sendMessage(message);
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("sendResponse error: ") + e.what());
    }
}

void NetworkServer::sendError(ErrorCode code, const std::string& message) {
    try {
        std::string payload;

        writeUint32LE(static_cast<uint32_t>(code), payload);

        uint32_t msg_len = static_cast<uint32_t>(message.size());
        writeUint32LE(msg_len, payload);

        payload.append(message);

        std::string response = buildMessage(MessageType::ERROR_RESPONSE, payload);
        sendMessage(response);
    } catch (const std::exception& e) {
        std::cerr << "Failed to send error response: " << e.what() << std::endl;
    }
}

// ============================================================================
// NetworkClient Implementation
// ============================================================================

class NetworkClient::Impl {
public:
    zmq::context_t zmq_context;
    std::unique_ptr<zmq::socket_t> socket;
};

NetworkClient::NetworkClient(
    const std::string& server_addr,
    int port,
    int timeout_ms)
    : impl_(std::make_unique<Impl>()),
      server_addr_(server_addr),
      port_(port),
      timeout_ms_(timeout_ms),
      connected_(false) {
    try {
        impl_->socket = std::make_unique<zmq::socket_t>(impl_->zmq_context, zmq::socket_type::req);

        impl_->socket->set(zmq::sockopt::rcvtimeo, timeout_ms);
        impl_->socket->set(zmq::sockopt::sndtimeo, timeout_ms);

        std::string endpoint = "tcp://" + server_addr + ":" + std::to_string(port);
        impl_->socket->connect(endpoint);

        connected_ = true;
    } catch (const zmq::error_t& e) {
        connected_ = false;
        throw NetworkException(
            std::string("Failed to connect to server ") + server_addr + ":" +
            std::to_string(port) + " - " + e.what()
        );
    } catch (const std::exception& e) {
        connected_ = false;
        throw NetworkException(std::string("Client initialization error: ") + e.what());
    }
}

NetworkClient::~NetworkClient() {
    try {
        if (impl_ && impl_->socket) {
            impl_->socket->close();
        }
    } catch (...) {
        // Ignore errors during cleanup
    }
    connected_ = false;
}

std::string NetworkClient::receiveMessage() {
    if (!impl_ || !impl_->socket) {
        throw NetworkException("Client socket not initialized");
    }

    try {
        zmq::message_t msg;
        auto result = impl_->socket->recv(msg, zmq::recv_flags::none);
        if (!result) {
            throw NetworkTimeoutException("No response from server");
        }
        bytes_received_ += msg.size();
        return std::string(static_cast<char*>(msg.data()), msg.size());
    } catch (const zmq::error_t& e) {
        if (e.num() == EAGAIN) {
            throw NetworkTimeoutException("Operation timed out");
        }
        throw NetworkException(std::string("Receive error: ") + e.what());
    }
}

void NetworkClient::sendMessage(const std::string& data) {
    if (!impl_ || !impl_->socket) {
        throw NetworkException("Client socket not initialized");
    }

    try {
        auto result = impl_->socket->send(zmq::buffer(data), zmq::send_flags::none);
        if (!result) {
            throw NetworkException("Send failed");
        }
        bytes_sent_ += data.size();
    } catch (const zmq::error_t& e) {
        if (e.num() == EAGAIN) {
            throw NetworkTimeoutException("Send timed out");
        }
        throw NetworkException(std::string("Send error: ") + e.what());
    }
}

void NetworkClient::sendQuery(const std::vector<seal::Ciphertext>& encrypted_query) {
    try {
        std::string payload = SealSerializer::serializeCiphertextVector(encrypted_query);
        std::string message = buildMessage(MessageType::QUERY_REQUEST, payload);
        sendMessage(message);
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("sendQuery error: ") + e.what());
    }
}

CVector2D NetworkClient::receiveResponse(
    std::shared_ptr<seal::SEALContext> context) {
    try {
        std::string raw_msg = receiveMessage();

        uint8_t version;
        MessageType type;
        std::string payload;
        parseMessage(raw_msg, version, type, payload);

        if (type == MessageType::ERROR_RESPONSE) {
            handleErrorResponse(payload);
            throw NetworkException("Unknown error from server");
        }

        if (type != MessageType::QUERY_RESPONSE) {
            throw NetworkException(
                "Expected QUERY_RESPONSE, got " + std::to_string(static_cast<int>(type))
            );
        }

        return SealSerializer::deserializeCVector2D(payload, context);
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("receiveResponse error: ") + e.what());
    }
}

void NetworkClient::handleErrorResponse(const std::string& data) {
    try {
        if (data.size() < 8) {
            throw NetworkException("Invalid error response format");
        }

        size_t offset = 0;

        uint32_t code = readUint32LE(data, offset);
        offset += 4;

        uint32_t msg_len = readUint32LE(data, offset);
        offset += 4;

        if (offset + msg_len != data.size()) {
            throw NetworkException("Error message length mismatch");
        }

        std::string error_msg = data.substr(offset, msg_len);

        throw NetworkException(
            "Server error (" + std::to_string(code) + "): " + error_msg
        );
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("Error parsing error response: ") + e.what());
    }
}

void NetworkClient::reconnect() {
    try {
        if (impl_ && impl_->socket) {
            impl_->socket->close();
        }

        impl_->socket = std::make_unique<zmq::socket_t>(impl_->zmq_context, zmq::socket_type::req);
        impl_->socket->set(zmq::sockopt::rcvtimeo, timeout_ms_);
        impl_->socket->set(zmq::sockopt::sndtimeo, timeout_ms_);

        std::string endpoint = "tcp://" + server_addr_ + ":" + std::to_string(port_);
        impl_->socket->connect(endpoint);

        connected_ = true;
    } catch (const zmq::error_t& e) {
        connected_ = false;
        throw NetworkException(std::string("Reconnect failed: ") + e.what());
    } catch (const std::exception& e) {
        connected_ = false;
        throw NetworkException(std::string("Reconnect error: ") + e.what());
    }
}

void NetworkClient::setTimeout(int timeout_ms) {
    timeout_ms_ = timeout_ms;
    if (impl_ && impl_->socket) {
        try {
            impl_->socket->set(zmq::sockopt::rcvtimeo, timeout_ms);
            impl_->socket->set(zmq::sockopt::sndtimeo, timeout_ms);
        } catch (const std::exception& e) {
            throw NetworkException(std::string("Failed to set timeout: ") + e.what());
        }
    }
}

void NetworkClient::sendSessionSetup(int query_item_count) {
    try {
        std::string payload;
        writeUint32LE(static_cast<uint32_t>(query_item_count), payload);
        std::string message = buildMessage(MessageType::SESSION_SETUP, payload);
        sendMessage(message);
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("sendSessionSetup error: ") + e.what());
    }
}

int NetworkServer::receiveSessionSetup() {
    try {
        std::string raw_msg = receiveMessage();

        uint8_t version;
        MessageType type;
        std::string payload;
        parseMessage(raw_msg, version, type, payload);

        if (type != MessageType::SESSION_SETUP) {
            throw NetworkException(
                "Expected SESSION_SETUP, got " + std::to_string(static_cast<int>(type))
            );
        }

        if (payload.size() < 4) {
            throw NetworkException("SESSION_SETUP payload too short");
        }

        uint32_t query_item_count = readUint32LE(payload, 0);
        return static_cast<int>(query_item_count);
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("receiveSessionSetup error: ") + e.what());
    }
}

void NetworkServer::sendSessionAck(int nrof_real_partitions) {
    try {
        std::string payload;
        writeUint32LE(static_cast<uint32_t>(nrof_real_partitions), payload);
        std::string message = buildMessage(MessageType::SESSION_SETUP, payload);
        sendMessage(message);
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("sendSessionAck error: ") + e.what());
    }
}

int NetworkClient::receiveSessionAck() {
    try {
        std::string raw_msg = receiveMessage();
        uint8_t version;
        MessageType type;
        std::string payload;
        parseMessage(raw_msg, version, type, payload);
        if (type != MessageType::SESSION_SETUP) {
            throw NetworkException(
                "Expected SESSION_SETUP ack, got " +
                std::to_string(static_cast<int>(type))
            );
        }
        // Legacy server may send an empty ack -> -1 (no slicing).
        if (payload.size() < 4) return -1;
        return static_cast<int>(readUint32LE(payload, 0));
    } catch (const NetworkException&) {
        throw;
    } catch (const std::exception& e) {
        throw NetworkException(std::string("receiveSessionAck error: ") + e.what());
    }
}

} // namespace cstpsi
