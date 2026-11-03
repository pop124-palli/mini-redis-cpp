
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <thread>
#include <mutex>
#include <limits>
#include <deque>
#include <algorithm>

#pragma comment(lib, "Ws2_32.lib")

// Find the end of one complete RESP array of bulk strings.
size_t findMessageEnd(const std::string &data)
{
  if (data.empty() || data[0] != '*')
    return std::string::npos;

  size_t lineEnd = data.find("\r\n");
  if (lineEnd == std::string::npos)
    return std::string::npos;

  int count;

  try
  {
    size_t used = 0;
    count = std::stoi(data.substr(1, lineEnd - 1), &used);

    if (used != lineEnd - 1 || count < 0 || count > 10000)
      return std::string::npos;
  }
  catch (...)
  {
    return std::string::npos;
  }

  size_t pos = lineEnd + 2;

  for (int i = 0; i < count; ++i)
  {
    if (pos >= data.size() || data[pos] != '$')
      return std::string::npos;

    lineEnd = data.find("\r\n", pos);
    if (lineEnd == std::string::npos)
      return std::string::npos;

    int length;

    try
    {
      size_t used = 0;
      length = std::stoi(
          data.substr(pos + 1, lineEnd - pos - 1), &used);

      if (used != lineEnd - pos - 1 ||
          length < 0 || length > 1024 * 1024)
        return std::string::npos;
    }
    catch (...)
    {
      return std::string::npos;
    }

    pos = lineEnd + 2;

    if (data.size() - pos < static_cast<size_t>(length) + 2)
      return std::string::npos;

    pos += static_cast<size_t>(length);

    if (data.compare(pos, 2, "\r\n") != 0)
      return std::string::npos;

    pos += 2;
  }

  return pos;
}

// Parse a complete RESP message into command arguments.
std::vector<std::string> parseCommand(const std::string &message)
{
  std::vector<std::string> args;

  size_t lineEnd = message.find("\r\n");

  if (lineEnd == std::string::npos ||
      message.empty() || message[0] != '*')
    throw std::runtime_error("Invalid array");

  size_t used = 0;
  int count = std::stoi(message.substr(1, lineEnd - 1), &used);

  if (used != lineEnd - 1 || count < 0 || count > 10000)
    throw std::runtime_error("Invalid array length");

  size_t pos = lineEnd + 2;

  for (int i = 0; i < count; ++i)
  {
    if (pos >= message.size() || message[pos] != '$')
      throw std::runtime_error("Expected bulk string");

    lineEnd = message.find("\r\n", pos);

    if (lineEnd == std::string::npos)
      throw std::runtime_error("Missing length ending");

    used = 0;
    int length = std::stoi(
        message.substr(pos + 1, lineEnd - pos - 1), &used);

    if (used != lineEnd - pos - 1 ||
        length < 0 || length > 1024 * 1024)
      throw std::runtime_error("Invalid bulk string length");

    pos = lineEnd + 2;

    if (message.size() - pos < static_cast<size_t>(length) + 2)
      throw std::runtime_error("Incomplete bulk string");

    args.push_back(
        message.substr(pos, static_cast<size_t>(length)));

    pos += static_cast<size_t>(length);

    if (message.compare(pos, 2, "\r\n") != 0)
      throw std::runtime_error("Invalid bulk string ending");

    pos += 2;
  }

  if (pos != message.size())
    throw std::runtime_error("Unexpected trailing data");

  return args;
}

// Format a RESP bulk-string response.
std::string bulkString(const std::string &value)
{
  return "$" + std::to_string(value.size()) +
         "\r\n" + value + "\r\n";
}

// In-memory entries with optional expiration times.
struct Entry
{
  std::string value;
  std::optional<std::chrono::system_clock::time_point> expiry;
};

std::unordered_map<std::string, Entry> store;
std::mutex storeMutex;
std::unordered_map<std::string, std::deque<std::string>> listStore;

// Execute a command. The mutex protects shared key-value data.

std::string handleCommand(const std::vector<std::string> &args)
{
  if (args.empty())
    return "-ERR empty command\r\n";

  std::string command = args[0];

  std::transform(
      command.begin(), command.end(), command.begin(),
      [](unsigned char c)
      {
        return static_cast<char>(std::toupper(c));
      });

  // PING
  if (command == "PING")
  {
    if (args.size() == 1)
      return "+PONG\r\n";

    if (args.size() == 2)
      return bulkString(args[1]);

    return "-ERR wrong number of arguments for 'ping' command\r\n";
  }

  // ECHO
  if (command == "ECHO")
  {
    if (args.size() != 2)
      return "-ERR wrong number of arguments for 'echo' command\r\n";

    return bulkString(args[1]);
  }

  // SET key value [EX seconds]
  if (command == "SET")
  {
    if (args.size() != 3 && args.size() != 5)
      return "-ERR wrong number of arguments for 'set' command\r\n";

    std::optional<std::chrono::system_clock::time_point> expiry;

    if (args.size() == 5)
    {
      std::string option = args[3];

      std::transform(
          option.begin(), option.end(), option.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::toupper(c));
          });

      if (option != "EX")
        return "-ERR syntax error\r\n";

      long long seconds;

      try
      {
        size_t used = 0;
        seconds = std::stoll(args[4], &used);

        if (used != args[4].size() || seconds <= 0)
          return "-ERR invalid expire time\r\n";
      }
      catch (...)
      {
        return "-ERR invalid expire time\r\n";
      }

      expiry = std::chrono::system_clock::now() + std::chrono::seconds(seconds);
    }

    {
      std::lock_guard<std::mutex> lock(storeMutex);

      // SET replaces a list with a string.
      listStore.erase(args[1]);
      store[args[1]] = Entry{args[2], expiry};
    }

    return "+OK\r\n";
  }

  // GET key
  if (command == "GET")
  {
    if (args.size() != 2)
      return "-ERR wrong number of arguments for 'get' command\r\n";

    std::lock_guard<std::mutex> lock(storeMutex);

    // GET cannot read a list.
    if (listStore.count(args[1]))
      return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

    auto it = store.find(args[1]);

    if (it == store.end())
      return "$-1\r\n";

    if (it->second.expiry.has_value() &&
        std::chrono::system_clock::now() >= it->second.expiry.value())
    {
      store.erase(it);
      return "$-1\r\n";
    }

    return bulkString(it->second.value);
  }

  // LPUSH key element [element ...]
  if (command == "LPUSH")
  {
    if (args.size() < 3)
      return "-ERR wrong number of arguments for 'lpush' command\r\n";

    std::lock_guard<std::mutex> lock(storeMutex);

    auto it = store.find(args[1]);

    // Remove an expired string key if present.
    if (it != store.end() &&
        it->second.expiry.has_value() &&
        std::chrono::system_clock::now() >= it->second.expiry.value())
    {
      store.erase(it);
      it = store.end();
    }

    if (it != store.end())
      return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

    auto &list = listStore[args[1]];

    for (size_t i = 2; i < args.size(); ++i)
      list.push_front(args[i]);

    return ":" + std::to_string(list.size()) + "\r\n";
  }

  // RPUSH key element [element ...]
  if (command == "RPUSH")
  {
    if (args.size() < 3)
      return "-ERR wrong number of arguments for 'rpush' command\r\n";

    std::lock_guard<std::mutex> lock(storeMutex);

    auto it = store.find(args[1]);

    if (it != store.end() &&
        it->second.expiry.has_value() &&
        std::chrono::system_clock::now() >= it->second.expiry.value())
    {
      store.erase(it);
      it = store.end();
    }

    if (it != store.end())
      return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

    auto &list = listStore[args[1]];

    for (size_t i = 2; i < args.size(); ++i)
      list.push_back(args[i]);

    return ":" + std::to_string(list.size()) + "\r\n";
  }

  // LRANGE key start stop
  if (command == "LRANGE")
  {
    if (args.size() != 4)
      return "-ERR wrong number of arguments for 'lrange' command\r\n";

    long long start, stop;

    try
    {
      size_t usedStart = 0, usedStop = 0;

      start = std::stoll(args[2], &usedStart);
      stop = std::stoll(args[3], &usedStop);

      if (usedStart != args[2].size() ||
          usedStop != args[3].size())
        return "-ERR value is not an integer or out of range\r\n";
    }
    catch (...)
    {
      return "-ERR value is not an integer or out of range\r\n";
    }

    std::lock_guard<std::mutex> lock(storeMutex);

    if (store.count(args[1]))
    {
      auto it = store.find(args[1]);

      if (!it->second.expiry.has_value() ||
          std::chrono::system_clock::now() < it->second.expiry.value())
        return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

      store.erase(it);
    }

    auto it = listStore.find(args[1]);

    if (it == listStore.end())
      return "*0\r\n";

    const auto &list = it->second;
    long long length = static_cast<long long>(list.size());

    // Negative indexes count backward from the end.
    if (start < 0)
      start = std::max(0LL, length + start);

    if (stop < 0)
      stop = length + stop;

    if (start >= length || stop < 0 || start > stop)
      return "*0\r\n";

    stop = std::min(stop, length - 1);

    std::string response =
        "*" + std::to_string(stop - start + 1) + "\r\n";

    for (long long i = start; i <= stop; ++i)
      response += bulkString(list[static_cast<size_t>(i)]);

    return response;
  }

  // LLEN key
  if (command == "LLEN")
  {
    if (args.size() != 2)
      return "-ERR wrong number of arguments for 'llen' command\r\n";

    std::lock_guard<std::mutex> lock(storeMutex);

    auto sit = store.find(args[1]);

    if (sit != store.end())
    {
      if (!sit->second.expiry.has_value() ||
          std::chrono::system_clock::now() < sit->second.expiry.value())
        return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

      store.erase(sit);
    }

    auto it = listStore.find(args[1]);

    if (it == listStore.end())
      return ":0\r\n";

    return ":" + std::to_string(it->second.size()) + "\r\n";
  }

  // LPOP key
  if (command == "LPOP" || command == "RPOP")
  {
    if (args.size() != 2)
      return command == "LPOP"
                 ? "-ERR wrong number of arguments for 'lpop' command\r\n"
                 : "-ERR wrong number of arguments for 'rpop' command\r\n";

    std::lock_guard<std::mutex> lock(storeMutex);

    auto sit = store.find(args[1]);

    if (sit != store.end())
    {
      if (!sit->second.expiry.has_value() ||
          std::chrono::system_clock::now() < sit->second.expiry.value())
        return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

      store.erase(sit);
    }

    auto it = listStore.find(args[1]);

    if (it == listStore.end() || it->second.empty())
      return "$-1\r\n";

    std::string value;

    if (command == "LPOP")
    {
      value = it->second.front();
      it->second.pop_front();
    }
    else
    {
      value = it->second.back();
      it->second.pop_back();
    }

    if (it->second.empty())
      listStore.erase(it);

    return bulkString(value);
  }

  // DEL key [key ...]
  // DEL key [key ...]
  if (command == "DEL")
  {
    if (args.size() < 2)
      return "-ERR wrong number of arguments for 'del' command\r\n";

    int deleted = 0;

    {
      std::lock_guard<std::mutex> lock(storeMutex);
      auto now = std::chrono::system_clock::now();

      for (size_t i = 1; i < args.size(); ++i)
      {
        auto sit = store.find(args[i]);

        if (sit != store.end())
        {
          bool expired =
              sit->second.expiry.has_value() &&
              now >= sit->second.expiry.value();

          store.erase(sit);

          if (!expired)
          {
            ++deleted;
            listStore.erase(args[i]);
            continue;
          }
        }

        auto lit = listStore.find(args[i]);

        if (lit != listStore.end())
        {
          listStore.erase(lit);
          ++deleted;
        }
      }
    }

    return ":" + std::to_string(deleted) + "\r\n";
  }
  // EXISTS key [key ...]
  if (command == "EXISTS")
  {
    if (args.size() < 2)
      return "-ERR wrong number of arguments for 'exists' command\r\n";

    int count = 0;

    {
      std::lock_guard<std::mutex> lock(storeMutex);
      auto now = std::chrono::system_clock::now();

      for (size_t i = 1; i < args.size(); ++i)
      {
        auto sit = store.find(args[i]);

        if (sit != store.end())
        {
          if (sit->second.expiry.has_value() &&
              now >= sit->second.expiry.value())
          {
            store.erase(sit);
          }
          else
          {
            ++count;
            continue;
          }
        }

        if (listStore.count(args[i]))
          ++count;
      }
    }

    return ":" + std::to_string(count) + "\r\n";
  }

  // INCR key
  if (command == "INCR")
  {
    if (args.size() != 2)
      return "-ERR wrong number of arguments for 'incr' command\r\n";

    std::lock_guard<std::mutex> lock(storeMutex);

    if (listStore.count(args[1]))
      return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

    auto it = store.find(args[1]);
    auto now = std::chrono::system_clock::now();

    if (it != store.end() &&
        it->second.expiry.has_value() &&
        now >= it->second.expiry.value())
    {
      store.erase(it);
      it = store.end();
    }

    if (it == store.end())
    {
      store[args[1]] = Entry{"1", std::nullopt};
      return ":1\r\n";
    }

    long long value;

    try
    {
      size_t used = 0;
      value = std::stoll(it->second.value, &used);

      if (used != it->second.value.size())
        return "-ERR value is not an integer or out of range\r\n";
    }
    catch (...)
    {
      return "-ERR value is not an integer or out of range\r\n";
    }

    if (value == std::numeric_limits<long long>::max())
      return "-ERR increment or decrement would overflow\r\n";

    ++value;
    it->second.value = std::to_string(value);

    return ":" + std::to_string(value) + "\r\n";
  }

  return "-ERR unknown command\r\n";
}

// Send all response bytes, including after a partial send.
bool sendResponse(SOCKET client, const std::string &response)
{
  size_t sent = 0;

  while (sent < response.size())
  {
    int result = send(
        client,
        response.data() + sent,
        static_cast<int>(response.size() - sent),
        0);

    if (result == SOCKET_ERROR || result == 0)
      return false;

    sent += static_cast<size_t>(result);
  }

  return true;
}

// Handle one client independently. Each client thread has its own input buffer.
void handleClient(SOCKET client)
{
  std::string input;
  char buffer[1024];

  while (true)
  {
    int received = recv(client, buffer, sizeof(buffer), 0);

    if (received <= 0)
      break;

    input.append(buffer, static_cast<size_t>(received));

    while (!input.empty())
    {
      size_t messageEnd = findMessageEnd(input);

      if (messageEnd == std::string::npos)
        break;

      std::string message = input.substr(0, messageEnd);
      std::string response;

      try
      {
        std::vector<std::string> args = parseCommand(message);

        std::cout << "Command from client: ";
        for (const auto &arg : args)
          std::cout << "[" << arg << "] ";
        std::cout << '\n';

        response = handleCommand(args);
      }
      catch (const std::exception &)
      {
        response = "-ERR invalid command format\r\n";
      }

      if (!sendResponse(client, response))
      {
        closesocket(client);
        std::cout << "Client disconnected after send error\n";
        return;
      }

      input.erase(0, messageEnd);
    }

    if (input.size() > 1024 * 1024)
    {
      std::cout << "Client input too large; closing connection\n";
      break;
    }
  }

  closesocket(client);
  std::cout << "Client disconnected\n";
}

int main()
{
  WSADATA wsaData;

  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
  {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }

  SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (server == INVALID_SOCKET)
  {
    std::cerr << "Failed to create socket\n";
    WSACleanup();
    return 1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(6379);

  if (bind(server, reinterpret_cast<sockaddr *>(&address),
           sizeof(address)) == SOCKET_ERROR)
  {
    std::cerr << "Failed to bind port 6379. Is another server running?\n";
    closesocket(server);
    WSACleanup();
    return 1;
  }

  if (listen(server, SOMAXCONN) == SOCKET_ERROR)
  {
    std::cerr << "listen failed\n";
    closesocket(server);
    WSACleanup();
    return 1;
  }

  std::cout << "Mini Redis listening on port 6379\n";

  // Accept clients continuously and give each client its own thread.
  while (true)
  {
    SOCKET client = accept(server, nullptr, nullptr);

    if (client == INVALID_SOCKET)
    {
      std::cerr << "accept failed\n";
      continue;
    }

    std::cout << "New client connected\n";

    try
    {
      std::thread clientThread(handleClient, client);
      clientThread.detach();
    }
    catch (const std::exception &e)
    {
      std::cerr << "Could not start client thread: "
                << e.what() << '\n';
      closesocket(client);
    }
  }

  // This server normally runs until it is stopped.
  closesocket(server);
  WSACleanup();
  return 0;
}
