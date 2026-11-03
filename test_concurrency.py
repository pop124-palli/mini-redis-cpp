
import socket
import threading

HOST = "127.0.0.1"
PORT = 6379

NUM_CLIENTS = 10
INCREMENTS_PER_CLIENT = 100


def encode_command(*args):
    """Encode a command using the RESP array format."""
    message = f"*{len(args)}\r\n"

    for arg in args:
        encoded = str(arg).encode()
        message += f"${len(encoded)}\r\n{arg}\r\n"

    return message.encode()


def read_response(file):
    """Read one complete RESP response."""
    line = file.readline()

    if not line:
        raise ConnectionError("Server closed the connection.")

    prefix = line[:1]
    content = line[1:].decode().rstrip("\r\n")

    if prefix in (b"+", b"-", b":"):
        return content

    if prefix == b"$":
        length = int(content)

        if length == -1:
            return None

        data = file.read(length + 2)

        if len(data) != length + 2:
            raise ConnectionError("Incomplete bulk-string response.")

        return data[:-2].decode()

    raise ValueError(f"Unexpected RESP response: {line!r}")


def send_command(sock, file, *args):
    sock.sendall(encode_command(*args))
    return read_response(file)


def worker(errors):
    try:
        with socket.create_connection((HOST, PORT)) as sock:
            with sock.makefile("rb") as file:
                for _ in range(INCREMENTS_PER_CLIENT):
                    response = send_command(sock, file, "INCR", "concurrent_counter")

                    if not response or not response.lstrip("-").isdigit():
                        raise RuntimeError(
                            f"Unexpected INCR response: {response!r}"
                        )

    except Exception as error:
        errors.append(str(error))


def main():
    # Reset the counter before testing.
    with socket.create_connection((HOST, PORT)) as sock:
        with sock.makefile("rb") as file:
            response = send_command(
                sock, file, "SET", "concurrent_counter", "0"
            )
            print("Reset counter:", response)

    threads = []
    errors = []

    print(
        f"Starting {NUM_CLIENTS} clients, "
        f"each performing {INCREMENTS_PER_CLIENT} increments..."
    )

    for _ in range(NUM_CLIENTS):
        thread = threading.Thread(target=worker, args=(errors,))
        threads.append(thread)
        thread.start()

    for thread in threads:
        thread.join()

    # Read the final counter.
    with socket.create_connection((HOST, PORT)) as sock:
        with sock.makefile("rb") as file:
            result = send_command(sock, file, "GET", "concurrent_counter")

    expected = NUM_CLIENTS * INCREMENTS_PER_CLIENT

    print("Expected counter:", expected)
    print("Actual counter:  ", result)
    print("Worker errors:   ", errors)

    if not errors and result == str(expected):
        print("\nPASS: All concurrent increments were preserved!")
    else:
        print("\nFAIL: Investigate the server or the reported errors.")


if __name__ == "__main__":
    main()
