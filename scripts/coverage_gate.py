#!/usr/bin/env python3
"""Portão de cobertura dos testes unitários.

Lê o relatório JSON do gcovr produzido pelo twister e reprova a execução se
houver linha ou ramo descoberto nos módulos do projeto.

A meta é 100% em linhas e em ramos. Ramos que o ambiente de teste
comprovadamente não consegue exercitar ficam registrados em
``tests/coverage-exclusions.json``, cada um com a justificativa — o objetivo
da exclusão é que a lacuna seja uma decisão escrita, e não um número que foi
caindo sem ninguém notar.

O portão também reprova exclusão obsoleta: se um ramo excluído passar a ser
coberto, a entrada tem de sair do arquivo. Sem isso, a lista viraria um
depósito de dívidas antigas.

Uso:
    python3 scripts/coverage_gate.py twister-out/coverage.json
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

#: Raiz do repositório, deduzida da posição deste script.
REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

#: Caminho do arquivo de exclusões.
EXCLUSIONS_PATH = REPO_ROOT / "tests" / "coverage-exclusions.json"

#: Fontes que podem morar em ``src/``.
#:
#: ``src/`` é a única parte do firmware que os testes não compilam, e por isso
#: a única que o relatório de cobertura não enxerga: um módulo colocado ali
#: entra sem teste e o portão passa dizendo 100%, porque o arquivo nem aparece
#: na medição. A regra que fecha esse buraco é de organização, não de
#: cobertura: **módulo testável mora em lib/, src/ só amarra o firmware**.
SRC_ALLOWED = {"main.c"}


class FileCoverage:
    """Cobertura de um arquivo, já reduzida ao que o portão precisa saber."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.lines_total = 0
        self.lines_hit = 0
        self.branches_total = 0
        self.branches_hit = 0
        #: Linhas sem nenhuma execução.
        self.missing_lines: list[int] = []
        #: Linhas com ao menos um ramo não tomado.
        self.missing_branches: list[int] = []

    @property
    def line_percent(self) -> float:
        if self.lines_total == 0:
            return 100.0
        return 100.0 * self.lines_hit / self.lines_total

    @property
    def branch_percent(self) -> float:
        if self.branches_total == 0:
            return 100.0
        return 100.0 * self.branches_hit / self.branches_total


def load_coverage(path: pathlib.Path) -> list[FileCoverage]:
    """Interpreta o JSON do gcovr.

    :param path: Arquivo ``coverage.json`` gerado pelo twister.
    :return: Uma entrada por arquivo medido.
    """
    report = json.loads(path.read_text(encoding="utf-8"))
    result: list[FileCoverage] = []

    for entry in report.get("files", []):
        cov = FileCoverage(entry.get("file", "?"))

        for line in entry.get("lines", []):
            number = line.get("line_number", 0)
            cov.lines_total += 1
            if line.get("count", 0) > 0:
                cov.lines_hit += 1
            else:
                cov.missing_lines.append(number)

            branches = line.get("branches", [])
            taken = [b for b in branches if b.get("count", 0) > 0]
            cov.branches_total += len(branches)
            cov.branches_hit += len(taken)
            if branches and len(taken) < len(branches):
                cov.missing_branches.append(number)

        result.append(cov)

    return sorted(result, key=lambda c: c.name)


def load_exclusions() -> dict[str, dict[int, str]]:
    """Lê as exclusões registradas, se houver arquivo.

    :return: ``{arquivo: {linha: justificativa}}``.
    """
    if not EXCLUSIONS_PATH.exists():
        return {}

    raw = json.loads(EXCLUSIONS_PATH.read_text(encoding="utf-8"))
    return {
        name: {int(line): reason for line, reason in entries.items()}
        for name, entries in raw.items()
        if not name.startswith("//")
    }


def check_source_layout(measured: set[str]) -> list[str]:
    """Verifica que não há código de módulo fora do alcance da medição.

    Cobertura de 100% só significa alguma coisa se todo o código que deveria
    ser medido chegou à medição. Duas maneiras de um arquivo escapar, ambas
    silenciosas:

    1. Estar em ``src/``, que os testes não compilam.
    2. Estar em ``lib/`` mas fora da lista de fontes do ``lib/CMakeLists.txt``,
       caso em que não é compilado por ninguém — nem pelo firmware.

    :param measured: Nomes de arquivo, relativos à raiz, presentes no
                     relatório de cobertura.
    :return: Uma mensagem por problema encontrado; vazio se estiver tudo bem.
    """
    problems: list[str] = []

    lib_dir = REPO_ROOT / "lib"
    for source in sorted(lib_dir.glob("*.c")):
        name = f"lib/{source.name}"
        if name not in measured:
            problems.append(
                f"{name} existe mas não aparece no relatório de cobertura."
                f"\n    Ou falta listá-lo em lib/CMakeLists.txt — e aí ele não"
                f" está sendo compilado nem no firmware —, ou os testes não o"
                f" alcançam.")

    src_dir = REPO_ROOT / "src"
    for source in sorted(src_dir.glob("*.c")):
        if source.name not in SRC_ALLOWED:
            problems.append(
                f"src/{source.name} está fora do alcance dos testes."
                f"\n    src/ só amarra o firmware; módulo testável mora em"
                f" lib/, que é o que os testes compilam. Mova o arquivo, ou"
                f" acrescente-o a SRC_ALLOWED se ele realmente não tiver o que"
                f" testar.")

    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=pathlib.Path,
                        help="caminho do coverage.json do gcovr")
    args = parser.parse_args()

    if not args.report.exists():
        print(f"Relatório de cobertura não encontrado: {args.report}", file=sys.stderr)
        return 2

    files = load_coverage(args.report)
    if not files:
        print("Relatório de cobertura vazio: nenhum arquivo foi medido.", file=sys.stderr)
        return 2

    exclusions = load_exclusions()
    failures: list[str] = check_source_layout({cov.name for cov in files})

    print()
    print(f"{'arquivo':<24} {'linhas':>16}   {'ramos':>16}")
    print("-" * 62)

    for cov in files:
        allowed = exclusions.get(cov.name, {})
        unexplained = [n for n in cov.missing_branches if n not in allowed]
        stale = [n for n in allowed if n not in cov.missing_branches]

        print(f"{cov.name:<24} "
              f"{cov.lines_hit:>4}/{cov.lines_total:<4} ({cov.line_percent:6.2f}%)   "
              f"{cov.branches_hit:>4}/{cov.branches_total:<4} ({cov.branch_percent:6.2f}%)")

        if cov.missing_lines:
            failures.append(
                f"{cov.name}: linhas sem execução: "
                + ", ".join(str(n) for n in cov.missing_lines))

        if unexplained:
            failures.append(
                f"{cov.name}: ramos não tomados e não justificados nas linhas "
                + ", ".join(str(n) for n in unexplained)
                + f"\n    Cubra-os com um teste, ou registre a razão em "
                  f"{EXCLUSIONS_PATH.name}.")

        if stale:
            failures.append(
                f"{cov.name}: exclusões obsoletas nas linhas "
                + ", ".join(str(n) for n in stale)
                + f"\n    Esses ramos passaram a ser cobertos. Remova as entradas de "
                  f"{EXCLUSIONS_PATH.name}.")

        for line, reason in sorted(allowed.items()):
            if line in cov.missing_branches:
                print(f"    ramo excluido, linha {line}: {reason}")

    print()

    if failures:
        print("Portão de cobertura REPROVADO:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("Portão de cobertura aprovado: 100% de linhas e de ramos "
          "(fora as exclusões justificadas acima).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
