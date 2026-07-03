# network

Wire transport for the network-mode sender/receiver.

- `network.{h,cc}` -- `NetworkServer` (sender) and `NetworkClient` (receiver)
  over ZeroMQ: session setup, length-prefixed query/response messages, error
  codes, byte accounting, and protocol-version checks.
- `serialization.{h,cc}` -- `SealSerializer`: length-prefixed binary
  serialization of `Ciphertext`, `vector<Ciphertext>`, and `CVector2D` using
  SEAL's native save/load. This is the only path between the two parties.
