#!/usr/bin/env python3
"""Rejects includes that cross the boundaries between Oly modules.

Usage: python3 -I tools/check_boundaries.py --root <repo> --manifest <json>
Exits 0 with no violations; exits 1 printing "<file>:<line>: <reason>" per violation.
"""
import argparse
import json
import os
import re
import sys

IGNORED_DIRECTORIES = {"build", "node_modules", ".git", ".superpowers", "dist", "dist-tests"}
CPP_EXTENSIONS = (".hpp", ".cpp")
TS_EXTENSION = ".ts"

INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s+"oly/([a-z_]+)/([^"]*)"')
RUNTIME_PREFIX = "engine/runtime/"

TS_IMPORT_PATTERN = re.compile(r"""(?:\bfrom\s+|\bimport\s*\(?\s*)["'](\.{1,2}/[^"']*|@oly/[^"']*)["']""")


def load_owners(manifest):
    owners = [(entry["directory"].rstrip("/") + "/", entry["name"], set(entry["depends"])) for entry in manifest["modules"] + manifest["games"]]
    # Longest prefix first: owner_of keeps the first match, which is therefore the most specific directory.
    return sorted(owners, key=lambda owner: len(owner[0]), reverse=True)


def owner_of(relative_path, owners):
    for directory, name, depends in owners:
        if relative_path.startswith(directory):
            return name, depends
    return None, set()


def source_files(root):
    for current, directories, files in os.walk(root):
        directories[:] = [directory for directory in directories if directory not in IGNORED_DIRECTORIES]
        for file_name in files:
            if file_name.endswith(CPP_EXTENSIONS) or file_name.endswith(TS_EXTENSION):
                yield os.path.join(current, file_name)


def check_cpp_file(path, relative_path, owners):
    violations = []
    owner, depends = owner_of(relative_path, owners)
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line_number, line in enumerate(handle, start=1):
            match = INCLUDE_PATTERN.match(line)
            if match is None:
                continue
            included_module, included_path = match.group(1), match.group(2)
            if owner is not None and included_module != owner and included_module not in depends:
                violations.append(f"{relative_path}:{line_number}: {owner} includes oly/{included_module} without declaring {included_module} in DEPENDS")
            inside_included_module = relative_path.startswith(f"{RUNTIME_PREFIX}{included_module}/")
            if included_path.startswith("detail/") and not inside_included_module:
                violations.append(f"{relative_path}:{line_number}: oly/{included_module}/detail is internal to module {included_module}")

    return violations


def package_root(path, root):
    current = os.path.dirname(path)
    while os.path.abspath(current) != os.path.abspath(root):
        if os.path.exists(os.path.join(current, "package.json")):
            return current
        current = os.path.dirname(current)
    return None


def check_ts_file(path, relative_path, root):
    violations = []
    inside_engine = relative_path.startswith("engine/")
    inside_core = relative_path.startswith("engine/environments/web/clients/typescript/core/")
    package_directory = package_root(path, root)
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line_number, line in enumerate(handle, start=1):
            for match in TS_IMPORT_PATTERN.finditer(line):
                specifier = match.group(1)
                if specifier.startswith("@oly/core/internal") and not inside_engine:
                    violations.append(f"{relative_path}:{line_number}: @oly/core/internal can only be used inside engine/")
                if specifier.startswith("@oly/web-host") and inside_core:
                    violations.append(f"{relative_path}:{line_number}: @oly/core cannot depend on @oly/web-host")
                if specifier.startswith(".") and package_directory is not None:
                    resolved = os.path.abspath(os.path.join(os.path.dirname(path), specifier))
                    if os.path.commonpath([resolved, os.path.abspath(package_directory)]) != os.path.abspath(package_directory):
                        violations.append(f"{relative_path}:{line_number}: relative import crosses packages; use the package name")

    return violations


def check(root, manifest):
    owners = load_owners(manifest)
    violations = []
    for path in source_files(root):
        relative_path = os.path.relpath(path, root).replace(os.sep, "/")
        if path.endswith(TS_EXTENSION):
            violations.extend(check_ts_file(path, relative_path, root))
        else:
            violations.extend(check_cpp_file(path, relative_path, owners))

    return sorted(violations)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--manifest", required=True)
    arguments = parser.parse_args(argv)

    with open(arguments.manifest, encoding="utf-8") as handle:
        manifest = json.load(handle)

    violations = check(arguments.root, manifest)
    for violation in violations:
        print(violation)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
