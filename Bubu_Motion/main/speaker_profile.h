#pragma once

#include <string>

namespace SpeakerProfile {

// Initializes profile storage on first use.
void Begin();

// Registers or updates the single owner profile from explicit self-identification.
bool Register(const std::string& name, std::string* error_out = nullptr);

// Returns the current known speaker in single-owner mode, or "unknown".
std::string Identify();

}  // namespace SpeakerProfile
