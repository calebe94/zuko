FROM python:3.11-slim-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential cmake git curl pkg-config libusb-1.0-0-dev \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir -U platformio \
    && pio pkg install --global --platform https://github.com/maxgerhardt/platform-raspberrypi.git

WORKDIR /workspace

CMD ["pio", "run", "-e", "rpipico2"]
