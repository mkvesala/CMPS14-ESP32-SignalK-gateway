"""
MCP server exposing the CMPS14-ESP32-SignalK gateway's HTTP API to a Claude
Desktop agent.

The agent's job (querying InfluxDB for COG(T)/HDG(T) deviations and deriving the
eight cardinal/intercardinal deviation values) is done by the agent itself. This
tool is the bridge to the gateway: read status, read the currently stored
deviation points, write new deviation points, and read the firmware-computed
360-entry (1 degree) deviation curve.

Configuration (environment variables, set in the Claude Desktop config `env` block):
  CMPS14_BASE_URL   e.g. "http://192.168.1.54"   (required)
  CMPS14_API_TOKEN  the value of MCP_API_TOKEN in the gateway's secrets.h (required)
  CMPS14_TIMEOUT    request timeout in seconds (optional, default 5.0)

All angles are in degrees. Deviation sign convention matches the gateway web UI
and the /dev8/set form: the same numbers the user enters by hand.
"""

from __future__ import annotations

import os
from typing import Optional

import httpx
from mcp.server.fastmcp import FastMCP

# Cardinal / intercardinal order expected by the gateway's /dev8/set and /deviations
DIRECTIONS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
DEVIATION_LIMIT = 90.0  # firmware clamps each measured point to +/- 90 deg

mcp = FastMCP("cmps14")

_client: Optional[httpx.Client] = None


def _get_client() -> httpx.Client:
    """Build (once) and return the shared HTTP client, or raise a clear config error."""
    global _client
    if _client is not None:
        return _client

    base_url = os.environ.get("CMPS14_BASE_URL", "").strip()
    token = os.environ.get("CMPS14_API_TOKEN", "").strip()
    if not base_url:
        raise RuntimeError("CMPS14_BASE_URL is not set (e.g. http://192.168.1.54)")
    if not token:
        raise RuntimeError("CMPS14_API_TOKEN is not set (must match MCP_API_TOKEN in secrets.h)")

    try:
        timeout = float(os.environ.get("CMPS14_TIMEOUT", "5.0"))
    except ValueError:
        timeout = 5.0

    _client = httpx.Client(
        base_url=base_url.rstrip("/"),
        headers={"X-Auth-Token": token},
        timeout=timeout,
    )
    return _client


def _check(resp: httpx.Response) -> httpx.Response:
    """Map gateway HTTP errors to clear, actionable exceptions."""
    if resp.status_code == 401:
        raise RuntimeError(
            "401 Unauthorized: the gateway rejected the API token. Verify "
            "CMPS14_API_TOKEN matches MCP_API_TOKEN in secrets.h (min 16 chars, "
            "non-empty enables token auth)."
        )
    if resp.status_code >= 400:
        body = resp.text[:200].replace("\n", " ")
        raise RuntimeError(f"HTTP {resp.status_code} from {resp.request.url}: {body}")
    return resp


def _request(method: str, path: str, **kwargs) -> httpx.Response:
    client = _get_client()
    try:
        resp = client.request(method, path, **kwargs)
    except httpx.TimeoutException as e:
        raise RuntimeError(f"Timeout contacting the gateway at {client.base_url}{path}: {e}") from e
    except httpx.HTTPError as e:
        raise RuntimeError(f"Network error contacting the gateway: {e}") from e
    return _check(resp)


@mcp.tool()
def get_status() -> dict:
    """
    Read the live status of the CMPS14 gateway (GET /status).

    Returns the parsed JSON status block, including current heading (hdg_deg,
    compass_deg, heading_true_deg), the live deviation (dev), magnetic variation,
    calibration status/mode, harmonic coefficients (hca..hce) and device health
    (heap, uptime, versions). Useful for checking the device is reachable and for
    reading the current heading/deviation before or after writing new values.
    """
    resp = _request("GET", "/status")
    return resp.json()


@mcp.tool()
def get_deviations() -> dict:
    """
    Read the currently stored deviation configuration (GET /deviations).

    Returns:
      {
        "measured": {"N": <deg>, "NE": <deg>, ..., "NW": <deg>},  # the 8 user-set points
        "coeffs":   {"A": .., "B": .., "C": .., "D": .., "E": ..} # fitted harmonic model
      }

    These are the eight cardinal/intercardinal deviation values the gateway uses.
    Read them first if you intend to update only some directions.
    """
    resp = _request("GET", "/deviations")
    return resp.json()


@mcp.tool()
def get_deviation_table(step: int = 1) -> dict:
    """
    Read the firmware-computed 360-entry deviation curve (GET /deviations?table=1).

    This is the "eksymataulukko": one deviation value per compass heading, 0..359
    degrees, interpolated from the harmonic model the gateway actually applies.

    Args:
      step: sampling interval in degrees (default 1 = full 360 values). Use a
            larger step (e.g. 5, 10, 15) to get a coarser curve and fewer tokens.

    Returns:
      {
        "step": <step>,
        "count": <number of samples>,
        "table": [{"heading": <deg>, "deviation": <deg>}, ...]
      }
    """
    if step < 1:
        step = 1
    if step > 180:
        step = 180

    resp = _request("GET", "/deviations", params={"table": "1"})
    data = resp.json()
    table = data.get("table")
    if not isinstance(table, list) or len(table) != 360:
        raise RuntimeError(
            "Gateway did not return a valid 360-entry table. Ensure the firmware "
            "with the /deviations endpoint is flashed."
        )

    samples = [
        {"heading": h, "deviation": table[h]}
        for h in range(0, 360, step)
    ]
    return {"step": step, "count": len(samples), "table": samples}


@mcp.tool()
def set_deviations(
    N: Optional[float] = None,
    NE: Optional[float] = None,
    E: Optional[float] = None,
    SE: Optional[float] = None,
    S: Optional[float] = None,
    SW: Optional[float] = None,
    W: Optional[float] = None,
    NW: Optional[float] = None,
) -> dict:
    """
    Write deviation values for the eight cardinal/intercardinal directions
    (POST /dev8/set), then return the re-read stored configuration.

    Merge-before-write: any direction left as None keeps its current stored value.
    This protects against the gateway treating a missing field as 0. So you can
    update a single direction, e.g. set_deviations(N=3.5), without zeroing the rest.

    Each value is in degrees and is clamped to +/- 90 (same as the web UI). Writing
    recomputes the harmonic model and the 360-entry table on the device, and the
    new values are persisted to NVS.

    Returns the same structure as get_deviations() reflecting the stored result.
    """
    # Fetch current values so unspecified directions are preserved.
    current = get_deviations()["measured"]

    provided = {
        "N": N, "NE": NE, "E": E, "SE": SE,
        "S": S, "SW": SW, "W": W, "NW": NW,
    }

    payload: dict[str, str] = {}
    for d in DIRECTIONS:
        value = provided[d] if provided[d] is not None else current.get(d, 0.0)
        value = float(value)
        # Clamp to the firmware-accepted range.
        value = max(-DEVIATION_LIMIT, min(DEVIATION_LIMIT, value))
        payload[d] = f"{value:.4f}"

    # /dev8/set responds with the config HTML page; any 2xx means success.
    _request("POST", "/dev8/set", data=payload)

    return get_deviations()


def main() -> None:
    """Entry point: run the MCP server over stdio (for Claude Desktop)."""
    mcp.run()


if __name__ == "__main__":
    main()
