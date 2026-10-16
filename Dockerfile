FROM gcc:14

RUN apt-get update \
  && apt-get install -y --no-install-recommends cmake git python3 linux-perf \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
