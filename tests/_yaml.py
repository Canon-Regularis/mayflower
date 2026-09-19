"""A strict reader for experiments/registry.yaml, and deliberately nothing wider.

The registry is the one YAML file this repository needs to read as data. It is
about a hundred lines and its whole grammar is nine constructs, so it gets a
reader rather than a dependency: the Python layer installs nothing, and a YAML
library would be the first thing it ever needed. No line numbers are quoted
below, because this file's own edits move them.

The two workflow files are far richer than anything here handles, and
tests/test_stated_counts.py already scans them as text on purpose. Point this
at one of them and it raises. That refusal is the property that makes a
hand-written parser safe to keep: the numbers in the registry exist so a test
can contradict them, and a parser that quietly misread one would make the test
agree with whatever it misread.

What it accepts, which is exactly what the registry uses:

  - one implicit document, no `---`, no directives
  - full-line comments, outside block scalars
  - mappings indented two spaces at a time, keys matching [A-Za-z0-9_-]+
  - block sequences whose items open `- key: value`
  - plain scalars, taken verbatim to the end of the line
  - double-quoted scalars, on one line, with no escapes
  - folded block scalars `>`, whose lines all sit two deeper than the key
  - decimal integers; everything else unquoted is a string

Everything outside that raises YamlError naming the construct, and the line
wherever the fault belongs to one. Four faults are properties of the whole
input, being a tab, a stray carriage return, a missing final newline and an
empty document, and those name the file alone.

Three traps decided the shape of this, and all three are in the file today:

  - The header comments that declare the status and fold domains are shaped
    like keys, `# status: registered | ...`. A line is tested for `#` before
    anything looks for a colon. Without that the reader would not misread
    them, it would refuse the file, since `#` is not a key character and the
    line matches nothing; either way the ordering is what makes the domains
    readable at all, and the test reads them.
  - One plain scalar carries an apostrophe, and nine quoted scalars carry
    brackets. Scalar style is decided by the first character of the value and
    never by scanning the line, so neither is taken for a quoted scalar or a
    flow collection.
  - Two folded blocks contain blank lines, and one of them holds most of the
    prose numbers in the file that have drifted. A block ends on a dedent,
    never on a blank line, or that block is truncated and the checks over it
    quietly stop covering anything.

Newlines need no handling here. io.open in text mode is universal-newline by
default, so a CRLF checkout, which this file will get on Windows since there is
no .gitattributes and core.autocrlf is true, arrives as LF. A lone CR would
too, so it is refused before that can hide a torn line.
"""

from __future__ import annotations

import io
import re
from typing import Any, NamedTuple


class YamlError(Exception):
    """The input is not in the subset this reads."""


class Document(NamedTuple):
    data: dict[str, Any]
    comments: list[str]


KEY = re.compile(r"^(?P<indent> *)(?P<key>[A-Za-z0-9_-]+):(?P<rest>.*)$")
INT = re.compile(r"^[+-]?[0-9]+$")
# Numeric-looking forms this does not resolve. Reading 020000 as decimal where
# YAML 1.1 reads octal would be a silent disagreement with every other parser,
# so the shapes that mean something elsewhere are refused rather than guessed.
NUMERISH = re.compile(r"^[+-]?(0[0-9]+|0[xXoObB][0-9a-fA-F_]+|[0-9][0-9_]*[._][0-9eE_+-]*)$")

# By first character of the value. Never by scanning the line: `[` and `]` are
# inside nine of the quoted scalars, `{` and `}` inside folded prose, and `'`
# is an apostrophe at line 61.
REFUSED = {
    "&": "anchors",
    "*": "aliases",
    "!": "tags",
    "'": "single-quoted scalars",
    "[": "flow sequences",
    "{": "flow mappings",
    "|": "literal block scalars",
    # mapping() tests for a bare `>` before it ever calls scalar(), so this
    # entry can only be reached by `>-`, `>+` or `>2`, which were being stored
    # as the literal string rather than read or refused.
    ">": "folded scalars with a chomping or indentation indicator",
    "%": "directives",
    "`": "reserved indicators",
    "@": "reserved indicators",
}


def indent_of(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


class _Parser:
    """One parse. Holds the lines so the dash rewrite and the comment list can
    be shared by the two recursive halves without threading them as arguments.
    """

    def __init__(self, lines: list[str], where: str) -> None:
        self.lines = lines
        self.where = where
        self.comments: list[str] = []

    def fail(self, i: int, message: str) -> YamlError:
        return YamlError("{}:{}: {}".format(self.where, i + 1, message))

    def skippable(self, i: int) -> bool:
        """A blank line or a comment, outside a block scalar.

        Block scalars consume their own lines and never reach here, which is
        why a `#` in folded prose stays prose.
        """
        raw = self.lines[i]
        if raw.strip() == "":
            return True
        if raw.lstrip(" ").startswith("#"):
            self.comments.append(raw.lstrip(" ")[1:].strip())
            return True
        return False

    def next_content(self, i: int) -> int:
        while i < len(self.lines) and self.skippable(i):
            i += 1
        return i

    def scalar(self, value: str, i: int) -> Any:
        head = value[0]
        if head in REFUSED:
            raise self.fail(i, "{} are not supported".format(REFUSED[head]))
        if head == '"':
            end = value.find('"', 1)
            if end < 0:
                raise self.fail(i, "unterminated double-quoted scalar")
            if end != len(value) - 1:
                raise self.fail(i, "trailing text after a double-quoted scalar")
            body = value[1:-1]
            if '"' in body or "\\" in body:
                raise self.fail(i, "escapes and embedded quotes are not supported")
            return body
        if " #" in value:
            raise self.fail(i, "inline comments are not supported")
        if NUMERISH.match(value):
            raise self.fail(i, "{!r} looks like a number this does not resolve; "
                               "quote it or write a plain integer".format(value))
        if INT.match(value):
            return int(value)
        return value

    def folded(self, i: int, indent: int) -> tuple[str, int]:
        """A `>` block, from the line after the indicator.

        Every line of the block sits at `indent` + 2, and one that does not is
        refused. Ending the block on any dedent instead is the obvious rule and
        it is wrong in a way that hides the commonest possible edit: one extra
        space on the key after a block makes that key more indented than its
        siblings but still deeper than the block's own key, so it is absorbed
        into the prose and the mapping loses it. The registry has two keys in
        that position, and mistyping either left the whole suite green.

        Refusing a more-indented line also keeps the folding honest. YAML keeps
        such a line literal rather than folding it, and this strips every line
        before joining, so accepting one would silently flatten it.

        Within that shape the fold is the usual one: single newlines become
        spaces and a blank line becomes one newline. Five of the registry's
        cross-references straddle a wrapped line and only match once folded.
        """
        depth = indent + 2
        body: list[str] = []
        opened = i
        while i < len(self.lines):
            raw = self.lines[i]
            if raw.strip() == "":
                body.append("")
                i += 1
                continue
            here = indent_of(raw)
            if here <= indent:
                break
            if here != depth:
                raise self.fail(i, "a folded block is indented {}, and this line "
                                   "is at {}".format(depth, here))
            body.append(raw.strip())
            i += 1
        while body and body[-1] == "":
            body.pop()
        if not body:
            raise self.fail(opened - 1, "a folded scalar with no content")
        out: list[str] = []
        para: list[str] = []
        for line in body:
            if line == "":
                out.append(" ".join(para))
                para = []
            else:
                para.append(line)
        if para:
            out.append(" ".join(para))
        return "\n".join(out), i

    def mapping(self, i: int, indent: int) -> tuple[dict[str, Any], int]:
        out: dict[str, Any] = {}
        while i < len(self.lines):
            if self.skippable(i):
                i += 1
                continue
            raw = self.lines[i]
            here = indent_of(raw)
            if here < indent:
                break
            if here > indent:
                raise self.fail(i, "indented {} where a mapping at {} was open"
                                .format(here, indent))
            m = KEY.match(raw)
            if m is None:
                raise self.fail(i, "not a mapping key: {!r}".format(raw))
            key = m.group("key")
            if key in out:
                raise self.fail(i, "duplicate key {!r}".format(key))
            rest = m.group("rest")
            if rest and not rest.startswith(" "):
                raise self.fail(i, "a key needs a space after its colon")
            value = rest[1:] if rest else ""
            if value == "":
                nxt = self.next_content(i + 1)
                if nxt >= len(self.lines) or indent_of(self.lines[nxt]) <= indent:
                    raise self.fail(i, "key {!r} has no value and no block".format(key))
                child = indent_of(self.lines[nxt])
                if child != indent + 2:
                    raise self.fail(nxt, "a block under {!r} must be indented two"
                                    .format(key))
                if self.lines[nxt][child:child + 2] == "- ":
                    out[key], i = self.sequence(nxt, child)
                else:
                    out[key], i = self.mapping(nxt, child)
            elif value == ">":
                out[key], i = self.folded(i + 1, indent)
            else:
                out[key] = self.scalar(value, i)
                i += 1
        return out, i

    def sequence(self, i: int, indent: int) -> tuple[list[Any], int]:
        out: list[Any] = []
        while i < len(self.lines):
            if self.skippable(i):
                i += 1
                continue
            raw = self.lines[i]
            here = indent_of(raw)
            if here < indent:
                break
            if here != indent or raw[here:here + 2] != "- ":
                raise self.fail(i, "a sequence item must open '- <key>: <value>'")
            # `- ` is exactly two characters, so putting two spaces in its place
            # leaves the item's first key at the item's own indent and the whole
            # item parses as an ordinary mapping.
            self.lines[i] = " " * (here + 2) + raw[here + 2:]
            item, i = self.mapping(i, here + 2)
            out.append(item)
        return out, i


def loads(text: str, where: str = "<string>") -> Document:
    if "\t" in text:
        raise YamlError("{}: tabs are not supported".format(where))
    # CRLF first, then any carriage return left over. The order is the
    # whole point: a CRLF checkout is routine on Windows and has to parse,
    # while a lone CR is a torn line and must not.
    text = text.replace("\r\n", "\n")
    if "\r" in text:
        raise YamlError("{}: a stray carriage return".format(where))
    if not text.endswith("\n"):
        raise YamlError("{}: the file does not end in a newline".format(where))
    lines = text.split("\n")
    lines.pop()
    for n, raw in enumerate(lines):
        if raw.rstrip() != raw:
            raise YamlError("{}:{}: trailing whitespace".format(where, n + 1))
        if raw.startswith("---") or raw.startswith("..."):
            raise YamlError("{}:{}: multi-document streams are not supported"
                            .format(where, n + 1))
        if raw.startswith("%"):
            raise YamlError("{}:{}: directives are not supported".format(where, n + 1))
    p = _Parser(lines, where)
    start = p.next_content(0)
    if start >= len(lines):
        raise YamlError("{}: no document".format(where))
    if indent_of(lines[start]) != 0:
        raise YamlError("{}:{}: the document must start at column zero"
                        .format(where, start + 1))
    data, end = p.mapping(start, 0)
    end = p.next_content(end)
    if end < len(lines):
        raise YamlError("{}:{}: trailing content after the document"
                        .format(where, end + 1))
    return Document(data, p.comments)


def load(path: str) -> Document:
    # newline="" rather than the default. Universal-newline mode translates
    # CRLF and a lone CR to LF while decoding, before loads() sees either, so
    # under the default the carriage-return refusal could never fire through
    # this function and a torn line would read as two sound ones.
    return loads(io.open(path, encoding="utf-8", newline="").read(), path)
