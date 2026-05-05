#include "confirmation_evaluator.h"

#include <cctype>

namespace {

void PushSpaceIfNeeded(std::string* out) {
    if (!out->empty() && out->back() != ' ') {
        out->push_back(' ');
    }
}

}  // namespace

namespace ConfirmationEvaluator {

std::string Normalize(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());

    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
            continue;
        }
        if (std::isspace(ch) || std::ispunct(ch)) {
            PushSpaceIfNeeded(&normalized);
        }
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

bool Matches(const std::string& response, const std::string& required_phrase) {
    const std::string normalized_required = Normalize(required_phrase);
    if (normalized_required.empty()) {
        return false;
    }
    const std::string normalized_response = Normalize(response);
    return normalized_response.find(normalized_required) != std::string::npos;
}

}  // namespace ConfirmationEvaluator
