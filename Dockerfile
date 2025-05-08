FROM ubuntu:16.04
ENV HOME=/os161

RUN apt-get update &&\
    apt-get install -y \
      software-properties-common \
      build-essential \
      python3 

RUN add-apt-repository ppa:ops-class/os161-toolchain && \
      apt-get update && \
      apt-get install -y os161-toolchain

WORKDIR /os161

CMD ["/bin/bash"]
