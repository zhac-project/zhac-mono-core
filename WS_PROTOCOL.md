# mono WS protocol alignment (to the shared SPA)

The SPA's WS client (`www-spa/src/ws/client.js`) is the contract. mono's WS
layer must match it. mono originally used a **flat** envelope + wrong push
names — incompatible. This file is the target + the remaining work.

## Envelope (MUST)
- **Reply:** `{"id":N,"ok":true,"data":<payload>}` or `{"id":N,"ok":false,"err":"…"}`.
  SPA does `resolve(msg.data)` / `reject(msg.err)`. Reads only `id`/`ok`/`data`/`err`.
- **Push:** `{"event":"<name>","data":<payload>}` (no `id`). Routed by `msg.event`.
- **ok-only cmds** (data ignored): `{"id":N,"ok":true}` suffices (`send_err`/`reply_ok_or_err` already fine). These rely on a **push** to update the UI.
- **Lists** tolerate bare array OR `{<key>:[…]}` — existing payloads (`{"groups":[…]}`, `{"logs":[…]}`, `{"entries":[…]}`) work once nested under `data`.

## Reply reshape (nest payload under `data`)
Data-returning cmds to wrap: `status` · `device.list` · `device.get` ·
`group.list/get` · `wifi.status/scan` · `logs.get` · `diagnostics.unhandled.get`
· `remote.status` · (new) `rule.list` · `script.list/read`.
Splice cmds: change prefix to `{"id":N,"ok":true,"data":<full payload>}`.
JsonDocument cmds: `env["id"]=id; env["ok"]=true; env["data"]=<payload obj>`.

## Push reshape — DONE for the 3 existing; names corrected
- `zcl_attr`  → **`attr.changed`**, data `{ieee,key,value,nwk,ep,cluster,attr_id}` ✅
- `device_join` → **`device.added`**, data `{ieee,friendly,name}` ✅
- `device_leave` → **`device.removed`**, data `{ieee}` ✅
(Optional: coalesced `attr.bulk` = array of `{ieee,attrs:{…}}` — net-core batches ~100 ms; mono can keep per-attr `attr.changed`.)

## Remaining work
1. **Reply reshape** (above) — wrap the data-returning cmds' payloads under `data`.
2. **WS `rule.*` + `script.*` commands** — SPA drives these over WS (mono has them REST-only). Add: `rule.list/create/update/enable/delete`, `script.list/read/delete/run`, `script.check` (script.write stays REST `POST /api/scripts/<name>`). Build replies in `data` shape.
3. **CRUD push events** — after create/update/delete, emit `rule.added/updated/deleted`, `script.added/updated/deleted`, `group.added/updated/deleted` (data `{id|name, …}` / `{id|name}`) so the SPA list updates without reload. Source: emit from the WS cmd (post-success) or from the service via event_bus.
4. **cmd-name fixes:** SPA sends `diagnostics.unhandled.get` (mono dispatches `diagnostics.unhandled`); add the `.get` alias. Verify `status` vs `status.get` (SPA sends `status.get`; mono dispatches `status` — add alias).
5. **zigbee.* cmds** (`zigbee.reset`, `zigbee.settings.set`) — still missing (gap #3).
6. **OTA** push/cmds — excluded by design (no OTA on mono).

Full cmd/event tables: see the parity-audit subagent output / net-core `api_routes.def` + `www-spa/src/stores`.
