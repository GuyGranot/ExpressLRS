"""ELRS-link simulator: throughput shaping, added latency, MSP-frame drops.

Models the MSP-over-CRSF tunnel's behavior without any firmware:
  - token-bucket byte throttle per direction (the OTA chunk cadence)
  - fixed added latency per direction (chunk/ack round-trip cost)
  - whole-MSP-frame drops (one lost OTA chunk kills the frame, no NAK)
  - non-MSP bytes dropped by default (msp2crsf only parses $M/$X frames,
    so raw CLI traffic dies at the bridge -- keep this on to test how the
    app's CLI tab fails)

Also collects the stats Phase 0 exists to measure: per-request latency,
pipelining depth, drop counts, effective throughput.
"""

from __future__ import annotations

import asyncio
import logging
import random
import time
from collections import deque
from dataclasses import dataclass, field

from sb_protocol import MspStreamParser, MspFrame

log = logging.getLogger("linksim")


@dataclass
class LinkConfig:
    rate_bps: float = 625.0        # bytes/sec each way (250Hz 1:2 std OTA)
    latency_s: float = 0.100       # added one-way latency
    drop_down: float = 0.0         # P(drop) per FC->app MSP frame
    drop_up: float = 0.0           # P(drop) per app->FC MSP frame
    forward_raw: bool = False      # False = drop non-MSP bytes like msp2crsf
    seed: int | None = None        # deterministic drops for repeatable runs


class Stats:
    def __init__(self):
        self.t0 = time.monotonic()
        self.up_frames = 0
        self.up_bytes = 0
        self.down_frames = 0
        self.down_bytes = 0
        self.dropped_up = 0
        self.dropped_down = 0
        self.raw_up_bytes = 0
        self.raw_down_bytes = 0
        self.outstanding: deque[MspFrame] = deque()
        self.max_outstanding = 0
        self.latencies: list[tuple[int, float]] = []   # (cmd, seconds)
        self.timeouts_guess = 0

    def on_request(self, frame: MspFrame):
        self.up_frames += 1
        self.up_bytes += len(frame.raw)
        self.outstanding.append(frame)
        self.max_outstanding = max(self.max_outstanding, len(self.outstanding))

    def on_response(self, frame: MspFrame):
        self.down_frames += 1
        self.down_bytes += len(frame.raw)
        for i, req in enumerate(self.outstanding):
            if req.cmd == frame.cmd:
                self.latencies.append((frame.cmd, time.monotonic() - req.t))
                del self.outstanding[i]
                return
        # response without a tracked request (unsolicited or post-drop retry)

    def summary(self) -> str:
        dt = max(time.monotonic() - self.t0, 1e-6)
        lat = sorted(s for _, s in self.latencies)
        pct = (lambda p: lat[min(len(lat) - 1, int(p * len(lat)))] if lat else 0.0)
        return (
            f"up {self.up_frames} frames/{self.up_bytes}B ({self.up_bytes/dt:.0f} B/s) "
            f"down {self.down_frames} frames/{self.down_bytes}B ({self.down_bytes/dt:.0f} B/s) | "
            f"dropped up/down {self.dropped_up}/{self.dropped_down} | "
            f"raw(CLI?) up/down {self.raw_up_bytes}/{self.raw_down_bytes}B | "
            f"pipeline max {self.max_outstanding}, now {len(self.outstanding)} | "
            f"latency med {pct(0.5)*1000:.0f}ms p90 {pct(0.9)*1000:.0f}ms "
            f"max {(lat[-1]*1000 if lat else 0):.0f}ms over {len(lat)} matched"
        )


class ShapedPipe:
    """Order-preserving byte pipe with rate limiting and added latency."""

    def __init__(self, name: str, rate_bps: float, latency_s: float,
                 sink, chunk: int = 16):
        self.name = name
        self.rate = rate_bps
        self.latency = latency_s
        self.sink = sink                     # async callable(bytes)
        self.chunk = chunk                   # release granularity, bytes
        self._queue: asyncio.Queue[tuple[float, bytes]] = asyncio.Queue()
        self._next_free = 0.0
        self._task: asyncio.Task | None = None

    def start(self):
        self._task = asyncio.get_running_loop().create_task(self._dispatch())

    def stop(self):
        if self._task:
            self._task.cancel()

    def write(self, data: bytes):
        """Schedule bytes; deliver time = serialization under rate + latency."""
        now = time.monotonic()
        self._next_free = max(self._next_free, now)
        for i in range(0, len(data), self.chunk):
            piece = data[i:i + self.chunk]
            self._next_free += len(piece) / self.rate
            self._queue.put_nowait((self._next_free + self.latency, piece))

    async def _dispatch(self):
        while True:
            due, piece = await self._queue.get()
            delay = due - time.monotonic()
            if delay > 0:
                await asyncio.sleep(delay)
            try:
                await self.sink(piece)
            except asyncio.CancelledError:
                raise
            except Exception:
                log.exception("%s pipe sink failed for %dB piece -- dropped",
                              self.name, len(piece))


class LinkSimulator:
    """Bidirectional bridge: app-side bytes <-> FC-side bytes, shaped.

    Wire-in points:
      app_to_fc(data)  -- call with bytes the app wrote (ABF1)
      fc_to_app(data)  -- call with bytes read from the FC serial
      sinks passed to __init__ receive the shaped output on each side.
    """

    def __init__(self, cfg: LinkConfig, to_fc_sink, to_app_sink):
        self.cfg = cfg
        self.stats = Stats()
        self.rng = random.Random(cfg.seed)
        self.up_parser = MspStreamParser("up")
        self.down_parser = MspStreamParser("down")
        self.up_pipe = ShapedPipe("up", cfg.rate_bps, cfg.latency_s, to_fc_sink)
        self.down_pipe = ShapedPipe("down", cfg.rate_bps, cfg.latency_s, to_app_sink)

    def start(self):
        self.up_pipe.start()
        self.down_pipe.start()

    def stop(self):
        self.up_pipe.stop()
        self.down_pipe.stop()

    def app_to_fc(self, data: bytes):
        for kind, item in self.up_parser.feed(data):
            if kind == "raw":
                self.stats.raw_up_bytes += len(item)
                if self.cfg.forward_raw:
                    self.up_pipe.write(item)
                else:
                    log.warning("UP: dropping %dB non-MSP (CLI?) bytes: %s",
                                len(item), item[:32].hex())
                continue
            frame: MspFrame = item
            if self.rng.random() < self.cfg.drop_up:
                self.stats.dropped_up += 1
                log.warning("UP: DROPPED MSP cmd=%d len=%d (injected)",
                            frame.cmd, frame.payload_len)
                continue
            if frame.is_request:
                self.stats.on_request(frame)
            log.debug("UP: MSP v%d cmd=%d len=%d", frame.version, frame.cmd,
                      frame.payload_len)
            self.up_pipe.write(frame.raw)

    def fc_to_app(self, data: bytes):
        for kind, item in self.down_parser.feed(data):
            if kind == "raw":
                self.stats.raw_down_bytes += len(item)
                if self.cfg.forward_raw:
                    self.down_pipe.write(item)
                else:
                    log.warning("DOWN: dropping %dB non-MSP bytes", len(item))
                continue
            frame: MspFrame = item
            if self.rng.random() < self.cfg.drop_down:
                self.stats.dropped_down += 1
                log.warning("DOWN: DROPPED MSP cmd=%d len=%d (injected)",
                            frame.cmd, frame.payload_len)
                continue
            self.stats.on_response(frame)
            log.debug("DOWN: MSP v%d cmd=%d len=%d", frame.version, frame.cmd,
                      frame.payload_len)
            self.down_pipe.write(frame.raw)


async def periodic_stats(stats: Stats, interval: float = 10.0):
    while True:
        await asyncio.sleep(interval)
        log.info("STATS: %s", stats.summary())
