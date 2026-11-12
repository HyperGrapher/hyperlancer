import {
  CodeIcon,
  ExternalLinkIcon,
  GlobeIcon,
  PencilIcon,
  PlusIcon,
  ServerIcon,
  TrashIcon,
} from "lucide-react"
import { useState, type FormEvent } from "react"
import { toast } from "sonner"

import { useContactMutations, useLinkMutations } from "@/api/hooks/work"
import type { SiteDetail } from "@/api/types"
import { CopyButton } from "@/components/common/misc"
import { SimpleSelect } from "@/components/common/simple-select"
import { Button } from "@/components/ui/button"
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"

type ContactItem = SiteDetail["contacts"][number]
type LinkItem = SiteDetail["links"][number]

const CHANNELS = ["email", "phone", "whatsapp", "telegram", "other"]
const LINK_KINDS = [
  "production",
  "staging",
  "repo",
  "hosting",
  "registrar",
  "dns",
  "analytics",
  "design",
  "other",
]
const linkIcons: Record<string, typeof GlobeIcon> = {
  production: GlobeIcon,
  staging: GlobeIcon,
  repo: CodeIcon,
  hosting: ServerIcon,
}

function ContactDialog({
  siteId,
  contact,
  onClose,
}: {
  siteId: number
  contact: ContactItem | null
  onClose: () => void
}) {
  const { create, update } = useContactMutations(siteId)
  const [name, setName] = useState(contact?.name ?? "")
  const [role, setRole] = useState(contact?.role ?? "")
  const [email, setEmail] = useState(contact?.email ?? "")
  const [phone, setPhone] = useState(contact?.phone ?? "")
  const [channel, setChannel] = useState<string | null>(
    contact?.preferredChannel ?? "email"
  )
  const [errors, setErrors] = useState<Record<string, string>>({})
  const pending = create.isPending || update.isPending

  function submit(event: FormEvent) {
    event.preventDefault()
    const body = { name, role, email, phone, preferredChannel: channel }
    const options = {
      onSuccess: onClose,
      onError: (error: Error & { fields?: Record<string, string> }) => {
        setErrors(error.fields ?? {})
        if (!error.fields || Object.keys(error.fields).length === 0)
          toast.error(error.message)
      },
    }
    if (contact) update.mutate({ id: contact.id, ...body }, options)
    else create.mutate(body, options)
  }

  const field = (
    id: string,
    label: string,
    value: string,
    set: (v: string) => void,
    extra: object = {}
  ) => (
    <div className="grid gap-1.5">
      <Label htmlFor={`contact-${id}`}>{label}</Label>
      <Input
        id={`contact-${id}`}
        value={value}
        onChange={(e) => set(e.target.value)}
        aria-invalid={Boolean(errors[id])}
        {...extra}
      />
      {errors[id] && <p className="text-xs text-destructive">{errors[id]}</p>}
    </div>
  )

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>{contact ? "Edit contact" : "Add contact"}</DialogTitle>
        </DialogHeader>
        <form onSubmit={submit} className="grid gap-3">
          {field("name", "Name", name, setName, {
            autoFocus: true,
            maxLength: 120,
          })}
          {field("role", "Role", role, setRole, {
            maxLength: 120,
            placeholder: "Owner, marketing, developer…",
          })}
          <div className="grid grid-cols-2 gap-3">
            {field("email", "Email", email, setEmail, { type: "email" })}
            {field("phone", "Phone", phone, setPhone, {
              placeholder: "+90 5xx xxx xx xx",
            })}
          </div>
          <div className="grid gap-1.5">
            <Label>Prefers</Label>
            <SimpleSelect
              value={channel}
              onChange={setChannel}
              className="w-full"
              options={CHANNELS.map((value) => ({ value, label: value }))}
            />
          </div>
          <DialogFooter>
            <Button type="submit" disabled={!name.trim() || pending}>
              Save
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}

export function ContactsSection({ site }: { site: SiteDetail }) {
  const { remove } = useContactMutations(site.id)
  const [editing, setEditing] = useState<ContactItem | "new" | null>(null)

  return (
    <section className="rounded-xl border border-border bg-card/60 p-4">
      <div className="mb-2 flex items-center justify-between">
        <h3 className="font-medium">Contacts</h3>
        <Button
          variant="ghost"
          size="icon-xs"
          aria-label="Add contact"
          onClick={() => setEditing("new")}
        >
          <PlusIcon />
        </Button>
      </div>
      {site.contacts.length === 0 ? (
        <p className="text-muted-foreground">No contacts yet.</p>
      ) : (
        <ul className="flex flex-col gap-3">
          {site.contacts.map((contact) => (
            <li key={contact.id} className="group">
              <div className="flex items-start justify-between gap-1">
                <div className="min-w-0">
                  <div className="truncate font-medium">{contact.name}</div>
                  <div className="text-xs text-muted-foreground">
                    {[
                      contact.role,
                      contact.preferredChannel &&
                        `prefers ${contact.preferredChannel}`,
                    ]
                      .filter(Boolean)
                      .join(" · ")}
                  </div>
                </div>
                <div className="flex shrink-0 opacity-0 group-hover:opacity-100 focus-within:opacity-100">
                  <Button
                    variant="ghost"
                    size="icon-xs"
                    aria-label={`Edit ${contact.name}`}
                    onClick={() => setEditing(contact)}
                  >
                    <PencilIcon />
                  </Button>
                  <Button
                    variant="ghost"
                    size="icon-xs"
                    aria-label={`Delete ${contact.name}`}
                    onClick={() =>
                      window.confirm(`Remove ${contact.name}?`) &&
                      remove.mutate(contact.id, {
                        onError: (e) => toast.error(e.message),
                      })
                    }
                  >
                    <TrashIcon />
                  </Button>
                </div>
              </div>
              {[contact.email, contact.phone]
                .filter((value): value is string => Boolean(value))
                .map((value) => (
                  <div
                    key={value}
                    className="flex items-center justify-between gap-1 text-sm"
                  >
                    <span className="min-w-0 truncate select-all">{value}</span>
                    <CopyButton value={value} />
                  </div>
                ))}
            </li>
          ))}
        </ul>
      )}
      {editing && (
        <ContactDialog
          siteId={site.id}
          contact={editing === "new" ? null : editing}
          onClose={() => setEditing(null)}
        />
      )}
    </section>
  )
}

function LinkDialog({
  siteId,
  link,
  onClose,
}: {
  siteId: number
  link: LinkItem | null
  onClose: () => void
}) {
  const { create, update } = useLinkMutations(siteId)
  const [kind, setKind] = useState<string | null>(link?.kind ?? "production")
  const [label, setLabel] = useState(link?.label ?? "")
  const [url, setUrl] = useState(link?.url ?? "")
  const [error, setError] = useState<string | null>(null)

  function submit(event: FormEvent) {
    event.preventDefault()
    const body = { kind: kind ?? "other", label, url }
    const options = {
      onSuccess: onClose,
      onError: (e: Error & { fields?: Record<string, string> }) =>
        setError(e.fields?.url ?? e.message),
    }
    if (link) update.mutate({ id: link.id, ...body }, options)
    else create.mutate(body, options)
  }

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>{link ? "Edit link" : "Add link"}</DialogTitle>
        </DialogHeader>
        <form onSubmit={submit} className="grid gap-3">
          <div className="grid gap-1.5">
            <Label>Type</Label>
            <SimpleSelect
              value={kind}
              onChange={setKind}
              className="w-full"
              options={LINK_KINDS.map((value) => ({ value, label: value }))}
            />
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="link-label">Label</Label>
            <Input
              id="link-label"
              value={label}
              onChange={(e) => setLabel(e.target.value)}
              placeholder="GitHub repo, cPanel…"
              maxLength={80}
            />
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="link-url">URL</Label>
            <Input
              id="link-url"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              autoFocus
              aria-invalid={Boolean(error)}
            />
            {error && <p className="text-xs text-destructive">{error}</p>}
          </div>
          <DialogFooter>
            <Button
              type="submit"
              disabled={!url.trim() || create.isPending || update.isPending}
            >
              Save
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}

export function LinksSection({ site }: { site: SiteDetail }) {
  const { remove } = useLinkMutations(site.id)
  const [editing, setEditing] = useState<LinkItem | "new" | null>(null)

  return (
    <section className="rounded-xl border border-border bg-card/60 p-4">
      <div className="mb-2 flex items-center justify-between">
        <h3 className="font-medium">Links</h3>
        <Button
          variant="ghost"
          size="icon-xs"
          aria-label="Add link"
          onClick={() => setEditing("new")}
        >
          <PlusIcon />
        </Button>
      </div>
      {site.links.length === 0 ? (
        <p className="text-muted-foreground">
          Production, staging, repo, hosting…
        </p>
      ) : (
        <ul className="flex flex-col gap-1">
          {site.links.map((link) => {
            const Icon = linkIcons[link.kind] ?? ExternalLinkIcon
            return (
              <li key={link.id} className="group flex items-center gap-1">
                <a
                  href={link.url}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="flex min-w-0 flex-1 items-center gap-2 rounded-md px-1.5 py-1 hover:bg-muted"
                >
                  <Icon className="size-4 shrink-0 text-muted-foreground" />
                  <span className="flex-1 truncate">
                    {link.label ?? link.url}
                  </span>
                  <span className="text-xs text-muted-foreground">
                    {link.kind}
                  </span>
                </a>
                <div className="flex shrink-0 opacity-0 group-hover:opacity-100 focus-within:opacity-100">
                  <Button
                    variant="ghost"
                    size="icon-xs"
                    aria-label={`Edit link ${link.label ?? link.url}`}
                    onClick={() => setEditing(link)}
                  >
                    <PencilIcon />
                  </Button>
                  <Button
                    variant="ghost"
                    size="icon-xs"
                    aria-label={`Delete link ${link.label ?? link.url}`}
                    onClick={() =>
                      remove.mutate(link.id, {
                        onError: (e) => toast.error(e.message),
                      })
                    }
                  >
                    <TrashIcon />
                  </Button>
                </div>
              </li>
            )
          })}
        </ul>
      )}
      {editing && (
        <LinkDialog
          siteId={site.id}
          link={editing === "new" ? null : editing}
          onClose={() => setEditing(null)}
        />
      )}
    </section>
  )
}
