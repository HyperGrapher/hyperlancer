/** Small editing helpers for the notes textarea (docs/DECISIONS.md D6). They return the new text and caret. */
export interface Edit {
  value: string
  caret: number
}

const LIST = /^(\s*)(?:([-*+])|(\d+)\.)\s(\[[ xX]\]\s)?/

/** Enter on a list line starts the next item; Enter on an empty item ends the list. null = let the browser handle it. */
export function continueList(value: string, caret: number): Edit | null {
  const lineStart = value.lastIndexOf("\n", caret - 1) + 1
  const lineEnd = value.indexOf("\n", caret)
  const line = value.slice(lineStart, lineEnd === -1 ? value.length : lineEnd)
  const match = LIST.exec(line)
  if (!match || caret < lineStart + match[0].length) return null
  const [marker, indent, bullet, number, task] = match
  if (line.slice(marker.length).trim() === "") {
    const before = value.slice(0, lineStart)
    return {
      value: before + value.slice(lineStart + line.length),
      caret: before.length,
    }
  }
  const next = bullet
    ? `${indent}${bullet} `
    : `${indent}${Number(number) + 1}. `
  const insert = `\n${next}${task ? "[ ] " : ""}`
  return {
    value: value.slice(0, caret) + insert + value.slice(caret),
    caret: caret + insert.length,
  }
}

/** Tab inserts two spaces at the caret. */
export function indent(value: string, start: number, end: number): Edit {
  return {
    value: value.slice(0, start) + "  " + value.slice(end),
    caret: start + 2,
  }
}
