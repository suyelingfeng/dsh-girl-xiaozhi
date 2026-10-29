"""Check Git-tracked release text without printing matched private values.

This is a small publication guard, not a comprehensive secret scanner. Inspect
the findings and keep device logs, credentials and build output outside Git.
Use --history in the publication repository to check every reachable commit.
"""
import argparse
import io
from pathlib import Path
import re
import subprocess
import sys
import tarfile


PATTERNS = {
    "personal-home-path": re.compile(r"(?:[A-Za-z]:[/\\]Users[/\\](?!Public\b|Default\b|<|YOUR_|USERNAME)[^/\\\s\"']+|/home/(?!<|YOUR_|USERNAME)[\w.-]+/)", re.I),
    "private-key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "github-token": re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b"),
    "openai-key": re.compile(r"\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{40,}\b"),
    "aws-access-key": re.compile(r"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"),
    "url-password": re.compile(r"https?://[^/\s:@]+:[^/\s@]+@"),
}
BLOCKED_PARTS = {".ssh", ".aws", "build", "managed_components", "node_modules", "target"}
BLOCKED_NAMES = {".env", "sdkconfig", "sdkconfig.old", "credentials.json", "nvs.bin", "settings.local.json"}


def git(*args):
    return subprocess.check_output(["git", *args])


def check_tree(ref=None):
    # One archive per revision avoids spawning a process for every source file.
    if ref:
        archive = tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", ref)))
        entries = ((member.name, archive.extractfile(member).read())
                   for member in archive.getmembers() if member.isfile())
    else:
        paths = git("ls-files", "-z").decode("utf-8").split("\0")
        entries = ((name, Path(name).read_bytes()) for name in filter(None, paths))
    findings = []
    for name, data in entries:
        path = Path(name)
        if (path.name in BLOCKED_NAMES or set(path.parts) & BLOCKED_PARTS
                or path.suffix.lower() in {".log", ".pem", ".key", ".pfx", ".p12", ".elf", ".bin"}):
            findings.append((name, 0, "private-or-generated-file"))
        if b"\0" in data:
            continue
        for number, line in enumerate(data.decode("utf-8", errors="replace").splitlines(), 1):
            for kind, pattern in PATTERNS.items():
                if pattern.search(line):
                    findings.append((name, number, kind))
    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true")
    args = parser.parse_args()
    refs = git("rev-list", "--all").decode().splitlines() if args.history else [None]
    count = 0
    for ref in refs:
        for name, line, kind in check_tree(ref):
            print(f"{ref[:12] if ref else 'worktree'} {name}:{line}: {kind}")
            count += 1
    print(f"Privacy guard: {len(refs)} tree(s), {count} finding(s); matched values withheld.")
    return bool(count)


if __name__ == "__main__":
    sys.exit(main())
