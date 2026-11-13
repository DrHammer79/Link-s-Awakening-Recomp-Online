#!/usr/bin/env python3
"""LA-Online: take a new LAS Recompiled release WITHOUT losing our co-op work.

    python tools/merge_upstream.py BASE OURS THEIRS OUT

  BASE    the LAS Recompiled source our mod is built on (beta_5.4, commit fb45ad6d1ac2e2fc1b7a1aba08bb639e5137fbc6)
  OURS    our LA-Online source folder (this tree)
  THEIRS  the new LAS Recompiled source the original dev released
  OUT     an empty folder: the merged result is written here (OURS and THEIRS are never changed)

It is a normal git 3-way merge done in a scratch repository:
  * a file only the dev changed       -> the dev's new version is taken
  * a file only we changed            -> ours is kept
  * a file both changed, different lines -> both changes are combined
  * both changed the same lines       -> listed in MERGE-REPORT.txt with <<<<<<< ======= >>>>>>> markers
                                         (nothing is guessed; fix those by hand, then build)
  * files we added (laonline_*, ui/settings_look.h, relay/, tests/ ...) are always kept

Never copies game files: folders named build/, keys, *.nsp, *.xci, *.nca, prod.keys, title.keys are skipped.
Needs git on PATH (MSYS2: pacman -S git).
"""
import os, shutil, subprocess, sys, tempfile

SKIP_DIRS = {".git", "build", "venv", "__pycache__", "out", "dist"}
SKIP_EXT = {".nsp", ".xci", ".nca", ".keys", ".o", ".obj", ".exe", ".dll", ".pdb"}
SKIP_NAMES = {"prod.keys", "title.keys"}


def copy_tree(src, dst):
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        rel = os.path.relpath(root, src)
        for f in files:
            if f in SKIP_NAMES or os.path.splitext(f)[1].lower() in SKIP_EXT:
                continue
            os.makedirs(os.path.join(dst, rel), exist_ok=True)
            shutil.copy2(os.path.join(root, f), os.path.join(dst, rel, f))


def clear_worktree(repo):
    for e in os.listdir(repo):
        if e == ".git":
            continue
        p = os.path.join(repo, e)
        shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)


def git(repo, *args, check=True):
    return subprocess.run(["git", "-C", repo, *args], check=check, capture_output=True, text=True)


def snapshot(repo, src, msg):
    clear_worktree(repo)
    copy_tree(src, repo)
    git(repo, "add", "-A")
    git(repo, "-c", "user.name=merge", "-c", "user.email=merge@local", "commit", "-q", "--allow-empty", "-m", msg)


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    base, ours, theirs, out = (os.path.abspath(a) for a in sys.argv[1:])
    for name, p in (("BASE", base), ("OURS", ours), ("THEIRS", theirs)):
        if not os.path.isdir(p):
            print(f"{name} folder not found: {p}")
            return 2
    if os.path.exists(out) and os.listdir(out):
        print(f"OUT must be empty or missing: {out}")
        return 2
    repo = tempfile.mkdtemp(prefix="lao-merge-")
    git(repo, "init", "-q")
    git(repo, "config", "core.autocrlf", "false")
    git(repo, "config", "merge.renameLimit", "20000")
    snapshot(repo, base, "upstream base")
    git(repo, "branch", "-M", "base")
    git(repo, "checkout", "-q", "-b", "ours")
    snapshot(repo, ours, "LA-Online")
    git(repo, "checkout", "-q", "-b", "theirs", "base")
    snapshot(repo, theirs, "new upstream")
    git(repo, "checkout", "-q", "ours")
    changed_up = git(repo, "diff", "--name-status", "base", "theirs").stdout.splitlines()
    changed_us = git(repo, "diff", "--name-only", "base", "ours").stdout.splitlines()
    r = git(repo, "-c", "user.name=merge", "-c", "user.email=merge@local", "merge", "--no-ff", "--no-commit",
            "-X", "diff-algorithm=histogram", "-X", "ignore-cr-at-eol", "theirs", check=False)
    conflicts = git(repo, "diff", "--name-only", "--diff-filter=U").stdout.split()
    os.makedirs(out, exist_ok=True)
    for e in os.listdir(repo):
        if e == ".git":
            continue
        p = os.path.join(repo, e)
        (shutil.copytree if os.path.isdir(p) else shutil.copy2)(p, os.path.join(out, e))
    us = set(changed_us)
    lines = ["LA-Online upstream merge report", "",
             f"BASE   {base}", f"OURS   {ours}", f"THEIRS {theirs}", "",
             f"Files the original dev changed: {len(changed_up)}",
             f"Files LA-Online changed or added: {len(changed_us)}",
             f"Needs a hand fix (both edited the same lines): {len(conflicts)}", ""]
    if conflicts:
        lines += ["== FIX THESE BY HAND (search for <<<<<<< ) =="] + ["  " + c for c in conflicts] + [""]
    lines += ["== dev's changes and what happened =="]
    for l in changed_up:
        st, _, path = l.partition("\t")
        path = path.split("\t")[-1]
        how = "CONFLICT - fix by hand" if path in conflicts else ("merged with ours" if path in us else "taken from dev")
        lines.append(f"  {st:<5} {path:<70} {how}")
    lines += ["", "git merge output:", r.stdout.strip(), r.stderr.strip()]
    with open(os.path.join(out, "MERGE-REPORT.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    shutil.rmtree(repo, ignore_errors=True)
    print("\n".join(lines[:lines.index("git merge output:")]))
    print(f"\nMerged source: {out}\nFull report:   {os.path.join(out, 'MERGE-REPORT.txt')}")
    return 1 if conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
