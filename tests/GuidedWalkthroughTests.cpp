#include "../GuidedWalkthrough.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using guided_walkthrough::Catalog;
using guided_walkthrough::State;

namespace {

void Expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    wchar_t tempPath[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH)
      throw std::runtime_error("GetTempPathW failed");
    for (unsigned attempt = 0; attempt != 64; ++attempt) {
      path_ = fs::path(tempPath) /
              (L"ctSpaces-guide-test-" +
               std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(GetTickCount64()) + L"-" +
               std::to_wstring(attempt));
      if (CreateDirectoryW(path_.c_str(), nullptr))
        return;
      if (GetLastError() != ERROR_ALREADY_EXISTS)
        break;
    }
    throw std::runtime_error("could not create a unique guide fixture");
  }

  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  const fs::path &path() const { return path_; }

private:
  fs::path path_;
};

void WriteText(const fs::path &path, const std::string &text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Expect(static_cast<bool>(output), "could not create guide fixture");
  output << text;
  output.flush();
  Expect(static_cast<bool>(output), "could not write guide fixture");
}

std::wstring ReadIni(const fs::path &path, const wchar_t *section,
                     const wchar_t *key) {
  wchar_t value[64]{};
  GetPrivateProfileStringW(section, key, L"", value,
                           static_cast<DWORD>(std::size(value)),
                           path.c_str());
  return value;
}

void TestCatalogContract() {
  const auto &topics = Catalog();
  Expect(topics.size() == 12, "the walkthrough catalog must contain 12 topics");

  std::set<std::wstring> ids;
  unsigned announced = 0;
  size_t announcedIndex = topics.size();
  for (size_t index = 0; index < topics.size(); ++index) {
    const auto &topic = topics[index];
    Expect(topic.id && topic.id[0], "walkthrough topic id is empty");
    Expect(topic.title && topic.title[0], "walkthrough topic title is empty");
    Expect(topic.location && topic.location[0],
           "walkthrough topic location is empty");
    Expect(topic.body && topic.body[0], "walkthrough topic body is empty");
    Expect(topic.revision != 0, "walkthrough topic revision is zero");
    Expect(ids.emplace(topic.id).second,
           "walkthrough topic ids must be stable and unique");
    if (topic.announce) {
      ++announced;
      announcedIndex = index;
    }
  }
  Expect(announced == 1 && announcedIndex + 1 == topics.size(),
         "only the current help/new-features topic may be announced");
}

void TestRevisionProgressContract() {
  const auto &topics = Catalog();
  const std::set<std::wstring> revised = {
      L"create_open", L"sessions", L"pins_shortcuts", L"identity",
      L"updates"};
  const std::set<std::wstring> stable = {
      L"welcome", L"browsers_restore", L"temporary_default", L"organize",
      L"cleanup", L"backup_restore", L"appearance"};
  Expect(revised.size() + stable.size() == topics.size(),
         "revision contract does not cover the catalog");
  for (const auto &topic : topics) {
    const std::wstring id = topic.id;
    Expect((revised.count(id) != 0) || (stable.count(id) != 0),
           "revision contract contains an unknown topic");
    Expect(topic.revision == (revised.count(id) != 0 ? 2u : 1u),
           "topic revision does not match the guide progress contract");
  }

  State oldRead;
  oldRead.readRevisions.assign(topics.size(), 1);
  for (size_t index = 0; index < topics.size(); ++index) {
    const std::wstring id = topics[index].id;
    const bool shouldBeUnread = revised.count(id) != 0;
    Expect(guided_walkthrough::IsUnread(oldRead, index) == shouldBeUnread,
           "old revision state did not expose exactly the changed topics");
  }
  Expect(guided_walkthrough::HasUnreadAnnouncement(oldRead),
         "old revision state did not expose the announced update");
  const auto unread = guided_walkthrough::UnreadAnnouncementIndices(oldRead);
  Expect(unread.size() == 1 &&
             std::wstring(topics[unread.front()].id) == L"updates",
         "old revision state exposed a non-announced topic in What's new");

  State futureRead;
  futureRead.readRevisions.assign(topics.size(), 999);
  for (size_t index = 0; index < topics.size(); ++index)
    Expect(!guided_walkthrough::IsUnread(futureRead, index),
           "a future revision was treated as unread");
  Expect(!guided_walkthrough::HasUnreadAnnouncement(futureRead),
         "a future revision left What's new announced");
  for (size_t index = 0; index < topics.size(); ++index) {
    Expect(guided_walkthrough::ReadTopicMutations(futureRead, index, false)
                   .empty(),
           "a future revision generated a redundant read mutation");
    const unsigned before = futureRead.readRevisions[index];
    guided_walkthrough::ApplyTopicRead(futureRead, index);
    Expect(futureRead.readRevisions[index] == before,
           "applying a current topic downgraded a future revision");
  }
}

void TestFreshFolderDetection() {
  TemporaryDirectory temporary;
  const fs::path absent = temporary.path() / L"absent";
  Expect(guided_walkthrough::IsFreshDataFolder(absent),
         "an absent data folder is not recognized as fresh");

  const fs::path empty = temporary.path() / L"empty";
  Expect(fs::create_directory(empty), "could not create empty fixture");
  Expect(guided_walkthrough::IsFreshDataFolder(empty),
         "an empty data folder is not recognized as fresh");

  for (const wchar_t *name : {L"config.ini", L"Sites", L"Default", L"Temp"}) {
    const fs::path knownPath = empty / name;
    if (std::wstring(name) == L"config.ini")
      WriteText(knownPath, "[guide]\r\nwelcome_pending=0\r\n");
    else
      fs::create_directory(knownPath);
    Expect(!guided_walkthrough::IsFreshDataFolder(empty),
           "known config/profile data was treated as a fresh folder");
    if (std::wstring(name) == L"config.ini")
      fs::remove(knownPath);
    else
      fs::remove_all(knownPath);
  }

  const fs::path regularFile = temporary.path() / L"file";
  WriteText(regularFile, "not a data directory");
  Expect(!guided_walkthrough::IsFreshDataFolder(regularFile),
         "a regular file was treated as a fresh data folder");
}

void TestStateAndRevisionSemantics() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"config.ini";
  WriteText(config,
            "[guide]\r\n"
            "welcome_handled=1\r\n"
            "welcome_pending=1\r\n"
            "read_welcome=999\r\n"
            "read_updates=0\r\n");

  State state = guided_walkthrough::LoadState(config);
  Expect(state.welcomeHandled && state.welcomePending,
         "welcome pending/handled state did not round-trip");
  Expect(!guided_walkthrough::IsUnread(state, 0),
         "a future topic revision was considered unread");
  Expect(guided_walkthrough::HasUnreadAnnouncement(state),
         "an unread announced revision was not detected");

  const auto unread = guided_walkthrough::UnreadAnnouncementIndices(state);
  Expect(unread.size() == 1 && unread.front() == Catalog().size() - 1,
         "What's new returned a non-announced or unstable topic");
  const auto mutations = guided_walkthrough::ReadTopicMutations(
      state, unread.front(), false);
  Expect(mutations.size() == 1 && mutations.front().key.has_value() &&
             mutations.front().value.has_value(),
         "reading one What's new topic did not produce one revision mutation");

  // Applying a read marker must never downgrade a value written by a newer
  // build, even when a stale state object is used after an update.
  state.readRevisions.resize(Catalog().size(), 0);
  state.readRevisions.back() = Catalog().back().revision + 99;
  guided_walkthrough::ApplyTopicRead(state, state.readRevisions.size() - 1);
  Expect(state.readRevisions.back() == Catalog().back().revision + 99,
         "a future guide revision was downgraded");

  const auto pending = guided_walkthrough::WelcomePendingMutations();
  const auto handled = guided_walkthrough::WelcomeHandledMutations();
  Expect(pending.size() == 1 && handled.size() == 2,
         "welcome state mutation shape changed unexpectedly");
  Expect(pending.front().key == std::wstring(L"welcome_pending") &&
             pending.front().value == std::wstring(L"1"),
         "pending welcome mutation is not durable");
  Expect(ReadIni(config, L"guide", L"welcome_pending") == L"1",
         "state fixture was not readable through Windows INI APIs");
}

} // namespace

int wmain() {
  try {
    TestCatalogContract();
    TestRevisionProgressContract();
    TestFreshFolderDetection();
    TestStateAndRevisionSemantics();
    std::wcout << L"Guided walkthrough unit tests passed: catalog, fresh-data "
                   L"detection, pending state, announced revisions, and "
                   L"non-downgrade semantics.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Guided walkthrough unit test failed: " << error.what()
              << '\n';
    return 1;
  }
}
