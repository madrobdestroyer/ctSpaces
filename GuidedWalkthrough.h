#pragma once

#include "ConfigPersistence.h"

#include <filesystem>
#include <string>
#include <vector>

namespace guided_walkthrough {

struct Topic {
  const wchar_t *id;
  unsigned revision;
  const wchar_t *title;
  const wchar_t *location;
  const wchar_t *body;
  unsigned targetCommand;
  bool announce;
  const wchar_t *announcementTitle = nullptr;
  const wchar_t *announcementBody = nullptr;
};

struct State {
  bool welcomeHandled = false;
  bool welcomePending = false;
  std::vector<unsigned> readRevisions;
};

const std::vector<Topic> &Catalog();

// This probe is intentionally conservative: an inaccessible path, an existing
// configuration, or any known client/default/temporary profile means the data
// folder is not treated as a first run.
bool IsFreshDataFolder(const std::filesystem::path &dataDirectory);

State LoadState(const std::filesystem::path &configPath);
bool IsUnread(const State &state, size_t topicIndex);
bool HasUnreadAnnouncement(const State &state);
std::vector<size_t> AllTopicIndices();
std::vector<size_t> UnreadAnnouncementIndices(const State &state);

std::vector<config_persistence::IniMutation>
WelcomePendingMutations();
std::vector<config_persistence::IniMutation>
WelcomeHandledMutations();
std::vector<config_persistence::IniMutation>
ReadTopicMutations(const State &state, size_t topicIndex,
                   bool alsoHandleWelcome);

void ApplyWelcomePending(State &state);
void ApplyWelcomeHandled(State &state);
void ApplyTopicRead(State &state, size_t topicIndex);

} // namespace guided_walkthrough
