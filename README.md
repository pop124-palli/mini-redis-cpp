# Mini Redis Server in C++

A lightweight, Redis-compatible in-memory key-value server built using C++17 and Winsock. The project implements TCP client-server communication, RESP-style command parsing, concurrent client handling, TTL-based key expiration, and custom snapshot persistence.

## Features

- **TCP Server:** Accepts client connections using Winsock.
- **RESP Parsing:** Processes Redis-style commands sent over TCP.
- **Concurrent Clients:** Handles clients using separate threads.
- **String Operations:** `PING`, `ECHO`, `SET`, `GET`, `DEL`, `EXISTS`, `INCR`.
- **List Operations:** `LPUSH`, `RPUSH`, `LRANGE`, `LLEN`, `LPOP`, `RPOP`.
- **Key Expiration:** Supports `SET key value EX seconds`.
- **Persistence:** Saves and restores data through a custom snapshot file named `dump.rdb`.
- **Thread Safety:** Uses a mutex to protect shared in-memory storage.
- **Automated Tests:** Includes Python scripts for command, list, and concurrency testing.

## Tech Stack

- C++17
- Winsock API
- TCP/IP networking
- C++ Standard Library: `unordered_map`, `deque`, `mutex`, `thread`
- Python 3 for testing
- Git and GitHub

## Architecture

1. **Connection Handling:** The server listens on port `6379` and accepts incoming TCP connections.
2. **Input Buffering:** Incoming bytes are buffered because TCP does not guarantee that a complete command arrives in a single read.
3. **RESP Parsing:** Complete commands are identified and converted into command arguments.
4. **Command Execution:** The command handler executes operations against the in-memory data store.
5. **Storage:** Hash maps store string values and lists; deques support list operations at both ends.
6. **Concurrency:** Separate client threads process connections, while a mutex protects shared data.
7. **Expiration:** Keys with expired TTL deadlines are removed when accessed.
8. **Persistence:** A custom snapshot is loaded at startup and saved to preserve data across restarts.

## Build and Run

### Requirements

- Windows
- A C++17-compatible compiler, such as MinGW-w64 GCC
- Python 3 for the test scripts

### Compile

Run this command from the project root:

```powershell
g++ -std=c++17 src/main.cpp -o redis-server.exe -lws2_32
```

### Start the server

```powershell
.\redis-server.exe
```

The server listens on port `6379`.

## Run Tests

Keep the server running in one terminal. Open a second terminal in the project root and run the test scripts that are present:

```powershell
python test_redis.py
python test_commands.py
python test_concurrency.py
python test_clients.py
python test_lists.py
```

## Limitations

- Implements a subset of Redis commands rather than the complete Redis protocol.
- Uses a custom snapshot format, not Redis's official RDB format.
- Lazy expiration means expired keys are checked when accessed rather than removed by a background expiration process.
- Saving the full dataset after writes can become inefficient as the dataset grows.
- Thread-per-client handling is straightforward but may not scale as well as an event-driven architecture.

## Future Improvements

- Implement additional Redis-compatible commands and data types.
- Improve persistence reliability and crash recovery.
- Add more edge-case and protocol compatibility tests.
- Introduce an event-driven networking model for improved scalability.
- Add performance benchmarks and memory usage measurements.

## Learning Outcomes

This project provided hands-on experience with TCP networking, protocol parsing, concurrency, synchronization, data structures, TTL management, persistence, and automated testing.
