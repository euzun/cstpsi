# CSTPSI Network Protocol Documentation

**Phase 4.2 - Network Protocol Implementation**

This document describes the network protocol for separating CSTPSI into sender (server) and receiver (client) binaries communicating over TCP using ZeroMQ.

## Overview

The CSTPSI network protocol splits the in-process protocol into two communicating binaries:

- **Sender (cstpsi_sender)**: Server that loads enrollment database, performs offline preprocessing, and responds to encrypted queries
- **Receiver (cstpsi_receiver)**: Client that loads query data, encrypts queries, sends them to sender, and decrypts results

## Architecture

```
┌──────────────────────────┐              ┌──────────────────────────┐
│   SENDER (Server)        │              │   RECEIVER (Client)      │
│   cstpsi_sender          │              │   cstpsi_receiver        │
├──────────────────────────┤              ├──────────────────────────┤
│                          │              │                          │
│ 1. Load enrollment DB    │              │ 1. Load query data       │
│ 2. Subsample (local)     │              │ 2. Subsample (local)     │
│ 3. Partition DB          │              │ 3. SEAL setup            │
│ 4. Compute coefficients  │              │                          │
│ 5. SEAL encode           │              │                          │
│ 6. Listen on port 1212   │◄────────────►│ 4. Connect to sender     │
│                          │   ZeroMQ     │                          │
│ [Query Loop]             │   TCP/IP     │ [Query Loop]             │
│ 7. Receive encrypted     │              │ 5. Encrypt query powers  │
│    query powers          │              │ 6. Send to sender        │
│ 8. Homomorphic eval      │              │ 7. Receive encrypted     │
│ 9. Send encrypted result │              │    result                │
│                          │              │ 8. Decrypt result        │
│                          │              │ 9. Find matches          │
└──────────────────────────┘              └──────────────────────────┘
           ▲                                         ▲
           │                                         │
           └─────────── Both use SAME ───────────────┘
                     parameters/config.json
```

## Message Protocol

### Transport Layer

**Technology**: ZeroMQ (libzmq + cppzmq)
- **Pattern**: REQ/REP (Request-Reply)
- **Binding**: TCP socket
- **Default Port**: 1212

### Message Types

```cpp
enum class MessageType : uint8_t {
    QUERY_REQUEST  = 0x01,   // Receiver → Sender: encrypted query powers
    QUERY_RESPONSE = 0x02,   // Sender → Receiver: encrypted results
    ERROR_RESPONSE = 0xFF    // Sender → Receiver: error condition
};
```

### Wire Format

**General Message Structure:**
```
┌─────────────┬─────────────┬─────────────┬─────────────┐
│ Version     │ Message Type│ Length      │ Payload     │
│ 1 byte      │ 1 byte      │ 4 bytes     │ N bytes     │
│             │             │ (uint32_t)  │             │
└─────────────┴─────────────┴─────────────┴─────────────┘
```

All multi-byte integers use **little-endian** byte order.

**QUERY_REQUEST Payload:**
```
┌─────────────┬──────────────────────────────────┐
│ Num Ctexts  │ Ciphertext Array                 │
│ 4 bytes     │ (count × ciphertext)             │
│ (uint32_t)  │                                  │
└─────────────┴──────────────────────────────────┘

Ciphertext Array Entry:
┌─────────────┬─────────────┐
│ Ctxt Size   │ Serialized  │
│ 4 bytes     │ SEAL Data   │
│ (uint32_t)  │ N bytes     │
└─────────────┴─────────────┘
```

**QUERY_RESPONSE Payload:**
```
┌─────────────┬─────────────┬──────────────────────────┐
│ Dim1 Size   │ Dim2 Size   │ 2D Ciphertext Array      │
│ 4 bytes     │ 4 bytes     │ (dim1 × dim2 × ctxt)     │
│ (uint32_t)  │ (uint32_t)  │                          │
└─────────────┴─────────────┴──────────────────────────┘

Each array entry: [size (4B)][serialized_ciphertext]
```

**ERROR_RESPONSE Payload:**
```
┌─────────────┬─────────────┬─────────────┐
│ Error Code  │ Msg Length  │ Error Msg   │
│ 4 bytes     │ 4 bytes     │ N bytes     │
│ (uint32_t)  │ (uint32_t)  │ (UTF-8)     │
└─────────────┴─────────────┴─────────────┘
```

## SEAL Serialization

Uses SEAL 3.5 native serialization (save/load) for compact binary format.

### Serialization Strategy

**For vector<Ciphertext>:**
```cpp
std::stringstream buffer;
uint32_t count = ciphertexts.size();
buffer.write(reinterpret_cast<const char*>(&count), 4);

for (const auto& ctxt : ciphertexts) {
    std::stringstream ctxt_stream;
    ctxt.save(ctxt_stream);
    std::string ctxt_data = ctxt_stream.str();
    uint32_t ctxt_size = ctxt_data.size();

    buffer.write(reinterpret_cast<const char*>(&ctxt_size), 4);
    buffer.write(ctxt_data.data(), ctxt_size);
}

return buffer.str();
```

**For CVector2D (2D array):**
```cpp
std::stringstream buffer;
uint32_t dim1 = cvector.size();
uint32_t dim2 = cvector[0].size();
buffer.write(reinterpret_cast<const char*>(&dim1), 4);
buffer.write(reinterpret_cast<const char*>(&dim2), 4);

for (size_t i = 0; i < dim1; i++) {
    for (size_t j = 0; j < dim2; j++) {
        std::stringstream ctxt_stream;
        cvector[i][j].save(ctxt_stream);
        std::string ctxt_data = ctxt_stream.str();
        uint32_t ctxt_size = ctxt_data.size();

        buffer.write(reinterpret_cast<const char*>(&ctxt_size), 4);
        buffer.write(ctxt_data.data(), ctxt_size);
    }
}

return buffer.str();
```

## Protocol Flow

### Normal Query

```
Receiver                          Sender
   │                                │
   ├─── QUERY_REQUEST ──────────►  │
   │    (encrypted query powers)    │
   │                                │
   │                                ├─ Deserialize
   │                                ├─ HE Evaluation
   │                                ├─ Serialize
   │                                │
   │  ◄───── QUERY_RESPONSE ───────┤
   │    (encrypted results)         │
   │                                │
```

### Error Case

```
Receiver                          Sender
   │                                │
   ├─── QUERY_REQUEST ──────────►  │
   │    (malformed/invalid)         │
   │                                │
   │                                ├─ Detect error
   │                                ├─ Prepare error msg
   │                                │
   │  ◄───── ERROR_RESPONSE ───────┤
   │    (error code + message)      │
   │                                │
   ├─ Log error, retry or exit     │
```

## Command-Line Interfaces

### cstpsi_sender

**Usage:**
```bash
cstpsi_sender --dbFile <path> --paramsFile <path> [--port 1212] [--bind 0.0.0.0] [--verbose]
```

**Options:**
- `--dbFile <path>`: Enrollment database file (CSV format) - **required**
- `--paramsFile <path>`: Parameter configuration JSON - **required**
- `--port <port>`: TCP port to listen on (default: 1212)
- `--bind <addr>`: Network interface to bind to (default: 0.0.0.0)
- `--verbose`: Verbose output
- `--help`: Show help message

**Example:**
```bash
cstpsi_sender --dbFile data/enrollment.csv \
              --paramsFile parameters/demo.json \
              --port 1212 \
              --verbose
```

**Output:**
```
=== CSTPSI Sender (Network Server) ===

Loading parameters from: parameters/demo.json
[Parameter summary...]

=== CSTPSI Parameters ===
N (database partitions): 64
ML (maximum label count): 8
hash_size: 16
m (SIMD): 2048
poly_modulus_degree: 4096
plain_modulus: 8519681
partition_size: 100
=========================

Loading enrollment database from: data/enrollment.csv
  Loaded 1595 enrollment records

[1/3] Plaintext subsampling...
  Time: 45 ms
[2/3] Database preprocessing...
  Time: 234 ms
[3/3] SEAL context and coefficient encoding...
  Time: 156 ms

Preprocessing complete. Total time: 435 ms

Starting network server...
Sender listening on 0.0.0.0:1212
Ready to accept queries. Press Ctrl+C to exit.

[Query 0] Received 64 ciphertexts
[Query 0] Result: 2 x 64 ciphertexts, evaluation time: 45 ms
[Query 1] Received 64 ciphertexts
[Query 1] Result: 2 x 64 ciphertexts, evaluation time: 44 ms
...

Shutdown complete. Processed 1000 queries.
```

### cstpsi_receiver

**Usage:**
```bash
cstpsi_receiver --queryFile <path> --paramsFile <path> --senderAddr <host> \
                --outputFile <path> [--port 1212] [--timeout 30000] [--verbose]
```

**Options:**
- `--queryFile <path>`: Query database file (CSV format) - **required**
- `--paramsFile <path>`: Parameter configuration JSON - **required**
- `--senderAddr <host>`: Sender hostname or IP address - **required**
- `--outputFile <path>`: Output results file (CSV format) - **required**
- `--port <port>`: TCP port to connect to (default: 1212)
- `--timeout <ms>`: Operation timeout in milliseconds (default: 30000)
- `--verbose`: Verbose output
- `--help`: Show help message

**Example:**
```bash
cstpsi_receiver --queryFile data/queries.csv \
                --paramsFile parameters/demo.json \
                --senderAddr localhost \
                --outputFile results.csv \
                --port 1212 \
                --verbose
```

**Output:**
```
=== CSTPSI Receiver (Network Client) ===

Loading parameters from: parameters/demo.json
[Parameter summary...]

=== CSTPSI Parameters ===
N (database partitions): 64
ML (maximum label count): 8
hash_size: 16
m (SIMD): 2048
poly_modulus_degree: 4096
plain_modulus: 8519681
partition_size: 100
=========================

Loading query database from: data/queries.csv
  Loaded 1000 query records

[Local] Setting up SEAL context...
  Time: 156 ms
[Local] Subsampling queries...
  Subsampled 1000 queries

Connecting to sender at localhost:1212
Connected to sender. Processing queries...

Processing query 0/1000
  Query 0: 1 matches (network: 52 ms)
  Query 1: 1 matches (network: 50 ms)
Processing query 100/1000
  Query 100: 2 matches (network: 51 ms)
...
Query processing complete.
Total time: 51234 ms
Average per query: 51 ms

Writing results to results.csv
Done.
```

## Demo Scripts

### Automated Network Demo

**File:** `demo/demo_network.sh`

Fully automated end-to-end demo that:
1. Generates test data if needed
2. Starts sender in background
3. Waits for sender initialization
4. Runs receiver
5. Stops sender
6. Reports results

**Usage:**
```bash
cd demo
./demo_network.sh
```

### Manual Sender/Receiver

**Files:** `demo/run_sender.sh`, `demo/run_receiver.sh`

For manual testing in separate terminals:

**Terminal 1:**
```bash
cd demo
./run_sender.sh
```

**Terminal 2:**
```bash
cd demo
./run_receiver.sh
```

### Split-Screen Demo

**File:** `demo/demo_split_screen.sh`

Opens sender and receiver in side-by-side tmux or iTerm2 panes:

```bash
cd demo
./demo_split_screen.sh
```

### Network Overhead

The automated network demo reports the per-cell online time and total
communication for a split sender/receiver run:

```bash
bash demo/demo_network.sh
```

```
[3/3] Result
  mode=CSTPSI  D=1k  T=2  threads=4
  online=... ms   comm=... kB
  FRR=0.0000  (5/5 true-positive queries recovered the correct label)
  FAR=0.000000 (0/5 false accepts among true-negatives)
```

Compare against the single-binary in-process path (`bash demo/demo_small.sh`)
on the same configuration; the network split adds only transport overhead. The
paper's overhead numbers come from the reproduction harness under
`experiments/` (see `experiments/reproduce.sh`).

## Security Considerations

### Tier 1 Limitations

⚠️ **WARNING: DO NOT USE OVER UNTRUSTED NETWORKS**

Tier 1 implementation has NO network security:
- ❌ No encryption (TCP traffic in cleartext)
- ❌ No authentication (anyone can connect)
- ❌ No rate limiting (vulnerable to DoS)
- ❌ No integrity checking (data can be modified in transit)

**Acceptable Use:**
- ✅ Localhost only (127.0.0.1)
- ✅ Trusted LAN with physical security
- ✅ Development and testing environments

**Prohibited Use:**
- ❌ Internet/WAN deployment
- ❌ Untrusted networks (public WiFi, etc.)
- ❌ Production systems with sensitive data

### Parameter Synchronization

**CRITICAL**: Sender and receiver MUST use identical parameters.

If parameters differ:
- Results are undefined/garbage
- May cause crashes or exceptions
- No privacy guarantees

**Mitigation**: Use same JSON configuration file on both sides.

## Error Handling

### Error Codes

```cpp
enum class ErrorCode : uint32_t {
    SUCCESS                     = 0,
    INVALID_MESSAGE             = 1,
    DESERIALIZATION_ERROR       = 2,
    EVALUATION_ERROR            = 3,
    PARAMETER_MISMATCH          = 4,
    PROTOCOL_VERSION_MISMATCH   = 5
};
```

### Receiver Retry Logic

On network timeout, receiver implements exponential backoff:
- Attempt 1: Wait 1 second, retry
- Attempt 2: Wait 2 seconds, retry
- Attempt 3: Wait 4 seconds, retry
- Failure: Abort with error message

Max retries: 3 (configurable)

### Socket Cleanup

If sender crashes, TCP port may remain bound:

**Check for stale process:**
```bash
lsof -i :1212
kill <PID>
```

**Enable socket reuse:**
- Automatic in ZeroMQ with linger=0

**Wait for TIME_WAIT:**
- TCP sockets enter TIME_WAIT state (2-4 minutes)

## Performance Characteristics

### Expected Message Sizes

- Single Ciphertext (BFV, poly_degree=4096): ~64-128 KB
- Query Request (N=64 ciphertexts): ~4-8 MB
- Query Response (varies by partitions): ~10-20 MB

### Expected Overhead

| Component | Time |
|-----------|------|
| Serialization | 50-100 ms |
| Network (localhost) | 5-10 ms |
| Deserialization | 50-100 ms |
| **Total Overhead** | **105-210 ms** |

**Baseline Query Time:** ~435 ms
**Network Query Time:** ~540-645 ms
**Overhead:** <30%

### Memory Requirements

**Sender (Server):**
- Encoded database (PVector3D): ~2 GB for YTF (1595 enrollments)
- Peak during serialization: +500 MB

**Receiver (Client):**
- Per-query: ~1 MB for N=64, m=2048
- Network buffers: ~50 MB ZeroMQ overhead

**Recommended minimum:**
- Sender: 4 GB RAM
- Receiver: 2 GB RAM

## Troubleshooting

### Sender fails to start

**Error:** `Address already in use (port 1212)`

**Solution:**
```bash
lsof -i :1212
kill <PID>
# or use different port
cstpsi_sender --dbFile data.csv --paramsFile params.json --port 1213
```

### Receiver cannot connect

**Error:** `Failed to connect to sender`

**Solutions:**
1. Verify sender is running
2. Check port number matches
3. Verify hostname/IP address is correct
4. Check firewall rules

### Results don't match in-process

**Error:** `Results differ!`

**Causes:**
- Different parameter files
- Corrupted serialization
- Network data corruption

**Solutions:**
1. Verify both use same parameter JSON
2. Re-run demo/demo_network.sh and check verify.py output to isolate the issue
3. Check network connectivity

### Timeout during queries

**Error:** `Query timeout`

**Solutions:**
1. Increase timeout: `--timeout 60000`
2. Check sender is not stuck
3. Verify network connectivity
4. Reduce query complexity

## Future Enhancements

### Tier 2 (FLPSI Extension)

1. **TLS Encryption:** ZeroMQ CURVE protocol
2. **Multi-threaded Server:** Handle concurrent queries
3. **Batch Queries:** Multiple queries per message
4. **Parameter Verification:** Hash-based validation
5. **Authentication:** Client credentials

## References

- **SEAL Documentation:** https://github.com/microsoft/SEAL
- **ZeroMQ Guide:** https://zguide.zeromq.org/
- **ASTPSI Source:** https://github.com/contact-discovery/apsi
- **FLPSI Paper:** USENIX Security 2021

---

**Document Version:** 1.0
**Last Updated:** 2026-03-30
**Author:** CSTPSI C++ Implementation Team
