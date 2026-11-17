#!/usr/bin/env python3
"""Tabela de pilha por thread, a partir do console da placa.

O consumo de pilha por thread é critério de avaliação do trabalho (issue #13),
e o número só existe na placa: o analisador de threads do Zephyr depende de
``!ARCH_POSIX`` e não roda no simulador. Este script lê o console capturado
de uma sessão na placa e devolve a tabela em Markdown, já com o veredito de
cada folga — é ela que entra em ``docs/medicoes/pilhas-por-thread.md``.

Entende três formatos, em qualquer mistura, no mesmo arquivo:

* o relatório periódico do ``CONFIG_THREAD_ANALYZER_AUTO``;
* ``kernel thread stacks``, do shell — inclui a pilha de interrupção;
* ``kernel thread list``, do shell — o único que traz a **prioridade**.

De cada thread fica a amostra de maior uso. O analisador mede marca d'água
(quantos bytes do fim da pilha ainda guardam o padrão de preenchimento), que
só sobe; ficar com o maior valor cobre também um console com várias rodadas.

Uso:
    python3 scripts/stack_report.py console.log
    python3 scripts/stack_report.py --check console.log   # reprova pilha apertada

Saída 0 com a tabela; 1 com ``--check`` e alguma pilha apertada ou estourada;
2 quando o console não tem nenhuma linha reconhecível.
"""

from __future__ import annotations

import argparse
import dataclasses
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Política de folga. A justificativa está em docs/medicoes/pilhas-por-thread.md;
# mudar um número aqui sem mudar lá deixa o relatório mentindo.
# ---------------------------------------------------------------------------

#: Fração mínima da pilha que tem de sobrar acima da marca d'água. A marca é
#: o pior caso dos caminhos EXERCITADOS — um limite inferior do pior caso
#: real, não ele.
MIN_FREE_FRACTION = 0.25

#: Folga mínima absoluta, para pilhas pequenas em que 25% é pouco. Cobre uma
#: interrupção chegando exatamente no ponto mais fundo da pilha, que a
#: medição pode não ter flagrado: o Cortex-M33 com FPU empilha na pilha da
#: thread interrompida um quadro estendido de 104 B, mais até 4 B de
#: alinhamento a 8. Os handlers em si rodam na pilha de interrupção.
MIN_FREE_BYTES = 128

#: Acima disto de folga, e com uso abaixo de metade, a pilha é candidata a
#: encolher. Não reprova: sobra custa RAM, não correção.
SPARE_FREE_BYTES = 1024
SPARE_USED_FRACTION = 0.5

# ---------------------------------------------------------------------------
# Onde se ajusta cada pilha. Thread nova entra aqui quando nascer — sem isso
# a tabela sai com "?" na coluna, o que é o lembrete.
# ---------------------------------------------------------------------------

KNOBS = {
    "ISR": "`CONFIG_ISR_STACK_SIZE`",
    "main": "`CONFIG_MAIN_STACK_SIZE`",
    "idle": "`CONFIG_IDLE_STACK_SIZE`",
    "sysworkq": "`CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE`",
    "logging": "`CONFIG_LOG_PROCESS_THREAD_STACK_SIZE`",
    "shell_uart": "`CONFIG_SHELL_STACK_SIZE`",
    "input": "`CONFIG_INPUT_THREAD_STACK_SIZE`",
    "lvgl": "`CONFIG_LV_Z_LVGL_WORKQUEUE_STACK_SIZE`",
    "thread_analyzer": "`CONFIG_THREAD_ANALYZER_AUTO_STACK_SIZE`",
    "card": "`CARD_MONITOR_STACK_SIZE` (`src/card.c`)",
}

# ---------------------------------------------------------------------------
# Formatos de saída do Zephyr v4.4.2
# ---------------------------------------------------------------------------

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

#: subsys/debug/thread_analyzer/thread_analyzer.c:
#:   " %-20s: STACK: unused %zu usage %zu / %zu (%zu %%); CPU: %u %%"
#:   " %-20s: unused %zu usage %zu / %zu (%zu %%)"            (ISR0)
ANALYZER_RE = re.compile(
    r"^\s*(?P<name>\S.*?)\s*:\s+(?:STACK:\s+)?"
    r"unused (?P<unused>\d+) usage (?P<used>\d+) / (?P<size>\d+) \(\d+ %\)"
)

#: subsys/shell/modules/kernel_service/thread/stacks.c:
#:   "%p %-Ns (real size %4zu):\tunused %4zu\tusage %4zu / %4zu (%2u %%)"
#:   "%p IRQ %02d %s(real size %4zu):\tunused ..."
STACKS_RE = re.compile(
    r"^\s*0x[0-9a-fA-F]+\s+(?P<name>\S.*?)\s*\(real size\s+\d+\):\s*"
    r"unused\s+(?P<unused>\d+)\s+usage\s+(?P<used>\d+)\s*/\s*(?P<size>\d+)"
)

#: subsys/shell/modules/kernel_service/thread/list.c — um bloco por thread:
#:   "%s%p %-10s"                            (o "*" marca a thread corrente)
#:   "\toptions: 0x%x, priority: %d timeout: %lld"
#:   "\tstack size %zu, unused %zu, usage %zu / %zu (%u %%)"
LIST_HEAD_RE = re.compile(r"^\s*\*?\s*0x[0-9a-fA-F]+\s+(?P<name>\S.*?)\s*$")
LIST_PRIO_RE = re.compile(r"priority:\s*(?P<prio>-?\d+)")
LIST_STACK_RE = re.compile(
    r"stack size \d+, unused (?P<unused>\d+), usage (?P<used>\d+) / (?P<size>\d+)"
)

#: O que o log diferido escreve quando o buffer transbordou. Com
#: LOG_MODE_OVERFLOW o descarte é das mensagens MAIS ANTIGAS — um bloco do
#: analisador pode ter perdido linhas sem nenhuma outra pista.
DROPPED_RE = re.compile(r"messages? dropped", re.IGNORECASE)

ISR_RE = re.compile(r"^(?:ISR\s*\d+|IRQ\s*\d+)$")


@dataclasses.dataclass
class Stack:
    """Pior amostra vista de uma pilha."""

    name: str
    size: int
    used: int
    priority: int | None = None

    @property
    def free(self) -> int:
        return self.size - self.used

    def verdict(self) -> str:
        if self.free <= 0:
            return "**ESTOURO** — a marca chegou ao fim"
        if self.free < max(self.size * MIN_FREE_FRACTION, MIN_FREE_BYTES):
            return "**apertada** — aumentar"
        if self.free > SPARE_FREE_BYTES and self.used < self.size * SPARE_USED_FRACTION:
            return "sobra — candidata a encolher"
        return "ok"

    def failing(self) -> bool:
        return self.verdict().startswith("**")


def canonical(name: str) -> str:
    """Um nome por pilha, venha do analisador ("ISR0") ou do shell ("IRQ 00")."""
    name = name.strip()
    return "ISR" if ISR_RE.match(name) else name


def parse(lines: list[str]) -> tuple[dict[str, Stack], int]:
    """Lê o console e devolve as pilhas e quantos avisos de descarte havia."""
    stacks: dict[str, Stack] = {}
    dropped = 0
    current: str | None = None  # thread aberta num bloco do `kernel thread list`

    def record(name: str, size: int, used: int) -> None:
        name = canonical(name)
        seen = stacks.get(name)
        if seen is None:
            stacks[name] = Stack(name, size, used)
        elif used > seen.used:
            seen.size, seen.used = size, used

    for raw in lines:
        line = ANSI_RE.sub("", raw).rstrip("\r\n")

        if DROPPED_RE.search(line):
            dropped += 1
            continue

        if "PRIV_STACK" in line:
            continue

        if m := STACKS_RE.match(line):
            record(m["name"], int(m["size"]), int(m["used"]))
            current = None
            continue

        if m := LIST_HEAD_RE.match(line):
            current = canonical(m["name"])
            continue

        if current is not None:
            if m := LIST_PRIO_RE.search(line):
                stacks.setdefault(current, Stack(current, 0, 0)).priority = int(m["prio"])
                continue
            if m := LIST_STACK_RE.search(line):
                record(current, int(m["size"]), int(m["used"]))
                current = None
                continue

        if m := ANALYZER_RE.match(line):
            record(m["name"], int(m["size"]), int(m["used"]))
            current = None

    # Prioridade sem amostra de pilha não vira linha da tabela.
    stacks = {k: v for k, v in stacks.items() if v.size > 0}
    return stacks, dropped


def order(stack: Stack) -> tuple[int, int, str]:
    """Interrupção primeiro, depois da maior prioridade para a menor."""
    if stack.name == "ISR":
        return (0, 0, "")
    if stack.priority is None:
        return (2, 0, stack.name)
    return (1, stack.priority, stack.name)


def render(stacks: dict[str, Stack]) -> str:
    rows = [
        "| Thread | Prioridade | Pilha (B) | Pico (B) | Folga (B) | Uso | Onde se ajusta | Veredito |",
        "|---|--:|--:|--:|--:|--:|---|---|",
    ]
    for s in sorted(stacks.values(), key=order):
        if s.name == "ISR":
            prio = "interrupção"
        elif s.priority is None:
            prio = "?"
        else:
            prio = str(s.priority)
        rows.append(
            f"| `{s.name}` | {prio} | {s.size} | {s.used} | {s.free} "
            f"| {100 * s.used // s.size} % | {KNOBS.get(s.name, '?')} | {s.verdict()} |"
        )
    return "\n".join(rows)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    parser.add_argument("log", type=Path, nargs="?", help="console capturado (padrão: stdin)")
    parser.add_argument(
        "--check", action="store_true", help="sai com 1 se alguma pilha estiver apertada"
    )
    args = parser.parse_args(argv)

    # A tabela tem acento e travessão; o console do Windows não é UTF-8.
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

    if args.log is None:
        lines = sys.stdin.read().splitlines()
    else:
        lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()

    stacks, dropped = parse(lines)

    if not stacks:
        print(
            "Nenhuma linha de pilha reconhecida. Rode na placa, com "
            "CONFIG_THREAD_ANALYZER, e capture o relatório periódico ou a "
            "saída de `kernel thread stacks` e `kernel thread list`.",
            file=sys.stderr,
        )
        return 2

    print(render(stacks))

    if dropped:
        print(
            f"\nAVISO: o console acusa {dropped} descarte(s) de mensagens de log. "
            "Uma thread pode ter ficado fora da tabela; confira a contagem com "
            "`kernel thread list`.",
            file=sys.stderr,
        )

    missing = sorted(s.name for s in stacks.values() if s.name != "ISR" and s.priority is None)
    if missing:
        print(
            "\nSem prioridade (falta `kernel thread list` no console): " + ", ".join(missing),
            file=sys.stderr,
        )

    if args.check and any(s.failing() for s in stacks.values()):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
