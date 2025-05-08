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


RUN echo '#!/bin/bash \n\
cd /os161/src \n\
./configure --ostree=/os161/root \n\
cd kern/conf \n\
./config ASST2 \n\
sleep 2 \n\
cd /os161/src/kern/compile/ASST2 \n\
bmake depend \n\
sleep 2
bmake \n\
sleep 1
bmake install \n\
sleep 1
cd /os161/src \n\
bmake depend \n\
sleep 2
bmake \n\
sleep 1
bmake install \n\
sleep 1
cd /os161/root \n\
' > /os161/setup_env.sh && \
chmod +x /os161/setup_env.sh

CMD ["/bin/bash"]
