#pragma once

#include <string>

namespace ConfirmationEvaluator {

// Normalizes speech-to-text text for deterministic local matching.
// Behavior: lowercase, punctuation->space, collapse whitespace, trim.
std::string Normalize(const std::string& text);

// Returns true when normalized response contains normalized required_phrase.
bool Matches(const std::string& response, const std::string& required_phrase);

}  // namespace ConfirmationEvaluator
