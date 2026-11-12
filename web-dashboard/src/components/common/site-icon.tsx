import { useState } from "react"
import { cn } from "cn"

/** Short, stable hash so the browser refetches the icon when the site's favicon URL changes. */
function hash(value: string): string {
  let h = 0
  for (let i = 0; i < value.length; i++) h = (h * 31 + value.charCodeAt(i)) | 0
  return (h >>> 0).toString(36)
}

/**
 * The favicon comes through our own API (/api/sites/:id/favicon): the original may sit behind an invalid
 * certificate, a redirect or hotlink protection. If it still fails, the first letter is shown.
 */
export function SiteIcon({
  site,
  className,
}: {
  site: { id: number; siteKey: string; faviconUrl: string | null }
  className?: string
}) {
  const [failed, setFailed] = useState<string | null>(null)
  const source = site.faviconUrl
    ? `/api/sites/${site.id}/favicon?v=${hash(site.faviconUrl)}`
    : null

  if (!source || failed === source) {
    return (
      <span
        className={cn(
          "flex shrink-0 items-center justify-center rounded-md bg-muted font-semibold uppercase",
          className
        )}
      >
        {site.siteKey[0]}
      </span>
    )
  }
  return (
    <img
      src={source}
      alt=""
      loading="lazy"
      onError={() => setFailed(source)}
      className={cn(
        "shrink-0 rounded-md bg-muted object-contain p-1",
        className
      )}
    />
  )
}
