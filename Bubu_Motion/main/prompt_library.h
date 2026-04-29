#ifndef _PROMPT_LIBRARY_H_
#define _PROMPT_LIBRARY_H_

#include <string>

enum class PromptCategory {
    kMorning,
    kEvening,
    kCareReminder,
    kIdleTooLong,
    kLevelUp,
    kReminderFire,
};

enum class CareStatType {
    kNone,
    kHunger,
    kMood,
    kEnergy,
    kCleanliness,
};

struct PromptContext {
    std::string name;
    CareStatType stat = CareStatType::kNone;
    int level = 0;
    std::string time_of_day;
    std::string reminder_message;
    std::string confirm_phrase;
};

class PromptLibrary {
public:
    static std::string Get(PromptCategory category, const PromptContext& context);
};

#endif  // _PROMPT_LIBRARY_H_
