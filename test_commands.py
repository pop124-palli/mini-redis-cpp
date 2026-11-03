
import socket

def command(*args):
    message = f"*{len(args)}\r\n"
    for arg in args:
        message += f"${len(arg.encode())}\r\n{arg}\r\n"

    with socket.create_connection(("127.0.0.1", 6379)) as s:
        s.sendall(message.encode())
        return s.recv(1024)

tests = [
    ("SET counter 5", ("SET", "counter", "5")),
    ("INCR counter", ("INCR", "counter")),
    ("INCR counter again", ("INCR", "counter")),
    ("GET counter", ("GET", "counter")),
    ("EXISTS counter", ("EXISTS", "counter")),
    ("DEL counter", ("DEL", "counter")),
    ("EXISTS counter after DEL", ("EXISTS", "counter")),
    ("INCR missing", ("INCR", "missing")),
    ("SET name Aditya", ("SET", "name", "Aditya")),
    ("INCR name (should error)", ("INCR", "name")),
    ("SET temporary value EX 1",
     ("SET", "temporary", "value", "EX", "1")),
]

for description, args in tests:
    print(f"{description}: {command(*args)!r}")
