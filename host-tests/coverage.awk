# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only

# Summarises `gcov -n` output for `make -C host-tests coverage`: keeps the best line
# coverage seen for each firmware source or header, across all translation units.
/^File / {
  name = $2
  gsub(/'/, "", name)
  keep = (name ~ /^\.\.\/(src|include)\//)
}

/^Lines executed:/ && keep {
  percent = $0
  sub(/^Lines executed:/, "", percent)
  split(percent, parts, "%")
  total = percent
  sub(/.* of /, "", total)
  if (!(name in best) || parts[1] + 0 > best[name]) {
    best[name] = parts[1] + 0
    lines[name] = total
  }
}

END {
  for (name in best) printf "%6.1f%%  %5s lines  %s\n", best[name], lines[name], name
}
