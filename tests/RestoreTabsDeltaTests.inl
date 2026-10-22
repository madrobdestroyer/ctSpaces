// Real staged settings, concurrent native replacement and immediate user retry.
// The enclosing harness intercepts presentation and creates no native windows.
static std::string EncodeRestorePreferences(const std::vector<std::wstring> &values) {
  std::wstring text=L"[restore_tabs]\r\ndisabled_count="+std::to_wstring(values.size())+L"\r\nfuture_key=KEEP\r\n";
  for(size_t i=0;i<values.size();++i)text+=L"disabled_client"+std::to_wstring(i)+L"="+values[i]+L"\r\n";
  text+=L"[independent]\r\nmarker=NEWER WORK\r\n";
  std::string bytes("\xFF\xFE",2);bytes.append(reinterpret_cast<const char *>(text.data()),text.size()*sizeof(wchar_t));return bytes;
}
static bool WriteRestoreFixture(const fs::path &path,const std::string &bytes) {
  std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<bytes;return static_cast<bool>(output);
}
struct RestorePreferenceConflict {std::string bytes;bool attempted=false,changed=false;};
static void ReplaceRestoreSettings(const fs::path &live,const fs::path &,
    config_persistence::IniPublicationBoundary boundary,void *context) {
  if(boundary!=config_persistence::IniPublicationBoundary::BeforePublish)return;
  auto &attack=*static_cast<RestorePreferenceConflict *>(context);attack.attempted=true;
  const auto external=live.parent_path()/L"external-settings.ini";
  attack.changed=WriteRestoreFixture(external,attack.bytes)&&
      ReplaceFileW(live.c_str(),external.c_str(),nullptr,REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
  g_sessions={{L"Gamma",BrowserKind::Brave,0x7ffff658}};g_iSelectedSessionTab=1;g_selectedBrowser=BrowserKind::Brave;
}
static void TestRestoreTabsDelta(const fs::path &base) {
  const std::wstring alpha=L"Alpha \u00C5ngstr\u00F6m \u4E2D\u6587";
  unsigned cases=0;
  const auto prepare=[&]() {
    g_sDataDir=base/std::to_wstring(++cases);g_sConfigPath=g_sDataDir/L"config.ini";
    for(const auto &name:{alpha,std::wstring(L"Beta"),std::wstring(L"Gamma")})fs::create_directories(g_sDataDir/L"Sites"/name);
    g_sessions={{alpha,BrowserKind::Edge,0x7ffff651}};g_iSelectedSessionTab=1;g_selectedBrowser=BrowserKind::Edge;
  };
  for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox})for(bool disabled:{false,true}) {
    prepare();const auto target=MakeClientBrowserPreferenceKey(alpha,browser);
    std::vector<std::wstring> initial{MakeClientBrowserPreferenceKey(L"Beta",BrowserKind::Firefox)};
    if(disabled)initial.push_back(target);
    Seed(initial);Check(SaveRestoreTabsPreferences(),"Delta fixture persists original restore preferences");
    auto foreign=initial;foreign.push_back(MakeClientBrowserPreferenceKey(L"Gamma",BrowserKind::Edge));
    RestorePreferenceConflict attack{EncodeRestorePreferences(foreign)};
    config_persistence::SetIniPublicationHookForTest(ReplaceRestoreSettings,&attack);
    g_sessions={{alpha,browser,0x7ffff651}};g_iSelectedSessionTab=1;
    const unsigned beforeNotices=notices;ToggleRestoreTabsForSelectedClient();
    config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);
    Check(attack.attempted&&attack.changed&&notices==beforeNotices+1,"A real competing settings write is retained and reported once");
    Check(Preferences()==initial&&Bytes(g_sConfigPath)==attack.bytes,"Failed toggle leaves memory and the newer saved file intact without retargeting to the changed session");
    // The actual UI offers no manual settings reload. Retry immediately.
    g_sessions={{alpha,browser,0x7ffff651}};g_iSelectedSessionTab=1;ToggleRestoreTabsForSelectedClient();
    auto wanted=foreign;const auto position=std::find(wanted.begin(),wanted.end(),target);
    if(position==wanted.end())wanted.push_back(target);else wanted.erase(position);
    Check(Preferences()==wanted,"Immediate retry applies only its original client/browser delta and retains newer unrelated preferences");
    Seed({});LoadRestoreTabsPreferences();Check(Preferences()==wanted,"Immediate-retry result survives a fresh native settings reload");
    wchar_t preserved[32]{};GetPrivateProfileStringW(L"restore_tabs",L"future_key",L"",preserved,32,g_sConfigPath.c_str());
    Check(std::wstring(preserved)==L"KEEP","Targeted restore update preserves unknown keys in the same settings section");
  }
  // A competing change committed before a new click is also preserved.
  prepare();const std::vector<std::wstring> initial{L"Beta|firefox"},foreign{L"Beta|firefox",L"Gamma|edge"};
  Seed(initial);Check(SaveRestoreTabsPreferences(),"Already-changed-disk fixture starts from saved settings");
  Check(WriteRestoreFixture(g_sConfigPath,EncodeRestorePreferences(foreign)),"Independent preferences commit before the user next clicks");
  ToggleRestoreTabsForSelectedClient();auto wanted=foreign;wanted.push_back(MakeClientBrowserPreferenceKey(alpha,BrowserKind::Edge));
  Check(Preferences()==wanted,"A fresh toggle merges with current disk preferences even if UI memory predates them");
  for(const auto &value:{std::wstring(L"-1"),std::wstring(L"2049"),std::wstring(L"invalid"),std::wstring(L"1\r\ndisabled_client0="),std::wstring(L"1\r\ndisabled_client0=")+std::wstring(520,L'x')}) {
    prepare();Seed(initial);const std::wstring text=L"[restore_tabs]\r\ndisabled_count="+value+L"\r\n";
    std::string bytes("\xFF\xFE",2);bytes.append(reinterpret_cast<const char *>(text.data()),text.size()*sizeof(wchar_t));
    Check(WriteRestoreFixture(g_sConfigPath,bytes),"Seed private malformed or overlong restore settings");
    const auto previousNotices=notices;ToggleRestoreTabsForSelectedClient();
    Check(notices==previousNotices+1&&Preferences()==initial&&Bytes(g_sConfigPath)==bytes,"Malformed count, missing row or truncated value is rejected without changing memory or saved bytes");
  }
  std::cout<<"Restore tabs targeted delta: "<<cases<<" conflict, immediate-retry, preexisting-change and malformed-list scenarios passed.\n";
}
