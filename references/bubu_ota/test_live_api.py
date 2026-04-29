# test_live_api.py
#
# Tests the Gemini Live API via WebSocket using the FULL setup JSON
# (same fields the ESP32 sends), not just the bare minimum.
#
# Usage:
#   export GEMINI_API_KEY=your_key_here
#   python3 test_live_api.py
#
# Or run without the environment variable set and you will be prompted to paste
# your key interactively.
#
# Requires: pip3 install websockets

import asyncio
import json
import os
import sys

try:
    import websockets
except ImportError:
    print("ERROR: 'websockets' library not found.")
    print("Install it with: pip3 install websockets")
    sys.exit(1)

LIVE_API_BASE = (
    "wss://generativelanguage.googleapis.com"
    "/ws/google.ai.generativelanguage.v1beta"
    ".GenerativeService.BidiGenerateContent"
)

MODEL = "models/gemini-2.5-flash-native-audio-preview-12-2025"

# Full setup JSON — mirrors exactly what gemini_live_handler.cpp sends on WStype_CONNECTED
FULL_SETUP = {
    "setup": {
        "model": MODEL,
        "generationConfig": {
            "responseModalities": ["AUDIO"],
            "speechConfig": {
                "voiceConfig": {
                    "prebuiltVoiceConfig": {
                        "voiceName": "Puck"
                    }
                }
            }
        },
        "systemInstruction": {
            "parts": [
                {
                    "text": (
                        "You are Bubu, a friendly and cute virtual pet on a small screen. "
                        "Keep responses SHORT (1-3 sentences). Be warm, playful, and helpful. "
                        "Respond in Vietnamese, unless the owner asks you otherwise. "
                        "Do NOT use emoji or special characters."
                    )
                }
            ]
        }
    }
}

TIMEOUT_SECONDS = 10


async def test_full_setup(api_key: str) -> None:
    """
    Opens a WebSocket connection to the Gemini Live API, sends the full
    setup message (same as ESP32), and waits for setupComplete.

    Prints all messages received until setupComplete or timeout.
    """
    url = f"{LIVE_API_BASE}?key={api_key}"
    setup_message = json.dumps(FULL_SETUP)

    print(f"Model:   {MODEL}")
    print(f"Setup:   {setup_message[:120]}...")
    print()

    try:
        async with websockets.connect(url) as ws:
            print("WebSocket connected — sending setup...")
            await ws.send(setup_message)
            print("Setup sent — waiting for response...")
            print()

            deadline = asyncio.get_event_loop().time() + TIMEOUT_SECONDS
            got_setup_complete = False

            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    print(f"[TIMEOUT] No setupComplete within {TIMEOUT_SECONDS}s")
                    break

                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=remaining)
                    if isinstance(raw, bytes):
                        raw = raw.decode("utf-8")

                    print(f"  RX ({len(raw)} bytes): {raw[:400]}")

                    try:
                        msg = json.loads(raw)
                        if "setupComplete" in msg:
                            print()
                            print("[OK] setupComplete received — model accepted the full setup!")
                            got_setup_complete = True
                            break
                        elif "error" in msg:
                            print()
                            print(f"[ERROR] Server returned error: {msg}")
                            break
                    except json.JSONDecodeError:
                        print("       (not valid JSON)")

                except asyncio.TimeoutError:
                    print(f"[TIMEOUT] No response within {TIMEOUT_SECONDS}s")
                    break

            if not got_setup_complete:
                print()
                print("[FAIL] Did not receive setupComplete")

    except websockets.exceptions.ConnectionClosedError as exc:
        print(f"[FAIL] Connection closed: code={exc.code} reason={exc.reason!r}")
    except websockets.exceptions.InvalidStatus as exc:
        print(f"[FAIL] HTTP rejection: {exc.response.status_code} — {exc}")
    except websockets.exceptions.WebSocketException as exc:
        print(f"[FAIL] WebSocket error: {exc}")
    except OSError as exc:
        print(f"[FAIL] Network error: {exc}")
    except Exception as exc:  # noqa: BLE001
        print(f"[FAIL] Unexpected error: {exc}")


async def main() -> None:
    # --- Resolve API key ---
    api_key = os.environ.get("GEMINI_API_KEY", "").strip()
    if not api_key:
        print("GEMINI_API_KEY environment variable is not set.")
        try:
            api_key = input("Paste your Gemini API key: ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\nAborted.")
            sys.exit(1)
    if not api_key:
        print("ERROR: No API key provided. Exiting.")
        sys.exit(1)

    print()
    print("=" * 60)
    print("  Gemini Live API — full setup validation")
    print("=" * 60)
    print()

    await test_full_setup(api_key)

    print()
    print("=" * 60)


if __name__ == "__main__":
    asyncio.run(main())
