# Stage 1: Ubuntu 16.04 for os161-toolchain installation
FROM ubuntu:16.04 as toolchain-stage

# Set environment variables
ENV HOME=/os161
ENV DEBIAN_FRONTEND=noninteractive

# Update package lists and install dependencies
RUN apt-get update && \
    apt-get install -y \
      software-properties-common \
      build-essential \
      wget \
      curl && \
    rm -rf /var/lib/apt/lists/*

# Add the os161-toolchain repository and install
RUN add-apt-repository ppa:ops-class/os161-toolchain && \
    apt-get update && \
    apt-get install -y os161-toolchain && \
    rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /os161

# Create setup script
RUN echo "#!/bin/bash \n\
cd /os161/src \n\
./configure --ostree=/os161/root \n\
cd kern/conf \n\
./config ASST3 \n\
sleep 2 \n\
cd /os161/src/kern/compile/ASST3 \n\
bmake depend \n\
sleep 2 \n\
bmake \n\
sleep 1 \n\
bmake install \n\
sleep 1 \n\
bmake depend \n\
sleep 2 \n\
bmake \n\
sleep 1 \n\
bmake install \n\
sleep 1 \n\
cd /os161/root \n\
cp /os161/src/sys161.conf /os161/root/ \n\
" > /os161/setup_env.sh && \
chmod +x /os161/setup_env.sh

# Stage 2: Ubuntu 20.04 with migrated toolchain
FROM ubuntu:20.04 as final-stage

# Set environment variables
ENV HOME=/os161
ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin"

# Install necessary packages for Ubuntu 20.04
RUN apt-get update && \
    apt-get install -y \
      build-essential \
      gcc \
      g++ \
      make \
      libc6-dev \
      binutils \
      gdb \
      wget \
      curl \
      vim \
      git \
      libncurses5 \
      libncurses5-dev && \
    rm -rf /var/lib/apt/lists/*

# Copy the essential os161-toolchain binaries from the first stage
COPY --from=toolchain-stage /usr/bin/os161-* /usr/bin/
COPY --from=toolchain-stage /usr/bin/bmake /usr/bin/
COPY --from=toolchain-stage /usr/bin/sys161 /usr/bin/

# Set up the working directory
WORKDIR /os161

# Copy the setup script
COPY --from=toolchain-stage /os161/setup_env.sh /os161/setup_env.sh

# Make binaries executable
RUN chmod +x /usr/bin/os161-* /usr/bin/bmake /usr/bin/sys161

# Create a script to verify the installation
RUN echo "#!/bin/bash \n\
echo 'Checking os161 toolchain installation...' \n\
echo 'Available os161 tools:' \n\
ls -la /usr/bin/os161-* \n\
echo 'Checking bmake:' \n\
which bmake && bmake --version \n\
echo 'Checking sys161:' \n\
which sys161 && sys161 --version \n\
echo 'Checking os161-gcc:' \n\
which os161-gcc && os161-gcc --version \n\
" > /os161/check_tools.sh && \
chmod +x /os161/check_tools.sh

# Set the default command
CMD ["/bin/bash"]
