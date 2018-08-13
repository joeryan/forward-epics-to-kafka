FROM ubuntu:18.04

RUN apt-get update && \
    apt-get --no-install-recommends -y install cmake g++ git python-pip tzdata vim-common && \
    apt-get -y autoremove && \
    apt-get clean all && \
    rm -rf /var/lib/apt/lists/*

RUN pip install --upgrade pip==9.0.3 && \
    pip install conan && \
    rm -rf /root/.cache/pip/*

# Force conan to create .conan directory and profile
RUN conan profile new default

# Replace the default profile and remotes with the ones from our Ubuntu build node
ADD "https://raw.githubusercontent.com/ess-dmsc/docker-ubuntu18.04-build-node/master/files/registry.txt" "/root/.conan/registry.txt"
ADD "https://raw.githubusercontent.com/ess-dmsc/docker-ubuntu18.04-build-node/master/files/default_profile" "/root/.conan/profiles/default"

RUN mkdir forwarder
RUN cd forwarder

ADD src/ ../forwarder_src/src
ADD conan/ ../forwarder_src/conan/
ADD cmake/ ../forwarder_src/cmake/
ADD CMakeLists.txt ../forwarder_src
ADD Doxygen.conf ../forwarder_src

RUN cd forwarder && \
    cmake ../forwarder_src && \
    make -j8 VERBOSE=1

ADD docker_launch.sh /
CMD ["./docker_launch.sh"]
