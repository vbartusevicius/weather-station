FROM python:3.13.9-slim-trixie

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       clang-format-19 clang-tidy-19 g++ make git ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir platformio==6.2.0

WORKDIR /work
ENTRYPOINT ["/bin/bash", "-c"]
