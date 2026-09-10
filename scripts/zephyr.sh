#!/usr/bin/env bash
#
# Roda um comando arbitrário dentro do contêiner de compilação.
#
#   scripts/zephyr.sh west build -b zbook@p2/rp2350b/m33 vitrolinha
#   scripts/zephyr.sh bash                    # sessão interativa
#   scripts/zephyr.sh west boards | grep zbook

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

vitrolinha_require_image
vitrolinha_docker_run "$@"
