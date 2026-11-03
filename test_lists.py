
import socket

def command(*args):
    message = f"*{len(args)}\r\n"

    for arg in args:
        message += f"${len(arg.encode())}\r\n{arg}\r\n"

    with socket.create_connection(("127.0.0.1", 6379)) as sock:
        sock.sendall(message.encode())
        sock.shutdown(socket.SHUT_WR)

        response = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk

        return response.decode()

tests = [
    ("RPUSH fruits apple banana", ("RPUSH", "fruits", "apple", "banana")),
    ("LPUSH fruits mango", ("LPUSH", "fruits", "mango")),
    ("LRANGE fruits 0 -1", ("LRANGE", "fruits", "0", "-1")),
    ("LLEN fruits", ("LLEN", "fruits")),
    ("LPOP fruits", ("LPOP", "fruits")),
    ("RPOP fruits", ("RPOP", "fruits")),
    ("LLEN fruits after pops", ("LLEN", "fruits")),
    ("SET name Aditya", ("SET", "name", "Aditya")),
    ("LPUSH name test (wrong type)", ("LPUSH", "name", "test")),
    ("DEL fruits", ("DEL", "fruits")),
    ("LLEN fruits after DEL", ("LLEN", "fruits")),
]

for description, args in tests:
    print(f"\n{description}:")
    print(repr(command(*args)))
