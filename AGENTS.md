# Agent Notes

## Cloudflare deployment

TinyDraw's browser builds are existing **Cloudflare Workers with static
assets**, not Cloudflare Pages projects. Do not run `wrangler pages deploy`,
do not infer their existence from `wrangler pages project list`, and do not
create a Pages project for either build.

The production Workers are:

- TinyDraw v1: `tinydraw1` at <https://tinydraw1.aliceisplaying.workers.dev>
- TinyDraw v2: `tinydraw2` at <https://tinydraw2.aliceisplaying.workers.dev>

Confirm the target before changing it:

```sh
wrangler deployments list --name tinydraw2
```

For a TinyDraw v2 deployment, build and verify the current TinyDraw firmware
through the Puck checkout, build Puck's static site, then update the existing
`tinydraw2` Worker:

```sh
# From the TinyDraw checkout or worktree:
./scripts/puck /Users/sarah/src/a/puck

# From /Users/sarah/src/a/puck:
bun run build
wrangler deploy \
  --name tinydraw2 \
  --assets dist \
  --compatibility-date YYYY-MM-DD \
  --message "TinyDraw V2 <tinydraw-commit> on Puck <puck-commit>"
```

After deployment, fetch
`https://tinydraw2.aliceisplaying.workers.dev/wasm/emu.wasm` and compare it
byte-for-byte with Puck's `dist/wasm/emu.wasm`. A v2 request must leave
`tinydraw1` untouched unless the user explicitly asks to update v1 too.
