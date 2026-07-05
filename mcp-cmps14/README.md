# cmps14-mcp

A small Python [MCP](https://modelcontextprotocol.io) tool that lets a **Claude Desktop**
agent talk to the **CMPS14-ESP32-SignalK gateway** over its HTTP API.

It exposes four tools:

| Tool | Gateway call | Purpose |
|------|--------------|---------|
| `get_status()` | `GET /status` | Live heading, deviation, calibration status, health |
| `get_deviations()` | `GET /deviations` | The 8 stored deviation points + harmonic coefficients |
| `get_deviation_table(step=1)` | `GET /deviations?table=1` | Firmware-computed 360-entry (1°) deviation curve |
| `set_deviations(N=…, …, NW=…)` | `POST /dev8/set` | Write deviation points (merge-before-write) |

The InfluxDB analysis (finding COG(T)/HDG(T) deviations and deriving the eight
values) is done by the Claude Desktop agent itself — this tool is only the bridge
to the gateway.

## Prerequisites

1. Gateway firmware with the `/deviations` endpoint and API-token support flashed.
2. In the gateway's `secrets.h`, set a token and reflash:
   ```cpp
   inline constexpr const char* MCP_API_TOKEN = "<32+ hex chars>";
   ```
   Generate one with: `python3 -c "import secrets; print(secrets.token_hex(24))"`
3. Python 3.11+ on the macOS workstation.

## Install

Using a virtualenv:

```bash
cd mcp-cmps14
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
# (or: .venv/bin/pip install -e .)
```

Or with [uv](https://docs.astral.sh/uv/):

```bash
cd mcp-cmps14
uv venv
uv pip install -e .
```

## Configure Claude Desktop

Edit `~/Library/Application Support/Claude/claude_desktop_config.json` and add a
server entry (use **absolute paths**):

```json
{
  "mcpServers": {
    "cmps14": {
      "command": "/absolute/path/to/mcp-cmps14/.venv/bin/python",
      "args": ["-m", "cmps14_mcp"],
      "env": {
        "CMPS14_BASE_URL": "http://192.168.1.54",
        "CMPS14_API_TOKEN": "<same token as MCP_API_TOKEN in secrets.h>",
        "CMPS14_TIMEOUT": "5.0"
      }
    }
  }
}
```

Restart Claude Desktop. The `cmps14` tools appear in the tool menu.

## Environment variables

| Variable | Required | Default | Meaning |
|----------|----------|---------|---------|
| `CMPS14_BASE_URL` | yes | – | Gateway base URL, e.g. `http://192.168.1.54` |
| `CMPS14_API_TOKEN` | yes | – | Must equal `MCP_API_TOKEN` in `secrets.h` (min 16 chars) |
| `CMPS14_TIMEOUT` | no | `5.0` | Per-request timeout in seconds |

## Try it standalone

With the [MCP Inspector](https://github.com/modelcontextprotocol/inspector):

```bash
CMPS14_BASE_URL=http://192.168.1.54 CMPS14_API_TOKEN=<token> \
  npx @modelcontextprotocol/inspector .venv/bin/python -m cmps14_mcp
```

Then call each tool against the live device.

## Notes

- Angles are in degrees; deviation values are clamped to ±90 (same as the web UI).
- `set_deviations` is **merge-before-write**: it reads the current 8 values first,
  overrides only the directions you pass, and writes all 8 — so a partial update
  never zeroes the others.
- A `401` means the token was rejected; check `CMPS14_API_TOKEN` vs `secrets.h`.
- The gateway serves plain HTTP on the boat LAN; keep the token secret.
