import { useState, type FormEvent, type ReactNode } from "react"
import { toast } from "sonner"

import { ApiError } from "@/api/client"
import { useStartCrawl } from "@/api/hooks/crawls"
import type { CrawlOptions } from "@/api/types"
import { SimpleSelect } from "@/components/common/simple-select"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import {
  Collapsible,
  CollapsibleContent,
  CollapsibleTrigger,
} from "@/components/ui/collapsible"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Textarea } from "@/components/ui/textarea"

export const DEFAULT_OPTIONS: CrawlOptions = {
  startUrls: [],
  maxPages: 200,
  maxDepth: 5,
  includeSubdomains: false,
  include: [],
  exclude: [],
  respectRobots: true,
  delay: 0.5,
  concurrency: 1,
  types: { images: true, documents: true, media: false, fonts: false },
  maxFileMb: 25,
  maxTotalMb: 1024,
  strip: [],
  keepChrome: false,
  languages: [],
  wordpress: "auto",
}

const SKIP_PRESET = [
  "/cart",
  "/checkout",
  "/my-account",
  "[?&]s=",
  "/tag/",
  "/author/",
  "/feed",
  "/wp-json",
]

const lines = (value: string) =>
  value
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean)

function badPatterns(value: string): string[] {
  return lines(value).filter((pattern) => {
    try {
      new RegExp(pattern)
      return false
    } catch {
      return true
    }
  })
}

function Field({
  label,
  hint,
  error,
  children,
}: {
  label: string
  hint?: string
  error?: string
  children: ReactNode
}) {
  return (
    <div className="grid gap-1.5">
      <Label>{label}</Label>
      {children}
      {error ? (
        <p className="text-xs text-destructive">{error}</p>
      ) : hint ? (
        <p className="text-xs text-muted-foreground">{hint}</p>
      ) : null}
    </div>
  )
}

function Check({
  checked,
  onChange,
  children,
}: {
  checked: boolean
  onChange: (value: boolean) => void
  children: ReactNode
}) {
  return (
    <label className="flex items-center gap-2 text-sm">
      <Checkbox
        checked={checked}
        onCheckedChange={(value) => onChange(Boolean(value))}
      />
      {children}
    </label>
  )
}

/** Mount only while open, with the last crawl's options (or the defaults) as `initial`. */
export function NewCrawlDialog({
  siteId,
  siteUrl,
  initial,
  onClose,
  onStarted,
}: {
  siteId: number
  siteUrl: string
  initial: CrawlOptions
  onClose: () => void
  onStarted: (crawlId: number) => void
}) {
  const start = useStartCrawl(siteId)
  const [o, setO] = useState<CrawlOptions>(initial)
  const [startUrls, setStartUrls] = useState(
    (initial.startUrls.length ? initial.startUrls : [siteUrl]).join("\n")
  )
  const [include, setInclude] = useState(initial.include.join("\n"))
  const [exclude, setExclude] = useState(initial.exclude.join("\n"))
  const [strip, setStrip] = useState(initial.strip.join("\n"))
  const [languages, setLanguages] = useState(initial.languages.join(", "))
  const [owner, setOwner] = useState(false)
  const set = <K extends keyof CrawlOptions>(key: K, value: CrawlOptions[K]) =>
    setO((previous) => ({ ...previous, [key]: value }))
  const num = (
    key:
      | "maxPages"
      | "maxDepth"
      | "delay"
      | "concurrency"
      | "maxFileMb"
      | "maxTotalMb"
  ) => (
    <Input
      type="number"
      step={key === "delay" ? 0.1 : 1}
      min={0}
      value={o[key]}
      onChange={(e) => set(key, e.target.valueAsNumber)}
    />
  )

  const fields = start.error instanceof ApiError ? start.error.fields : {}
  const invalidInclude = badPatterns(include)
  const invalidExclude = badPatterns(exclude)
  const needsConfirm = !o.respectRobots && !owner
  const blocked =
    invalidInclude.length > 0 ||
    invalidExclude.length > 0 ||
    needsConfirm ||
    start.isPending

  async function submit(event: FormEvent) {
    event.preventDefault()
    try {
      const { crawl } = await start.mutateAsync({
        ...o,
        startUrls: lines(startUrls),
        include: lines(include),
        exclude: lines(exclude),
        strip: lines(strip),
        languages: languages
          .split(",")
          .map((l) => l.trim())
          .filter(Boolean),
        confirmOwnership: owner,
      })
      toast.success("Crawl queued")
      onStarted(crawl.id)
      onClose()
    } catch (e) {
      if (!(e instanceof ApiError && e.status === 422))
        toast.error(e instanceof Error ? e.message : "Could not start crawl")
    }
  }

  const patternError = (key: "include" | "exclude", bad: string[]) =>
    bad.length > 0
      ? `Invalid pattern: ${bad[0]}`
      : Object.entries(fields).find(([k]) => k.startsWith(`${key}.`))?.[1]

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-h-[90vh] overflow-y-auto sm:max-w-2xl">
        <form onSubmit={submit} className="grid gap-4">
          <DialogHeader>
            <DialogTitle>New crawl</DialogTitle>
            <DialogDescription>
              Downloads pages as Markdown plus images and files into a new
              folder. Your choices are remembered for this site.
            </DialogDescription>
          </DialogHeader>

          <Field
            label="Start URL(s)"
            hint="One per line. The sitemap is used as well."
            error={fields.startUrls}
          >
            <Textarea
              rows={2}
              value={startUrls}
              onChange={(e) => setStartUrls(e.target.value)}
              className="font-mono text-xs"
            />
          </Field>

          <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
            <Field label="Max pages" error={fields.maxPages}>
              {num("maxPages")}
            </Field>
            <Field label="Max depth" error={fields.maxDepth}>
              {num("maxDepth")}
            </Field>
            <Field label="Delay (s)" error={fields.delay}>
              {num("delay")}
            </Field>
            <Field label="Parallel" error={fields.concurrency}>
              {num("concurrency")}
            </Field>
          </div>

          <div className="grid gap-2">
            <Label>Content</Label>
            <div className="flex flex-wrap gap-x-5 gap-y-2">
              {(
                [
                  ["images", "Images"],
                  ["documents", "Documents (pdf, office, csv, zip)"],
                  ["media", "Video / audio"],
                  ["fonts", "Fonts"],
                ] as const
              ).map(([key, label]) => (
                <Check
                  key={key}
                  checked={o.types[key]}
                  onChange={(value) =>
                    set("types", { ...o.types, [key]: value })
                  }
                >
                  {label}
                </Check>
              ))}
            </div>
            <Check
              checked={o.includeSubdomains}
              onChange={(value) => set("includeSubdomains", value)}
            >
              Include subdomains
            </Check>
          </div>

          <div className="grid grid-cols-2 gap-3">
            <Field label="Max file size (MB)" error={fields.maxFileMb}>
              {num("maxFileMb")}
            </Field>
            <Field
              label="Max total size (MB)"
              hint="The crawl stops gracefully at the cap."
              error={fields.maxTotalMb}
            >
              {num("maxTotalMb")}
            </Field>
          </div>

          <div className="grid gap-2 rounded-lg border p-3">
            <Check
              checked={o.respectRobots}
              onChange={(value) => set("respectRobots", value)}
            >
              Respect robots.txt
            </Check>
            {!o.respectRobots && (
              <Check checked={owner} onChange={setOwner}>
                <span className="text-amber-600 dark:text-amber-300">
                  I own this site or may copy it.
                </span>
              </Check>
            )}
            {fields.respectRobots && (
              <p className="text-xs text-destructive">{fields.respectRobots}</p>
            )}
          </div>

          <Collapsible>
            <CollapsibleTrigger
              render={<Button type="button" variant="ghost" size="sm" />}
            >
              Advanced
            </CollapsibleTrigger>
            <CollapsibleContent className="mt-3 grid gap-4">
              <div className="grid gap-4 sm:grid-cols-2">
                <Field
                  label="Only URLs matching (regex)"
                  hint="One per line. Empty means everything."
                  error={patternError("include", invalidInclude)}
                >
                  <Textarea
                    rows={3}
                    value={include}
                    onChange={(e) => setInclude(e.target.value)}
                    className="font-mono text-xs"
                  />
                </Field>
                <Field
                  label="Skip URLs matching (regex)"
                  error={patternError("exclude", invalidExclude)}
                >
                  <Textarea
                    rows={3}
                    value={exclude}
                    onChange={(e) => setExclude(e.target.value)}
                    className="font-mono text-xs"
                  />
                  <Button
                    type="button"
                    variant="outline"
                    size="xs"
                    className="w-fit"
                    onClick={() =>
                      setExclude(
                        [...new Set([...lines(exclude), ...SKIP_PRESET])].join(
                          "\n"
                        )
                      )
                    }
                  >
                    Add: skip cart, search, tags
                  </Button>
                </Field>
              </div>
              <div className="grid gap-4 sm:grid-cols-2">
                <Field
                  label="Main content selector"
                  hint="CSS selector. Empty means automatic."
                >
                  <Input
                    value={o.mainSelector ?? ""}
                    onChange={(e) => set("mainSelector", e.target.value)}
                    className="font-mono text-xs"
                  />
                </Field>
                <Field label="Languages" hint="Prefixes such as en, de.">
                  <Input
                    value={languages}
                    onChange={(e) => setLanguages(e.target.value)}
                  />
                </Field>
              </div>
              <Field label="Remove these elements (CSS selectors)">
                <Textarea
                  rows={2}
                  value={strip}
                  onChange={(e) => setStrip(e.target.value)}
                  className="font-mono text-xs"
                />
              </Field>
              <div className="grid items-end gap-4 sm:grid-cols-2">
                <Field label="WordPress REST API">
                  <SimpleSelect
                    value={o.wordpress}
                    onChange={(value) =>
                      set(
                        "wordpress",
                        (value ?? "auto") as CrawlOptions["wordpress"]
                      )
                    }
                    options={[
                      { value: "auto", label: "Auto-detect" },
                      { value: "off", label: "Off (HTML only)" },
                      { value: "only", label: "Only (skip HTML crawl)" },
                    ]}
                  />
                </Field>
                <Check
                  checked={o.keepChrome}
                  onChange={(value) => set("keepChrome", value)}
                >
                  Keep header and footer
                </Check>
              </div>
            </CollapsibleContent>
          </Collapsible>

          <DialogFooter>
            <Button type="button" variant="outline" onClick={onClose}>
              Cancel
            </Button>
            <Button type="submit" disabled={blocked}>
              {start.isPending ? "Starting…" : "Start crawl"}
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}
