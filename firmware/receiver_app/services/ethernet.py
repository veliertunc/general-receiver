"""Ethernet status and multicast MPEG-TS reception."""
import ipaddress
import socket


def status():
    return {"interfaces": [{"index": index, "name": name} for index, name in socket.if_nameindex()]}


class MulticastStream:
    def __init__(self, group, port, config):
        try:
            address = ipaddress.ip_address(group)
        except ValueError as error:
            raise ValueError("Use an IPv4 multicast group in 239.0.0.0/8") from error
        if address.version != 4 or address not in ipaddress.ip_network("239.0.0.0/8"):
            raise ValueError("Use an IPv4 multicast group in 239.0.0.0/8")
        if not 1 <= port <= 65535:
            raise ValueError("UDP port must be between 1 and 65535")

        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        try:
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.bind(("", port))
            interface = config.get("multicast_interface", "0.0.0.0")
            membership = socket.inet_aton(group) + socket.inet_aton(interface)
            self.socket.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
        except (OSError, ValueError):
            self.socket.close()
            raise

    def read(self):
        return self.socket.recvfrom(65536)[0]

    def close(self):
        self.socket.close()


def open_multicast(group, port, config):
    return MulticastStream(group, port, config)
