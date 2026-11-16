#!/usr/bin/env bash
# Format check for the files clang-format and clang-tidy never look at.
#
#   C and C++   clang-format --dry-run -Werror, over every tracked source file
#   shell       bash -n, and no leading tabs (recipes aside, these files are indented with spaces)
#   the rest    no tabs, no trailing whitespace, LF endings, a newline at the end
#
# Everything the check looks at is tracked by git, so build output and .scratch stay out of it.
set -eu
cd "$(dirname "$0")/.."

fail=0
note() {
  echo "  $1"
  fail=1
}

# ---- enumeration ----------------------------------------------------------
# The three sections below enumerate tracked files with git ls-files. A failed
# git (a runner that distrusts the checkout) or a wrong working directory used
# to degrade silently into an empty set, and the check reported OK on checking
# nothing -- every list is checked for both, and either aborts the run.
tracked() {
  local list
  list=$(git ls-files "$@") || {
    echo "  git ls-files failed; refusing to check an unknown set" >&2
    exit 1
  }
  if [ -z "$list" ]; then
    echo "  tracked-file enumeration came back empty; refusing" >&2
    exit 1
  fi
  printf '%s\n' "$list"
}

# ---- C and C++ ------------------------------------------------------------
# Command substitution, not process substitution: a failing enumeration has to
# fail the script, and mapfile cannot see an exit status across <(...).
cxx_list=$(tracked '*.c' '*.cc' '*.cpp' '*.h' '*.hpp')
mapfile -t cxx <<< "$cxx_list"
echo "clang-format: ${#cxx[@]} files"
if ! diff=$(clang-format --dry-run -Werror "${cxx[@]}" 2>&1); then
  printf '%s\n' "$diff" | sed 's/^/  /'
  note 'clang-format found differences'
fi

# ---- whitespace and line endings -----------------------------------------
text_list=$(tracked | grep -vE '\.(png|jpg|gif|zip|ko|apk|jar|so)$')
mapfile -t text <<< "$text_list"
echo "whitespace: ${#text[@]} files"
for f in "${text[@]}"; do
  [ -f "$f" ] || continue # gitlinks name directories, not project text files
  # Kernel sources and Makefile recipes are indented with tabs by their own rules, so the tab
  # check only covers the file types this repository writes itself.
  case "$f" in
  *.sh | *.yml | *.yaml | *.prop | *.md | *.txt | *.cmake | CMakeLists.txt | */CMakeLists.txt)
    grep -qP '\t' "$f" && note "$f: contains a tab"
    case "$f" in
    *.md)
      head -1 "$f" | grep -qE '^# ' || note "$f: first line is not a markdown heading"
      grep -qE '^#+[^ #]' "$f" && note "$f: a heading has no space after its hashes"
      ;;
    esac
    ;;
  esac
  grep -qE ' +$' "$f" && note "$f: trailing whitespace"
  grep -qU $'\r' "$f" && note "$f: CRLF line ending"
  [ -s "$f" ] && [ -n "$(tail -c 1 "$f")" ] && note "$f: no newline at end of file"
done

# ---- shell ---------------------------------------------------------------
sh_list=$(tracked '*.sh')
mapfile -t sh <<< "$sh_list"
echo "shell: ${#sh[@]} files"
for f in "${sh[@]}"; do bash -n "$f" || note "$f: syntax"; done

echo
if [ "$fail" = 0 ]; then
  echo 'format: OK'
else
  echo 'format: FAILED'
fi
exit "$fail"
