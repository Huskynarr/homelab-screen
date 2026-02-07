function trim(s) {
    sub(/^[ \t]+/, "", s)
    sub(/[ \t]+$/, "", s)
    return s
}

function repeat(ch, n, out, i) {
    out = ""
    for (i = 0; i < n; i++) out = out ch
    return out
}

function is_align_cell(cell) {
    return cell ~ /^:?-{1,}:?$/
}

function row_is_alignment(r, c, cols, cell) {
    cols = row_cols[r]
    if (cols == 0) return 0
    for (c = 1; c <= cols; c++) {
        cell = cells[r, c]
        if (!is_align_cell(cell)) return 0
    }
    return 1
}

function clear_table(    r, c) {
    for (r = 1; r <= tcount; r++) {
        delete row_cols[r]
        for (c = 1; c <= max_cols; c++) delete cells[r, c]
    }
    tcount = 0
    max_cols = 0
}

function flush_table(    r, c, s, parts, cols, cell, line, width, left, right, dashes) {
    if (tcount == 0) return

    # Parse rows into cells and determine max columns.
    for (r = 1; r <= tcount; r++) {
        s = table_lines[r]
        sub(/^[[:space:]]*\|/, "", s)
        sub(/\|[[:space:]]*$/, "", s)
        cols = split(s, parts, /\|/)
        row_cols[r] = cols
        if (cols > max_cols) max_cols = cols
        for (c = 1; c <= cols; c++) {
            cells[r, c] = trim(parts[c])
        }
    }

    # Compute display widths from non-alignment rows.
    for (c = 1; c <= max_cols; c++) maxw[c] = 3
    for (r = 1; r <= tcount; r++) {
        if (row_is_alignment(r)) continue
        for (c = 1; c <= max_cols; c++) {
            cell = cells[r, c]
            width = length(cell)
            if (width > maxw[c]) maxw[c] = width
        }
    }

    # Emit formatted table.
    for (r = 1; r <= tcount; r++) {
        line = "|"
        for (c = 1; c <= max_cols; c++) {
            cell = cells[r, c]
            if (row_is_alignment(r)) {
                left = (cell ~ /^:/)
                right = (cell ~ /:$/)
                if (left && right) {
                    dashes = repeat("-", maxw[c] - 2)
                    cell = ":" dashes ":"
                } else if (left) {
                    dashes = repeat("-", maxw[c] - 1)
                    cell = ":" dashes
                } else if (right) {
                    dashes = repeat("-", maxw[c] - 1)
                    cell = dashes ":"
                } else {
                    cell = repeat("-", maxw[c])
                }
            } else {
                cell = sprintf("%-" maxw[c] "s", cell)
            }
            line = line " " cell " |"
        }
        print line
    }

    clear_table()
}

BEGIN {
    clear_table()
}

/^[[:space:]]*\|.*\|[[:space:]]*$/ {
    tcount++
    table_lines[tcount] = $0
    next
}

{
    flush_table()
    print $0
}

END {
    flush_table()
}
