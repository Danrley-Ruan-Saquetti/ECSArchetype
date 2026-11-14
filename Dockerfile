FROM gcc:14

# clang-format-22 and not Debian's default (19): .clang-format uses keys that 19 rejects
# (BinPackParameters: OnePerLine, SortIncludes as a map, BinPackLongBracedList).
RUN apt-get update \
  && apt-get install -y --no-install-recommends cmake git python3 linux-perf clang-format-22 \
  && rm -rf /var/lib/apt/lists/* \
  && ln -s /usr/bin/clang-format-22 /usr/local/bin/clang-format

WORKDIR /app
ENV OLY_TOOLCHAIN=dev
