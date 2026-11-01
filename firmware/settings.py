"""Shared configuration loading for the receiver services."""
import json
import os

CONFIG_PATH = os.environ.get("RECEIVER_CONFIG", "/etc/receiver/receiver.json")
with open(CONFIG_PATH, encoding="utf-8") as config_file:
    CONFIG = json.load(config_file)
