import socket
import threading

def send_command(name, command):
    with socket.create_connection(("127.0.0.1", 6379)) as s:
        s.sendall(command)
        response = s.recv(1024)
        print(f"{name}: {response!r}")

client1 = threading.Thread(
    target=send_command,
    args=("Client 1", b"*1\r\n$4\r\nPING\r\n")
)

client2 = threading.Thread(
    target=send_command,
    args=("Client 2", b"*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n")
)

client1.start()
client2.start()

client1.join()
client2.join()