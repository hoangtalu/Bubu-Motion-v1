# Testing Guide - Reminder & Note System

## Setup

1. **Upload firmware** to your device
2. **Connect to WiFi** so the device gets NTP time
3. **Open Serial Monitor** at 115200 baud

## Demo Commands

Type these commands in the Serial Monitor:

### Initial Setup
```
demo-help
```
Shows all available demo commands.

```
demo-setup
```
Creates test data:
- 2 reminders (one in 10 seconds, one daily in 30 seconds)
- 3 notes (shopping, ideas, important)

**IMPORTANT:** Make sure WiFi is connected first so you have valid time!

### View Data
```
demo-reminders
```
Lists all reminders with IDs and trigger times.

```
demo-notes
```
Lists all notes with content and metadata.

### Test Display
```
demo-show1
```
Shows note ID 1 on the pet's screen (blue notification popup).

```
demo-show2
```
Shows note ID 2 on screen.

```
demo-show3
```
Shows note ID 3 on screen.

### Test Sound
```
demo-chime
```
Plays the reminder notification sound (2-tone bell).

## What to Expect

### After `demo-setup`:
1. Wait 10 seconds → reminder notification appears on screen with chime sound
2. Wait 30 seconds → another reminder triggers
3. Reminders auto-dismiss after 5 seconds

### When showing notes:
- Blue-bordered popup appears on screen
- Note title and content are displayed
- Scrolls if content is long
- Auto-dismisses after 10 seconds

### Reminder notification:
- Orange border with 🔔 bell emoji
- Plays gentle 2-tone chime (800Hz → 1000Hz)
- Shows reminder title
- Auto-dismisses after 5 seconds

### Note display:
- Blue border with 📝 note emoji
- Shows title and full content
- Scrollable for long notes
- Auto-dismisses after 10 seconds

## Testing Checklist

- [ ] WiFi connected (check serial output for NTP sync)
- [ ] Run `demo-setup` successfully
- [ ] Run `demo-reminders` - see 2 reminders listed
- [ ] Run `demo-notes` - see 3 notes listed
- [ ] Run `demo-show1` - note appears on screen
- [ ] Run `demo-chime` - hear the bell sound
- [ ] Wait 10 seconds - first reminder triggers automatically
- [ ] Wait 30 seconds - second reminder triggers
- [ ] Verify reminders show on screen with sound
- [ ] Verify reminders auto-dismiss

## Troubleshooting

**"No valid time" error:**
- Device needs NTP time from internet
- Connect to WiFi first
- Wait a few seconds for NTP sync

**Notes don't show on screen:**
- LVGL might not be initialized yet
- Wait until the pet's display is fully loaded
- Try the command again

**Reminders don't trigger:**
- Check that you have valid time (>0)
- Use `demo-reminders` to see trigger timestamps
- Compare with current Unix time

**No sound:**
- Check if mute is enabled
- Check if chat is streaming (sounds suppressed during chat)
- Verify speaker is connected

## Storage

Data is saved in SPIFFS:
- `/reminders.json` - all reminders
- `/notes.json` - all notes

Data persists across reboots. Use `demo-setup` to reset test data.

## Next Steps

Once basic demo works, you can:
1. Integrate with Gemini voice commands (see `TOOLS_INTEGRATION_GUIDE.md`)
2. Add menu system for touch control
3. Create custom reminders via API
4. Build a companion app

## Example Session

```
> demo-help
=== Tools Demo Commands ===
demo-setup      - Create test reminders and notes
demo-reminders  - List all reminders
demo-notes      - List all notes
demo-show1      - Show note ID 1 on screen
demo-chime      - Play reminder sound
demo-help       - Show this help

> demo-setup
[Demo] Added 2 test reminders
[Demo] Added 3 test notes

> demo-reminders
=== Reminders (2) ===
ID 1: Test reminder in 10s! (at 1709567890)
ID 2: Daily task (at 1709567910) [REPEAT]

> demo-notes
=== Notes (3) ===
ID 1: Shopping List
  Content: Milk, eggs, bread, butter
  Category: 1, Pinned: NO
ID 2: Ideas
  Content: Build a robot pet with AI voice assistant
  Category: 0, Pinned: NO
ID 3: Important
  Content: Remember to backup the code!
  Category: 2, Pinned: YES

> demo-show1
[Demo] Showing note: Shopping List
[Note appears on pet's screen]

> demo-chime
[Demo] Playing reminder chime
[Bell sound plays]

[10 seconds pass...]
[Reminder] Triggered: Test reminder in 10s!
[Orange notification appears on screen with chime]

[30 seconds pass...]
[Reminder] Triggered: Daily task
[Another notification appears]
```

Enjoy testing! 🎉
