FROM gcc:14

# clang-format-22 e não o padrão do Debian (19): o .clang-format usa chaves que o 19 rejeita
# (BinPackParameters: OnePerLine, SortIncludes como mapa, BinPackLongBracedList).
RUN apt-get update \
  && apt-get install -y --no-install-recommends cmake git python3 linux-perf clang-format-22 \
  && rm -rf /var/lib/apt/lists/* \
  && ln -s /usr/bin/clang-format-22 /usr/local/bin/clang-format

WORKDIR /app
