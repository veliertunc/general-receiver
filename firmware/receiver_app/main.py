"""Application startup and configuration loading."""
import json
import os

from receiver_app.api import serve


def main():
    config_path = os.environ.get("RECEIVER_CONFIG", "/etc/receiver/receiver.json")
    with open(config_path, encoding="utf-8") as config_file:
        config = json.load(config_file)
    serve(config)


if __name__ == "__main__":
    main()
