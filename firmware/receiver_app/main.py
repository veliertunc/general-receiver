"""Application startup and configuration loading."""
import json
import os

from receiver_app.api import serve
from receiver_app.services import ethernet, radio, television


def validate_config(config):
    for section in ("api", "television", "radio", "ethernet"):
        if not isinstance(config.get(section), dict):
            raise ValueError(f"Configuration section '{section}' must contain a JSON object")

    port = config["api"].get("listen_port", 8080)
    if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
        raise ValueError("api.listen_port must be an integer from 1 to 65535")

    for section, validate in (
        ("radio", radio.validate_config),
        ("television", television.validate_config),
        ("ethernet", ethernet.validate_config),
    ):
        try:
            validate(config[section])
        except (TypeError, ValueError) as error:
            raise ValueError(f"Invalid {section} configuration: {error}") from error


def main():
    config_dir = os.environ.get("RECEIVER_CONFIG_DIR", "/etc/receiver")
    config = {}
    for name in ("api", "television", "radio", "ethernet"):
        config_path = os.path.join(config_dir, f"{name}.json")
        with open(config_path, encoding="utf-8") as config_file:
            config[name] = json.load(config_file)
    validate_config(config)
    serve(config)


if __name__ == "__main__":
    main()
