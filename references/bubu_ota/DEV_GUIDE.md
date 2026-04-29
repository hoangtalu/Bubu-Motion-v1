# Bubu Developer Guide

System reference for the Bubu virtual pet firmware.

---

## Architecture Overview

```
main.cpp                    Entry point, system initialization, main loop
    |
    +-- DisplaySystem       Eyes, emotions, legacy emotions, UI rendering
    +-- MenuSystem          LVGL panels, settings, WiFi connect
    +-- CareSystem          Hunger, mood, energy, cleanliness stats
    +-- LevelSystem         XP, levels, feature unlocks
    +-- PetHearing          Microphone input, voice detection, mood reactions
    +-- SoundSystem         I2S audio output, sound effects
    +-- TouchSystem         Capacitive touch via CST816
```

Hardware reference: See `ESP32S3 dev manual.md`

---

## Display System

**Files**: `src/display_system.cpp`, `include/display_system.h`

### Eye Emotions

Standard emotions set via `DisplaySystem_setEmotion(EyeEmotion)`:

| Emotion | Description | Unlock Level |
|---------|-------------|--------------|
| `EYE_EMO_IDLE` | Default neutral | 1 |
| `EYE_EMO_SAD1` | Sad eyes | 1 |
| `EYE_EMO_HAPPY1` | Happy eyes | 1 |
| `EYE_EMO_EXCITED` | Bouncy excited | 2 |
| `EYE_EMO_ANGRY1` | Angry eyes | 4 |
| `EYE_EMO_CURIOUS` | Looking around | 1 |
| `EYE_EMO_WORRIED1` | Worried expression | 1 |
| `EYE_EMO_LOVE` | Heart eyes | 7 |
| `EYE_EMO_TIRED` | Droopy tired | 1 |

### Legacy Emotions

Special full-screen animations from old system. Triggered via `DisplaySystem_triggerLegacyEmotion()`:

| Emotion | Visual | Unlock Level |
|---------|--------|--------------|
| `LEGACY_EMO_CYCLOP` | Single giant eye, moving pupil | 5 |
| `LEGACY_EMO_LOVE` | Heart eyes, pulsing cheeks | 6 |
| `LEGACY_EMO_DRUNK` | Whirlpool spiral eyes | 7 |

**Auto-trigger**: At level 5+, legacy emotions have 20% chance to play automatically every 7-15 seconds. Only unlocked emotions are eligible.

**Duration**: 8 seconds default.

### Idle Behaviors

| Behavior | Description | Unlock Level |
|----------|-------------|--------------|
| `IDLE_JITTER` | Subtle eye movement | 3 |
| `IDLE_GIGGLE` | Small bounce | 5 |
| `IDLE_JUDGING` | Suspicious look | 10 |
| `IDLE_SPEED_FAST` | Faster animations | 12 |

---

## Pet Hearing

**Files**: `src/pet_hearing.cpp`, `include/pet_hearing.h`

### Voice Detection

Distinguishes human voice from background noise using 3 signals:

1. **Rhythm Score** - Threshold crossings (voice is bursty on-off, noise is steady)
2. **Variance Score** - Level fluctuation (voice wiggles, noise is flat)
3. **Transient Score** - Fast attacks (voice syllables have quick rise)

Combined into `voice_score` (0-100). Score >= 40 = voice-like.

### Mood States

| Mood | Trigger | Effect |
|------|---------|--------|
| `MOOD_ALERT` | Sound detected, not voice-like | Curious eyes |
| `MOOD_HAPPY` | Voice, normal volume | +2 mood, happy eyes |
| `MOOD_EXCITED` | Voice, loud | +5 mood, excited animation |
| `MOOD_CALM` | Voice, soft | Relaxed |
| `MOOD_LONELY` | No voice for 30s | -3 mood, sad eyes |
| `MOOD_STARTLED` | Sudden loud spike | Instant worried eyes |

### API

```cpp
petHearing.begin();              // Initialize (called after SoundSystem)
petHearing.update();             // Call in main loop
petHearing.getMood();            // Current PetMood
petHearing.getLevel();           // 0-100 audio level
petHearing.getVoiceScore();      // 0-100 voice likelihood
petHearing.heardSomething();     // Any sound in last 3s
petHearing.heardVoice();         // Voice-like sound in last 3s
petHearing.setEnabled(bool);     // Enable/disable
petHearing.suppressFor(ms);      // Ignore mic during speaker playback
```

### Tuning

In `pet_hearing.cpp`:

```cpp
#define SILENCE          20      // Below = quiet
#define SOFT             80      // Soft voice
#define NORMAL           300     // Normal talking
#define LOUD             1500    // Excited/loud
#define STARTLE          4000    // Sudden spike
#define VOICE_SCORE_MIN  40      // Min score to consider voice
#define LONELY_TIME      30000   // 30s no voice = lonely
```

---

## Sound System

**Files**: `src/sound/sound_system.cpp`, `src/sound/sound_system.h`

### Hardware

- Codec: MAX98357 (I2S)
- Speaker: 8 ohm recommended
- GPIO: WS=4, BCLK=5, DIN=7

### Available Sounds

```cpp
SoundSystem::blinkClink();           // Eye blink
SoundSystem::eyeSwoosh(strength);    // Eye movement (0.0-1.0)
SoundSystem::eyeJitter(strength);    // Eye jitter buzz
SoundSystem::happyPip(strength);     // Happy chirp
SoundSystem::sadSigh(strength);      // Sad descending tone
SoundSystem::mute(bool);             // Mute all
```

### Microphone Suppression

When playing sounds, suppress mic input to prevent feedback:

```cpp
petHearing.suppressFor(300);  // Ignore mic for 300ms
SoundSystem::happyPip(0.8f);
```

---

## Level System

**Files**: `src/level_system.cpp`, `include/level_system.h`

### XP & Levels

- XP required per level: `50 + (level * 25)`
- Level 1 -> 2: 50 XP
- Level 4 -> 5: 125 XP
- Total to level 5: 350 XP

### Unlock Progression

| Level | Unlocks |
|-------|---------|
| 1 | Core emotions (IDLE, SAD1, HAPPY1, CURIOUS, WORRIED1, TIRED) |
| 2 | EXCITED |
| 3 | IDLE_JITTER |
| 4 | ANGRY1 |
| 5 | IDLE_GIGGLE, LEGACY_EMO_CYCLOP |
| 6 | LEGACY_EMO_LOVE |
| 7 | LOVE (normal), LEGACY_EMO_DRUNK |
| 10 | IDLE_JUDGING |
| 12 | IDLE_SPEED_FAST |

### API

```cpp
LevelSystem::begin();                    // Load saved state
LevelSystem::addXP(amount);              // Grant XP
LevelSystem::getLevel();                 // Current level
LevelSystem::getXP();                    // Current XP
LevelSystem::isUnlocked(Feature);        // Check if feature available
```

---

## Care System

**Files**: `src/care_system.cpp`, `include/care_system.h`

### Stats (0-100)

| Stat | Decay Rate | Effect |
|------|------------|--------|
| Hunger | -1/min | Low = sad |
| Mood | -0.5/min | Low = sad, high = happy |
| Energy | -0.3/min | Low = tired |
| Cleanliness | -0.2/min | Low = dirty |

### API

```cpp
CareSystem::getHunger();
CareSystem::getMood();
CareSystem::getEnergy();
CareSystem::getCleanliness();
CareSystem::addMood(amount);      // Can be negative
CareSystem::feed();               // Restore hunger
CareSystem::play();               // Restore mood
CareSystem::sleep();              // Restore energy
CareSystem::clean();              // Restore cleanliness
```

---

## Menu System

**Files**: `src/menu_system.cpp`, `include/menu_system.h`

### Panels

- **Main Menu** - Carousel with: Stats, Options, Games, Battery, Settings, Connect
- **Stats Panel** - View hunger/mood/energy/cleanliness
- **Options Panel** - Feed/Play/Sleep/Clean actions
- **Connect Panel** - WiFi network selection
- **Keyboard Panel** - T9-style password input

### Navigation

- Touch outside eyes -> Open menu
- Swipe up/down -> Navigate items
- Tap -> Select
- Physical buttons: UP (P3), DOWN (P5) on TCA6408

### Keyboard

T9-style input for WiFi passwords:
- Numbers 1-9 cycle through letters (2=abc, 3=def, etc.)
- UP arrow = caps toggle
- OK = submit
- LEFT arrow = backspace

---

## Main Loop Structure

```cpp
void loop() {
    uint32_t now = millis();

    // Touch input
    TouchSystem::update();

    // Audio input & voice detection
    petHearing.update();

    // Display rendering
    DisplaySystem_update();

    // Menu UI
    lv_timer_handler();

    // Care stat decay
    CareSystem::update();

    // Level/XP save
    LevelSystem::update();

    delay(1);
}
```

---

## Debug Logging

Each system has its own log prefix:

```
[DisplaySystem] ...
[MenuSystem] ...
[Hearing] Mood: happy (level: 450, voice: 65 [r:70 v:55 t:40])
[LevelSystem] Level up! Now level 5
[CareSystem] ...
[SoundSystem] ...
```

---

## Build & Upload

```bash
# Build
pio run

# Upload
pio run --target upload

# Monitor serial
pio device monitor
```

---

## File Structure

```
bubu_clean/
├── src/
│   ├── main.cpp              # Entry point
│   ├── display_system.cpp    # Display & emotions
│   ├── menu_system.cpp       # LVGL menus
│   ├── care_system.cpp       # Pet stats
│   ├── level_system.cpp      # XP & progression
│   ├── pet_hearing.cpp       # Mic input & voice detection
│   ├── legacy_emotions.cpp   # Ported special emotions
│   ├── touch_system.cpp      # Touch handling
│   └── sound/
│       └── sound_system.cpp  # Audio output
├── include/
│   ├── display_system.h
│   ├── menu_system.h
│   ├── care_system.h
│   ├── level_system.h
│   ├── pet_hearing.h
│   ├── legacy_emotions.h
│   └── touch_system.h
├── ESP32S3 dev manual.md     # Hardware reference
└── DEV_GUIDE.md              # This file
```
