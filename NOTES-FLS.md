# Florida Seating notes on this fork

**Why this fork exists:** Florida Seating deploys go-mapi across its Windows
workstations. Upstream (`marcfargas/go-mapi`) has had no maintainer activity since the
v3.0.0 release (2026-04-30), and the published v3.0.0 release has defects that make the
stock download unusable as shipped. This fork keeps the upstream link for tracking, and
gives us a line we can patch and build ourselves if upstream stays dormant.

Licence is unchanged: **LGPL-3.0-or-later**. We have made no source changes so far — the
production fix was applied at deployment time, not in code.

Related in-house documentation: `FLS-workstation-setup-runbook.md` (go-mapi section).

---

## Findings from the 2026-07-29 fleet deployment

### 1. The published v3.0.0 binary ships with no OAuth credentials — it cannot start

`go-mapi-setup.exe` from the v3.0.0 release installs fine, but the app fatals immediately
on launch:

```
[ERROR] FATAL: OAuth client_id missing - build was not wired correctly
        (expected -ldflags -X main.oauthClientID, or GOMAPI_OAUTH_CLIENT_ID env var for wails dev)
```

The release was built without its `oauthClientID` / `oauthClientSecret` ldflags, so no user
can ever sign in or create a draft. This is almost certainly the real cause of the
open upstream reports:

- upstream issue #7 "Failed to launch"
- upstream issue #6 "Installer creates mail client registry entry, but no install.log/runtime
  logs and SendTo does nothing" — the reporter saw no runtime logs because the app was dying
  at startup.

**How we work around it:** the binary still honours the dev-mode environment variables, so we
set them machine-wide from our own Google Cloud OAuth client (Desktop app type, Internal
consent, in the Florida Seating Workspace tenant):

```
GOMAPI_OAUTH_CLIENT_ID      (machine env var)
GOMAPI_OAUTH_CLIENT_SECRET  (machine env var)
```

With those present the app starts normally (`startup complete (version 3.0.0)`) and reaches
the expected signed-out state awaiting the user's Google sign-in.

A proper fix upstream would be to wire the ldflags in the release workflow — or, better for
downstream deployers, to document the env-var override as a supported enterprise
configuration so organisations can use their own OAuth client instead of a shared one.
(A per-organisation client is arguably the *correct* model for Workspace tenants anyway:
Internal consent keeps the grant inside the domain.)

### 2. `MAPISendDocuments` is a stub that returns success

`src/interceptor/mapi_impl.cpp`:

```cpp
ULONG MapiImpl::MAPISendDocuments(...) {
    // Stub: not implemented yet
    return SUCCESS_SUCCESS;
}
```

Any application that uses this Simple MAPI entry point gets `SUCCESS_SUCCESS` and **nothing
happens** — no queue file, no draft, no error. Silent data loss from the user's point of view
("I clicked send and it vanished").

Verified on our fleet: calling `MAPISendDocuments` directly against the interceptor DLL
returns 0 and writes nothing to `%LOCALAPPDATA%\go-mapi\queue`.

**Not on the critical path for Explorer**, fortunately: Windows' own `sendmail.dll` (the
handler behind *Send to → Mail recipient*) imports `MAPISendMail` / `MAPISendMailW`, both of
which are fully implemented and work correctly. We confirmed the whole chain end to end —
`MAPISendMail` writes the JSON envelope plus a copy of the attachment into the queue, and the
running app picks it up.

Returning `MAPI_E_NOT_SUPPORTED` would at least surface a real error to callers that use it.

### 3. Installer points the 64-bit registration at the 32-bit DLL

The installer writes `HKLM\SOFTWARE\Clients\Mail\go-mapi\DLLPath` =
`C:\Program Files (x86)\go-mapi\go-mapi.dll`.

Worth knowing (and worth a comment in the installer): `HKLM\SOFTWARE\Clients` is one of the
**shared, non-redirected** registry keys, so the native and `WOW6432Node` views are the *same*
key — there is exactly **one** `DLLPath` serving both 32- and 64-bit MAPI callers, and it can
only point at one architecture.

Pointing it at the x86 DLL (the shipped default) is the pragmatic choice, since most legacy
MAPI callers are 32-bit, and it is what we run in production. But a 64-bit caller then fails
to load the provider with `ERROR_BAD_EXE_FORMAT (193)`, silently. We initially "fixed" this to
the x64 path and had to revert — that just moves the failure onto the 32-bit apps that matter
more here.

### 4. Toast notification fails on arrival

```
[ERROR] toast: arrival push failed for <id>: toast shim: load xml: LoadXml HRESULT 0xc00ce50d
```

`0xC00CE50D` is an XML parse failure in the toast payload. In the default `manual` mode the
toast is the user's only signal that a message is waiting to be turned into a draft, so a
broken toast means the workflow appears to do nothing.

**How we work around it:** we seed `settings.json` with `"mode":"auto"` so the draft is created
without depending on the notification.

### 5. Useful for deployers: `settings.json` has an update kill switch

`%APPDATA%\go-mapi\settings.json`:

```json
{"mode":"auto","update_checks_enabled":false}
```

`update_checks_enabled:false` pins the version without needing a firewall rule — which matters,
because unlike a purely local app go-mapi legitimately needs outbound network access for Google
sign-in and the Gmail API. Seeding this file into `C:\Users\Default\...` gives every future
profile the same defaults. Worth documenting in `ENTERPRISE.md`.

---

## Our deployment shape (for future reference)

- Machine-wide silent install, `go-mapi-setup.exe /S`, **without** `/AUTOUPDATE=1`.
- Installer kept resident at `C:\FLS-Deploy\` with a do-not-delete README.
- Version pinned at 3.0.0; installer SHA256 verified against the release's `SHA256SUMS.txt`
  (`fed4c4d1b09e6f0ca93471b94e940c6bbb26781ead1e7c727e21e8636d56ca06`) and scanned before rollout.
- OAuth client id/secret injected as machine environment variables (see finding 1). A Desktop-app
  OAuth client is a *public* client under RFC 8252, so the secret is not treated as confidential —
  the real controls are Internal consent plus each user's own Google sign-in.
- `settings.json` seeded per profile and into the Default profile (see findings 4 and 5).
- Machines whose users deliberately remain on Outlook/Exchange keep `Microsoft Outlook` as the
  registered default mail client; go-mapi is installed there but does not take over MAPI.
