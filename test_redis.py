
import socket
import threading
import time
import unittest

HOST = "127.0.0.1"
PORT = 6379


def encode_command(*args):
    parts = [f"*{len(args)}\r\n".encode()]

    for arg in args:
        data = str(arg).encode()
        parts.append(f"${len(data)}\r\n".encode())
        parts.append(data + b"\r\n")

    return b"".join(parts)


def read_resp(file):
    """Read one complete RESP response."""
    line = file.readline()

    if not line:
        raise ConnectionError("Server closed the connection.")

    prefix = line[:1]
    content = line[1:].decode().rstrip("\r\n")

    if prefix in (b"+", b"-"):
        return (prefix.decode(), content)

    if prefix == b":":
        return (":", int(content))

    if prefix == b"$":
        length = int(content)

        if length == -1:
            return ("$", None)

        data = file.read(length + 2)

        if len(data) != length + 2:
            raise ConnectionError("Incomplete bulk-string response.")

        return ("$", data[:-2].decode())

    if prefix == b"*":
        length = int(content)

        if length == -1:
            return ("*", None)

        return ("*", [read_resp(file) for _ in range(length)])

    raise ValueError(f"Unknown RESP response: {line!r}")


class RedisTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Fail early if the server is not running.
        try:
            with socket.create_connection((HOST, PORT), timeout=3):
                pass
        except OSError as exc:
            raise RuntimeError(
                "Cannot connect to the Redis clone. "
                "Start redis-server.exe in another terminal."
            ) from exc

    def setUp(self):
        self.sock = socket.create_connection((HOST, PORT), timeout=5)
        self.file = self.sock.makefile("rb")

    def tearDown(self):
        self.file.close()
        self.sock.close()

    def command(self, *args):
        self.sock.sendall(encode_command(*args))
        return read_resp(self.file)

    def assert_integer(self, response, expected):
        self.assertEqual(response, (":", expected))

    def assert_bulk(self, response, expected):
        self.assertEqual(response, ("$", expected))

    # ---------- Basic commands ----------

    def test_ping(self):
        self.assertEqual(self.command("PING"), ("+", "PONG"))

    def test_echo(self):
        self.assert_bulk(self.command("ECHO", "hello"), "hello")

    def test_set_get(self):
        key = "test:set_get"
        self.assertEqual(self.command("SET", key, "hello"), ("+", "OK"))
        self.assert_bulk(self.command("GET", key), "hello")

    def test_get_missing_key(self):
        self.assert_bulk(self.command("GET", "test:missing"), None)

    def test_incr(self):
        key = "test:incr"
        self.command("SET", key, "10")
        self.assert_integer(self.command("INCR", key), 11)
        self.assert_bulk(self.command("GET", key), "11")

    # ---------- List commands ----------

    def test_rpush_and_lpush(self):
        key = "test:list_push"

        self.assert_integer(
            self.command("RPUSH", key, "apple", "banana"), 2
        )
        self.assert_integer(self.command("LPUSH", key, "mango"), 3)

        response = self.command("LRANGE", key, "0", "-1")
        self.assertEqual(
            response,
            ("*", [("$", "mango"), ("$", "apple"), ("$", "banana")])
        )

    def test_lrange_slice(self):
        key = "test:list_slice"
        self.command("RPUSH", key, "a", "b", "c", "d")

        self.assertEqual(
            self.command("LRANGE", key, "1", "2"),
            ("*", [("$", "b"), ("$", "c")])
        )

    def test_llen(self):
        key = "test:list_length"
        self.command("RPUSH", key, "a", "b", "c")
        self.assert_integer(self.command("LLEN", key), 3)

    def test_lpop_and_rpop(self):
        key = "test:list_pop"
        self.command("RPUSH", key, "first", "middle", "last")

        self.assert_bulk(self.command("LPOP", key), "first")
        self.assert_bulk(self.command("RPOP", key), "last")
        self.assert_integer(self.command("LLEN", key), 1)

    def test_missing_list(self):
        key = "test:missing_list"

        self.assert_integer(self.command("LLEN", key), 0)
        self.assertEqual(self.command("LRANGE", key, "0", "-1"), ("*", []))
        self.assert_bulk(self.command("LPOP", key), None)

    def test_pop_last_element_removes_list(self):
        key = "test:single_element_list"
        self.command("RPUSH", key, "only")

        self.assert_bulk(self.command("LPOP", key), "only")
        self.assert_integer(self.command("LLEN", key), 0)
        self.assert_integer(self.command("EXISTS", key), 0)

    # ---------- Key behavior and types ----------

    def test_set_replaces_list(self):
        key = "test:replace_list"
        self.command("RPUSH", key, "a", "b")

        self.assertEqual(self.command("SET", key, "now_string"), ("+", "OK"))
        self.assert_bulk(self.command("GET", key), "now_string")
        self.assertEqual(self.command("LLEN", key)[0], "-")

    def test_list_commands_reject_string_key(self):
        key = "test:wrong_type"
        self.command("SET", key, "text")

        for args in [
            ("LPUSH", key, "x"),
            ("RPUSH", key, "x"),
            ("LLEN", key),
            ("LRANGE", key, "0", "-1"),
            ("LPOP", key),
            ("RPOP", key),
        ]:
            with self.subTest(command=args[0]):
                response = self.command(*args)
                self.assertEqual(response[0], "-")
                self.assertIn("WRONGTYPE", response[1])

    def test_get_and_incr_reject_list_key(self):
        key = "test:string_commands_on_list"
        self.command("RPUSH", key, "x")

        for args in [("GET", key), ("INCR", key)]:
            with self.subTest(command=args[0]):
                response = self.command(*args)
                self.assertEqual(response[0], "-")
                self.assertIn("WRONGTYPE", response[1])

    def test_del_and_exists_for_list(self):
        key = "test:delete_list"
        self.command("RPUSH", key, "x")

        self.assert_integer(self.command("EXISTS", key), 1)
        self.assert_integer(self.command("DEL", key), 1)
        self.assert_integer(self.command("EXISTS", key), 0)

    def test_expired_string_can_become_list(self):
        key = "test:expired_string_to_list"
        self.command("SET", key, "old", "EX", "1")
        time.sleep(1.2)

        self.assert_integer(self.command("RPUSH", key, "new"), 1)
        self.assertEqual(
            self.command("LRANGE", key, "0", "-1"),
            ("*", [("$", "new")])
        )

    # ---------- Protocol ----------

    def test_multiple_commands_on_one_connection(self):
        self.assertEqual(self.command("PING"), ("+", "PONG"))
        self.assertEqual(self.command("PING"), ("+", "PONG"))
        self.assert_bulk(self.command("ECHO", "same connection"), "same connection")


class ConcurrencyTests(unittest.TestCase):
    def test_concurrent_increments(self):
        key = "test:concurrent_counter"
        clients = 10
        increments_each = 50
        errors = []

        def worker():
            try:
                with socket.create_connection((HOST, PORT), timeout=5) as sock:
                    with sock.makefile("rb") as file:
                        for _ in range(increments_each):
                            sock.sendall(encode_command("INCR", key))
                            response = read_resp(file)

                            if response[0] != ":":
                                raise AssertionError(
                                    f"Unexpected INCR response: {response}"
                                )
            except Exception as exc:
                errors.append(str(exc))

        # Initialize the shared counter.
        with socket.create_connection((HOST, PORT), timeout=5) as sock:
            with sock.makefile("rb") as file:
                sock.sendall(encode_command("SET", key, "0"))
                read_resp(file)

        threads = [
            threading.Thread(target=worker)
            for _ in range(clients)
        ]

        for thread in threads:
            thread.start()

        for thread in threads:
            thread.join()

        self.assertEqual(errors, [], f"Client errors: {errors}")

        with socket.create_connection((HOST, PORT), timeout=5) as sock:
            with sock.makefile("rb") as file:
                sock.sendall(encode_command("GET", key))
                result = read_resp(file)

        self.assertEqual(result, ("$", str(clients * increments_each)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
