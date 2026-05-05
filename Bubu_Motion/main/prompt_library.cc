#include "prompt_library.h"

#include <array>
#include <string_view>

#include <esp_random.h>

namespace {

std::string GetNameValue(const PromptContext& context) {
    return context.name.empty() ? "friend" : context.name;
}

std::string GetStatValue(const PromptContext& context) {
    switch (context.stat) {
        case CareStatType::kHunger:
            return "hunger";
        case CareStatType::kMood:
            return "mood";
        case CareStatType::kEnergy:
            return "energy";
        case CareStatType::kCleanliness:
            return "cleanliness";
        case CareStatType::kNone:
        default:
            return "care";
    }
}

std::string GetLevelValue(const PromptContext& context) {
    if (context.level > 0) {
        return std::to_string(context.level);
    }
    return "a new level";
}

std::string GetTimeOfDayValue(const PromptContext& context) {
    return context.time_of_day.empty() ? "today" : context.time_of_day;
}

std::string GetReminderMessageValue(const PromptContext& context) {
    return context.reminder_message.empty() ? "you have something important to remember"
                                            : context.reminder_message;
}

std::string GetConfirmPhraseValue(const PromptContext& context) {
    return context.confirm_phrase.empty() ? "" : context.confirm_phrase;
}

void ReplaceAll(std::string* text, std::string_view needle, const std::string& replacement) {
    if (text == nullptr || needle.empty()) {
        return;
    }

    size_t pos = 0;
    while ((pos = text->find(needle, pos)) != std::string::npos) {
        text->replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
}

std::string FillTemplate(std::string_view templ, const PromptContext& context) {
    std::string filled(templ);
    ReplaceAll(&filled, "{name}", GetNameValue(context));
    ReplaceAll(&filled, "{stat}", GetStatValue(context));
    ReplaceAll(&filled, "{level}", GetLevelValue(context));
    ReplaceAll(&filled, "{time_of_day}", GetTimeOfDayValue(context));
    ReplaceAll(&filled, "{reminder_message}", GetReminderMessageValue(context));
    ReplaceAll(&filled, "{confirm_phrase}", GetConfirmPhraseValue(context));
    // Strip any unreplaced {tokens} to avoid LLM confusion
    size_t start = 0;
    while ((start = filled.find('{', start)) != std::string::npos) {
        size_t end = filled.find('}', start);
        if (end == std::string::npos) {
            break;
        }
        filled.erase(start, end - start + 1);
    }
    return filled;
}

template <size_t N>
std::string PickTemplate(const std::array<std::string_view, N>& templates,
                         const PromptContext& context) {
    static_assert(N > 0, "Prompt template list must not be empty");
    const size_t index = static_cast<size_t>(esp_random() % N);
    return FillTemplate(templates[index], context);
}

// Legacy LLM-oriented proactive prompts (kept for future reuse).
//
// constexpr std::array<std::string_view, 3> kMorningTemplates = {{
//     "It is {time_of_day}. Greet {name} warmly, sound fresh and caring, and start a short friendly conversation.",
//     "Give {name} a gentle morning greeting for {time_of_day}, sound cheerful, and invite one easy follow-up response.",
//     "Start the day with {name} in a warm, natural way. Mention that it is {time_of_day} and keep the reply short and spoken.",
// }};
//
// constexpr std::array<std::string_view, 3> kEveningTemplates = {{
//     "It is {time_of_day}. Check in with {name} in a calm, comforting tone and ask one simple evening question.",
//     "Give {name} a gentle evening check-in for {time_of_day}. Sound relaxed, caring, and brief.",
//     "Speak first to {name} with a soft evening tone. Mention {time_of_day} and invite a short response.",
// }};
//
// constexpr std::array<std::string_view, 3> kCareReminderTemplates = {{
//     "Start a caring conversation with {name}. Gently mention that {stat} may need attention and encourage one small action without sounding strict.",
//     "Check in with {name} about {stat}. Sound supportive, playful, and short, like a companion giving a helpful nudge.",
//     "Speak first to {name} and bring up {stat} in a warm, natural way. Encourage care without using alarmist wording.",
// }};
//
// constexpr std::array<std::string_view, 3> kIdleTooLongTemplates = {{
//     "It has been quiet for a while. Start a light conversation with {name}, sound warm and curious, and ask one easy question.",
//     "Check in with {name} after a long quiet stretch. Keep it short, friendly, and inviting.",
//     "Break the silence by speaking to {name} first. Sound natural, gentle, and a little playful.",
// }};
//
// constexpr std::array<std::string_view, 3> kLevelUpTemplates = {{
//     "Celebrate with {name} for reaching {level}. Sound excited and proud, then add one short playful follow-up line.",
//     "Start a happy conversation with {name} about reaching {level}. Keep it brief, upbeat, and spoken aloud naturally.",
//     "Congratulate {name} for hitting {level}. Sound energetic and affectionate without becoming too long.",
// }};
//
// constexpr std::array<std::string_view, 3> kReminderFireTemplates = {{
//     "Remind {name} about this message: {reminder_message}. Speak clearly and kindly. If a confirmation phrase is provided, tell {name} they must say exactly '{confirm_phrase}' to acknowledge.",
//     "Start a reminder for {name}: {reminder_message}. Keep the tone warm but direct. If '{confirm_phrase}' is set, tell them to say that exact phrase to dismiss the reminder.",
//     "Speak first to {name} and deliver this reminder: {reminder_message}. Then tell them: say exactly '{confirm_phrase}' when you have heard and understood.",
// }};

constexpr std::array<std::string_view, 3> kMorningTemplates = {{
    "Good {time_of_day}, {name}. Tiny mission: smile first, coffee second.",
    "Morning check-in for {name}: stretch, sip water, and own the day.",
    "Rise and shine, {name}. New day, new sparkle.",
}};

constexpr std::array<std::string_view, 3> kEveningTemplates = {{
    "Good {time_of_day}, {name}. Slow breaths, soft vibes, gentle landing.",
    "Evening mode for {name}: exhale the noise, keep the peace.",
    "{name}, today was a lot. You still did great.",
}};

constexpr std::array<std::string_view, 3> kCareReminderTemplates = {{
    "Hey {name}, your {stat} bar looks a bit shy. Quick top-up?",
    "Little nudge, {name}: {stat} needs a tiny boost.",
    "{name}, friendly alert: {stat} is dipping. One small fix and we are golden.",
}};

constexpr std::array<std::string_view, 3> kIdleTooLongTemplates = {{
    "Hey {name}, quiet mode detected. You still with me?",
    "{name}, it got too calm in here. Say hi and wake the vibes.",
    "Psst {name}, I miss the chaos. Let us chat for a minute.",
}};

constexpr std::array<std::string_view, 3> kLevelUpTemplates = {{
    "Level up! {name} just hit {level}. Main character energy.",
    "Boom, {name}! Level {level} unlocked. Stylish move.",
    "{name}, you leveled up to {level}. Confetti in my tiny robot heart.",
}};

constexpr std::array<std::string_view, 3> kReminderFireTemplates = {{
    "Hey {name}, reminder time: {reminder_message}",
    "Ping {name}: {reminder_message}",
    "{name}, do not forget this: {reminder_message}",
}};

}  // namespace

std::string PromptLibrary::Get(PromptCategory category, const PromptContext& context) {
    switch (category) {
        case PromptCategory::kMorning:
            return PickTemplate(kMorningTemplates, context);
        case PromptCategory::kEvening:
            return PickTemplate(kEveningTemplates, context);
        case PromptCategory::kCareReminder:
            return PickTemplate(kCareReminderTemplates, context);
        case PromptCategory::kIdleTooLong:
            return PickTemplate(kIdleTooLongTemplates, context);
        case PromptCategory::kLevelUp:
            return PickTemplate(kLevelUpTemplates, context);
        case PromptCategory::kReminderFire:
            return PickTemplate(kReminderFireTemplates, context);
    }

    return "";
}
