# Ambiente de compilação do Vitrolinha.
#
# Imagem enxuta (~1,3 GB) em vez da zephyrprojectrtos/ci (~7,6 GB): traz apenas
# o necessário para compilar para o RP2350B e rodar os testes em native_sim.
# O CI do GitHub usa a imagem oficial; esta existe para a bancada local, onde
# baixar 7,6 GB por desenvolvedor não se paga.
#
# Construir:  docker build -t vitrolinha-zephyr docker/
# Usar:       scripts/zephyr.sh <comando>

FROM ubuntu:24.04

# Versões fixadas de propósito: o mesmo commit tem de produzir o mesmo binário
# em qualquer máquina. Subir a versão é uma alteração deliberada, com rebuild.
ARG ZEPHYR_VERSION=v4.4.2
ARG ZEPHYR_SDK_VERSION=1.0.1
ARG ZEPHYR_TOOLCHAIN=arm-zephyr-eabi

ENV DEBIAN_FRONTEND=noninteractive

# gcc-multilib/g++-multilib: native_sim é 32 bits por padrão.
# lcov + gcovr: relatório de cobertura dos testes.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        ccache \
        cmake \
        device-tree-compiler \
        file \
        g++ \
        g++-multilib \
        gcc \
        gcc-multilib \
        gcovr \
        git \
        gperf \
        lcov \
        libmagic1 \
        make \
        ninja-build \
        python3 \
        python3-dev \
        python3-venv \
        wget \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04 marca o Python do sistema como externally-managed (PEP 668),
# então o pip só funciona dentro de um ambiente virtual.
ENV VIRTUAL_ENV=/opt/venv
ENV PATH="${VIRTUAL_ENV}/bin:${PATH}"
RUN python3 -m venv "${VIRTUAL_ENV}" \
    && pip install --no-cache-dir --upgrade pip \
    && pip install --no-cache-dir \
        -r "https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/${ZEPHYR_VERSION}/scripts/requirements-base.txt" \
        -r "https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/${ZEPHYR_VERSION}/scripts/requirements-build-test.txt" \
        natsort tabulate
# natsort e tabulate vêm do requirements-run-test.txt, que traz junto opencv,
# numpy, pyocd e python-can: centenas de MB para gravar e depurar hardware,
# coisa que este contêiner não faz. O twister, porém, importa esses dois já
# no carregamento do módulo, então são listados à parte.

# Pacote mínimo do SDK (~70 MB) + apenas a toolchain ARM (~100 MB), em vez do
# bundle completo de ~2 GB com as toolchains de todas as arquiteturas.
ENV ZEPHYR_SDK_INSTALL_DIR="/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}"
RUN wget -q -O /tmp/zephyr-sdk.tar.xz \
        "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz" \
    && tar xf /tmp/zephyr-sdk.tar.xz -C /opt \
    && rm /tmp/zephyr-sdk.tar.xz \
    && "${ZEPHYR_SDK_INSTALL_DIR}/setup.sh" -t "${ZEPHYR_TOOLCHAIN}" -c

ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr

# `west zephyr-export` grava no registro de pacotes do CMake dentro do HOME,
# que some junto com o contêiner em `docker run --rm`. Apontar ZEPHYR_BASE
# resolve de forma persistente: é o que o `find_package(Zephyr ... HINTS
# $ENV{ZEPHYR_BASE})` do CMakeLists procura primeiro.
ENV ZEPHYR_BASE=/workdir/zephyr

# O workspace west (zephyr/ e modules/) mora aqui e é mantido num volume
# nomeado, para não pagar `west update` a cada execução.
WORKDIR /workdir

# git recusa operar em repositório cujo dono difere do usuário do processo, o
# que acontece sempre em bind mount vindo do Windows.
RUN git config --system --add safe.directory '*'
