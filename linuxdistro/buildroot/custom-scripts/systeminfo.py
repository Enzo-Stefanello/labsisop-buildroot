#!/usr/bin/env python3

import ipaddress
import json
import os
import re
import time

from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read().strip()


def get_datetime():
    btime = 0

    with open("/proc/stat", "r") as f:
        for line in f:
            if line.startswith("btime "):
                btime = int(line.split()[1])
                break

    uptime = float(read_text("/proc/uptime").split()[0])
    timestamp = btime + uptime

    return datetime.fromtimestamp(timestamp).strftime("%Y-%m-%d %H:%M:%S")


def get_uptime():
    return int(float(read_text("/proc/uptime").split()[0]))


def read_cpu_times():
    with open("/proc/stat", "r") as f:
        values = [int(v) for v in f.readline().split()[1:]]

    total = sum(values)

    idle = values[3]
    if len(values) > 4:
        idle += values[4]

    return total, idle


def get_cpu_usage():
    total1, idle1 = read_cpu_times()

    time.sleep(0.1)

    total2, idle2 = read_cpu_times()

    delta_total = total2 - total1
    delta_idle = idle2 - idle1

    if delta_total <= 0:
        return 0.0

    usage = 100.0 * (delta_total - delta_idle) / delta_total

    return round(usage, 2)


def get_cpu_info():
    model = "Unknown"
    speed_mhz = 0.0

    with open("/proc/cpuinfo", "r") as f:
        for line in f:
            if ":" not in line:
                continue

            key, value = line.split(":", 1)
            key = key.strip()
            value = value.strip()

            if key in ("model name", "Hardware", "Processor") and model == "Unknown":
                model = value

            if key == "cpu MHz" and speed_mhz == 0.0:
                try:
                    speed_mhz = float(value)
                except ValueError:
                    pass

    return {
        "model": model,
        "speed_mhz": round(speed_mhz, 2),
        "usage_percent": get_cpu_usage()
    }


def get_memory_info():
    values = {}

    with open("/proc/meminfo", "r") as f:
        for line in f:
            parts = line.replace(":", "").split()

            if len(parts) >= 2:
                try:
                    values[parts[0]] = int(parts[1])
                except ValueError:
                    pass

    total_kb = values.get("MemTotal", 0)

    if "MemAvailable" in values:
        used_kb = total_kb - values["MemAvailable"]
    else:
        used_kb = (
            total_kb
            - values.get("MemFree", 0)
            - values.get("Buffers", 0)
            - values.get("Cached", 0)
            - values.get("SReclaimable", 0)
            + values.get("Shmem", 0)
        )

    return {
        "total_mb": round(total_kb / 1024, 2),
        "used_mb": round(used_kb / 1024, 2)
    }


def get_os_version():
    return read_text("/proc/version")


def get_process_list():
    processes = []

    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue

        try:
            name = read_text(f"/proc/{entry}/comm")
            processes.append({
                "pid": int(entry),
                "name": name
            })
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            # O processo pode terminar enquanto /proc está sendo percorrido.
            continue

    processes.sort(key=lambda p: p["pid"])

    return processes


def get_disks():
    disks = []

    for device in sorted(os.listdir("/sys/block")):
        # Ignora dispositivos virtuais que não representam armazenamento
        # físico do sistema.
        if device.startswith(("loop", "ram", "zram")):
            continue

        size_path = f"/sys/block/{device}/size"

        try:
            sectors = int(read_text(size_path))
        except (FileNotFoundError, ValueError):
            continue

        size_mb = int((sectors * 512) / (1024 * 1024))

        disks.append({
            "device": f"/dev/{device}",
            "size_mb": size_mb
        })

    return disks


def get_usb_devices():
    devices = []
    base = "/sys/bus/usb/devices"

    if not os.path.isdir(base):
        return devices

    for port in sorted(os.listdir(base)):
        # Entradas contendo ":" representam interfaces de um dispositivo.
        if ":" in port:
            continue

        path = os.path.join(base, port)

        manufacturer = ""
        product = ""
        vendor = ""
        product_id = ""

        try:
            manufacturer = read_text(os.path.join(path, "manufacturer"))
        except (FileNotFoundError, IsADirectoryError):
            pass

        try:
            product = read_text(os.path.join(path, "product"))
        except (FileNotFoundError, IsADirectoryError):
            pass

        try:
            vendor = read_text(os.path.join(path, "idVendor"))
            product_id = read_text(os.path.join(path, "idProduct"))
        except (FileNotFoundError, IsADirectoryError):
            pass

        description = " ".join(
            value for value in (manufacturer, product) if value
        ).strip()

        if not description and vendor and product_id:
            description = f"{vendor}:{product_id}"

        if description:
            devices.append({
                "port": port,
                "description": description
            })

    return devices


def decode_route_address(value):
    number = int(value, 16)

    return ".".join(str((number >> (8 * i)) & 0xFF) for i in range(4))


def get_local_ipv4_addresses():
    addresses = set()

    try:
        lines = read_text("/proc/net/fib_trie").splitlines()
    except FileNotFoundError:
        return addresses

    for index in range(len(lines) - 1):
        match = re.search(r"(\d+\.\d+\.\d+\.\d+)\s*$", lines[index])

        if match and "/32 host LOCAL" in lines[index + 1]:
            addresses.add(match.group(1))

    return addresses


def get_routes():
    routes = []

    try:
        with open("/proc/net/route", "r") as f:
            next(f, None)

            for line in f:
                fields = line.split()

                if len(fields) < 8:
                    continue

                interface = fields[0]
                destination = decode_route_address(fields[1])
                gateway = decode_route_address(fields[2])
                mask = decode_route_address(fields[7])

                try:
                    network = ipaddress.IPv4Network(
                        f"{destination}/{mask}",
                        strict=False
                    )
                except ValueError:
                    continue

                routes.append({
                    "interface": interface,
                    "network": network,
                    "gateway": gateway
                })

    except FileNotFoundError:
        pass

    return routes


def get_network_adapters():
    interfaces = sorted(os.listdir("/sys/class/net"))
    addresses = get_local_ipv4_addresses()
    routes = get_routes()

    address_by_interface = {}

    for address in addresses:
        ip = ipaddress.IPv4Address(address)

        if ip.is_loopback:
            address_by_interface["lo"] = address
            continue

        candidates = []

        for route in routes:
            if route["network"].prefixlen == 0:
                continue

            if ip in route["network"]:
                candidates.append(route)

        if candidates:
            best_route = max(
                candidates,
                key=lambda r: r["network"].prefixlen
            )

            address_by_interface[best_route["interface"]] = address

    adapters = []

    for interface in interfaces:
        adapters.append({
            "interface": interface,
            "ip_address": address_by_interface.get(interface, "")
        })

    return adapters


class StatusHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/status":
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Not Found")
            return

        response = {
            "datetime": get_datetime(),
            "uptime_seconds": get_uptime(),
            "cpu": get_cpu_info(),
            "memory": get_memory_info(),
            "os_version": get_os_version(),
            "processes": get_process_list(),
            "disks": get_disks(),
            "usb_devices": get_usb_devices(),
            "network_adapters": get_network_adapters()
        }

        data = json.dumps(response, indent=2).encode()

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def run_server(port=8080):
    print(f"Servidor disponível em http://0.0.0.0:{port}/status")
    server = HTTPServer(("0.0.0.0", port), StatusHandler)
    server.serve_forever()


if __name__ == "__main__":
    run_server()
