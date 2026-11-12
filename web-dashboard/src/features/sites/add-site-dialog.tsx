import { Link, useNavigate } from "@tanstack/react-router"
import { ArrowRightIcon, TriangleAlertIcon } from "lucide-react"
import { useEffect, useState, type FormEvent } from "react"
import { toast } from "sonner"

import { ApiError } from "@/api/client"
import { useCategories, useCheckUrl, useCreateSite } from "@/api/hooks/sites"
import type { SiteStatus } from "@/api/types"
import { SimpleSelect } from "@/components/common/simple-select"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
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
import { STATUSES } from "@/lib/domain"

function useDebounced<T>(value: T, delay = 350): T {
  const [debounced, setDebounced] = useState(value)
  useEffect(() => {
    const timer = setTimeout(() => setDebounced(value), delay)
    return () => clearTimeout(timer)
  }, [value, delay])
  return debounced
}

export function AddSiteDialog({
  open,
  onOpenChange,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
}) {
  const navigate = useNavigate()
  const [url, setUrl] = useState("")
  const [name, setName] = useState("")
  const [status, setStatus] = useState<string | null>("none")
  const [category, setCategory] = useState<string | null>(null)
  const [tags, setTags] = useState("")
  const [fetchNow, setFetchNow] = useState(true)
  const [allowPrivate, setAllowPrivate] = useState(false)
  const [allowDuplicate, setAllowDuplicate] = useState(false)

  const debouncedUrl = useDebounced(url.trim())
  const check = useCheckUrl(debouncedUrl)
  const categories = useCategories().data ?? []
  const create = useCreateSite()

  const info = check.data
  const typing = url.trim() !== debouncedUrl
  const invalid =
    check.error instanceof ApiError ? check.error.fields.url : undefined
  const blocked = info?.blockedReason === "private_network" && !allowPrivate
  const duplicate = Boolean(info?.existing) && !allowDuplicate
  const canSubmit =
    Boolean(info) && !typing && !blocked && !duplicate && !create.isPending

  function reset() {
    setUrl("")
    setName("")
    setTags("")
    setAllowPrivate(false)
    setAllowDuplicate(false)
  }

  function submit(event: FormEvent) {
    event.preventDefault()
    create.mutate(
      {
        url,
        name: name.trim() || undefined,
        status: (status ?? "none") as SiteStatus,
        categoryId: category ? Number(category) : null,
        tags: tags
          .split(",")
          .map((tag) => tag.trim())
          .filter(Boolean),
        fetch: fetchNow,
        allowPrivateNet: allowPrivate,
        allowDuplicate,
      },
      {
        onSuccess: ({ site }) => {
          toast.success(
            `${site.siteKey} added${fetchNow ? ". Fetching its profile…" : ""}`
          )
          onOpenChange(false)
          reset()
          navigate({ to: "/sites/$siteId", params: { siteId: site.id } })
        },
        onError: (error) => toast.error(error.message),
      }
    )
  }

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-md">
        <form onSubmit={submit} className="grid gap-4">
          <DialogHeader>
            <DialogTitle>Add site</DialogTitle>
            <DialogDescription>
              Paste a URL or domain. The profile is fetched in the background.
            </DialogDescription>
          </DialogHeader>

          <div className="grid gap-1.5">
            <Label htmlFor="add-url">URL</Label>
            <Input
              id="add-url"
              value={url}
              onChange={(event) => {
                setUrl(event.target.value)
                setAllowDuplicate(false)
              }}
              placeholder="example.com"
              autoFocus
              aria-invalid={Boolean(invalid)}
            />
            {invalid && <p className="text-xs text-destructive">{invalid}</p>}
            {info && !typing && (
              <p className="text-xs text-muted-foreground">
                → <span className="font-mono">{info.normalizedUrl}</span> · key{" "}
                <span className="font-mono">{info.siteKey}</span>
              </p>
            )}
            {info?.existing && (
              <div className="grid gap-2 rounded-lg border border-amber-500/30 bg-amber-500/10 px-2.5 py-2 text-xs text-amber-700 dark:text-amber-200">
                <div className="flex items-center gap-2">
                  <TriangleAlertIcon className="size-4 shrink-0" />
                  <span className="flex-1">
                    Already in your list: {info.existing.siteKey}
                  </span>
                  <Link
                    to="/sites/$siteId"
                    params={{ siteId: info.existing.id }}
                    onClick={() => onOpenChange(false)}
                    className="flex items-center gap-1 font-medium underline-offset-2 hover:underline"
                  >
                    Open <ArrowRightIcon className="size-3" />
                  </Link>
                </div>
                <Label className="flex items-center gap-2 font-normal">
                  <Checkbox
                    checked={allowDuplicate}
                    onCheckedChange={(v) => setAllowDuplicate(!!v)}
                  />
                  Add anyway as a separate project
                </Label>
              </div>
            )}
            {info && info.related.length > 0 && !info.existing && (
              <p className="text-xs text-muted-foreground">
                Related: {info.related.map((site) => site.siteKey).join(", ")}
              </p>
            )}
            {info?.blockedReason === "private_network" && (
              <div className="grid gap-2 rounded-lg border border-amber-500/30 bg-amber-500/10 px-2.5 py-2 text-xs text-amber-700 dark:text-amber-200">
                <span>
                  This address is on a private network (localhost, LAN or
                  similar).
                </span>
                <Label className="flex items-center gap-2 font-normal">
                  <Checkbox
                    checked={allowPrivate}
                    onCheckedChange={(v) => setAllowPrivate(!!v)}
                  />
                  Allow local/LAN target
                </Label>
              </div>
            )}
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="add-name">Name (optional)</Label>
            <Input
              id="add-name"
              value={name}
              onChange={(event) => setName(event.target.value)}
              placeholder="Client or project name"
              maxLength={120}
            />
          </div>

          <div className="grid grid-cols-2 gap-3">
            <div className="grid gap-1.5">
              <Label>Status</Label>
              <SimpleSelect
                value={status}
                onChange={setStatus}
                className="w-full"
                options={STATUSES.map((value) => ({ value, label: value }))}
              />
            </div>
            <div className="grid gap-1.5">
              <Label>Category</Label>
              <SimpleSelect
                value={category}
                onChange={setCategory}
                placeholder="None"
                className="w-full"
                options={categories.map((item) => ({
                  value: String(item.id),
                  label: item.name,
                }))}
              />
            </div>
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="add-tags">Tags (comma separated)</Label>
            <Input
              id="add-tags"
              value={tags}
              onChange={(event) => setTags(event.target.value)}
              placeholder="seo, rebuild"
            />
          </div>

          <Label className="flex items-center gap-2 font-normal">
            <Checkbox
              checked={fetchNow}
              onCheckedChange={(value) => setFetchNow(value)}
            />
            Fetch profile now
          </Label>

          <DialogFooter>
            <Button
              type="button"
              variant="outline"
              onClick={() => onOpenChange(false)}
            >
              Cancel
            </Button>
            <Button type="submit" disabled={!canSubmit}>
              Add site
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}
