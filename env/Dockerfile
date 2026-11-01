FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# All dependencies are installed by the same script used on a host machine.
COPY env/install_deps.sh /tmp/install_deps.sh
RUN bash /tmp/install_deps.sh && rm /tmp/install_deps.sh

WORKDIR /esmt
COPY . .

RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Debug \
    && make -j"$(nproc)"

CMD ["/bin/bash"]
