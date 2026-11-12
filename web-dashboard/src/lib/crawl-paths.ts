/** Resolves `target` (such as `../images/x.png`) against the folder of `fromFile` inside a crawl. */
export function resolveRelative(fromFile: string, target: string): string {
  const parts = fromFile.split("/").slice(0, -1)
  for (const segment of target.split("/")) {
    if (segment === "..") parts.pop()
    else if (segment !== ".") parts.push(segment)
  }
  return parts.join("/")
}

/** Rewrites relative links in page Markdown (`](../images/x.png)`) to URLs that serve the downloaded copies. */
export function withLocalImages(
  markdown: string,
  pageFile: string,
  toUrl: (path: string) => string
): string {
  return markdown.replace(/\]\((\.\.?\/[^)\s]+)\)/g, (_match, rel: string) => {
    let decoded = rel
    try {
      decoded = decodeURI(rel)
    } catch {
      /* keep the raw text */
    }
    return `](${toUrl(resolveRelative(pageFile, decoded))})`
  })
}
