// Both real browser preference adapters use the shared native publisher.
// Every file belongs to this process-private fixture; no HWND, browser, shell,
// clipboard, device input or production data directory is accessed.
#define NOMINMAX
#include <windows.h>
#include "RetainedFixture.h"
#include "../BrowserPreferencesJson.h"
#include "../FirefoxPreferences.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs=std::filesystem;
using Boundary=config_persistence::IniPublicationBoundary;
static unsigned checks=0,failures=0,scenarios=0;
static void Check(bool value,const char *label){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';}
static std::string Read(const fs::path &path){std::ifstream input(path,std::ios::binary);return{std::istreambuf_iterator<char>(input),{}};}
static bool Write(const fs::path &path,const std::string &bytes){std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<bytes;return static_cast<bool>(output);}
static bool Contains(const fs::path &directory,const std::string &bytes){for(const auto &entry:fs::recursive_directory_iterator(directory))if(entry.is_regular_file()&&Read(entry.path())==bytes)return true;return false;}

struct Attack {
  unsigned mode=0;
  std::string first,second;
  bool attempted=false,changed=false,secondChanged=false;
};
static void PublicationAttack(const fs::path &live,const fs::path &auxiliary,Boundary boundary,void *context){
  auto &attack=*static_cast<Attack *>(context);
  const auto replacement=live.parent_path()/L"external-source";
  const auto replace=[&](const fs::path &target,const std::string &bytes){return Write(replacement,bytes)&&ReplaceFileW(target.c_str(),replacement.c_str(),nullptr,REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;};
  if(boundary==Boundary::AfterStageCopy&&attack.mode>=1&&attack.mode<=3){
    attack.attempted=true;
    if(attack.mode==1)attack.changed=Write(live,attack.first);
    else if(attack.mode==2)attack.changed=Write(replacement,attack.first)&&MoveFileExW(replacement.c_str(),live.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE;
    else attack.changed=replace(auxiliary,attack.first);
  }else if(boundary==Boundary::BeforePublish&&(attack.mode==4||attack.mode==7)){
    attack.attempted=true;attack.changed=replace(live,attack.first);
  }else if(boundary==Boundary::BeforeNativePublication&&attack.mode==5){
    attack.attempted=true;attack.changed=replace(auxiliary,attack.first);
  }else if(boundary==Boundary::AfterPublish&&attack.mode==6){
    attack.attempted=true;attack.changed=replace(live,attack.first);
  }else if(boundary==Boundary::BeforeRollback&&attack.mode==7){
    attack.secondChanged=replace(live,attack.second);
  }else if(boundary==Boundary::BeforeNativePublication&&attack.mode==10){
    attack.attempted=true;attack.changed=Write(live,attack.first);
  }
}
struct HookReset {~HookReset(){config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);}};

static void AdapterCases(const fs::path &base,bool firefox){
  const std::string original=firefox?
      "// Preserve raw bytes \xe2\x80\x94 \xe6\x97\xa5\nuser_pref(\"keep\", \"ORIGINAL WORK\");\nuser_pref(\"browser.startup.page\", 1);\n":
      "{\"keep\":\"ORIGINAL WORK \xe2\x80\x94 \xe6\x97\xa5\",\"session\":{\"restore_on_startup\":5}}\r\n";
  for(unsigned mode=0;mode<=9;++mode){
    ++scenarios;
    const auto directory=base/(firefox?L"firefox":L"chromium")/std::to_wstring(mode);
    fs::create_directories(directory);
    const auto live=directory/(firefox?L"user.js":L"Preferences");
    if(!Write(live,original))throw std::runtime_error("Cannot seed private browser publication fixture");
    Attack attack;attack.mode=mode;
    attack.first=firefox?"user_pref(\"keep\", \"NEWER EXTERNAL WORK\");\n":"{\"keep\":\"NEWER EXTERNAL WORK\"}";
    attack.second=firefox?"user_pref(\"keep\", \"SECOND EXTERNAL WORK\");\n":"{\"keep\":\"SECOND EXTERNAL WORK\"}";
    config_persistence::SetIniPublicationHookForTest(PublicationAttack,&attack);HookReset reset;
    if(mode>=8)config_persistence::FailIniPublicationForTest(mode==8?
        config_persistence::IniPublicationFailure::OldDisplaced:config_persistence::IniPublicationFailure::Installed);
    std::wstring details;
    const bool saved=firefox?firefox_preferences::UpdateStartupPreferenceFile(directory,true,&details):
        browser_preferences::UpdateStartupPreferencesFile(live,true,true,&details)==browser_preferences::FileUpdateResult::updated;
    std::cout<<"CASE "<<(firefox?"Firefox":"Chromium")<<" mode="<<mode<<" saved="<<saved<<" injected="<<attack.changed<<'\n';
    if(mode==0){
      std::string expected;
      if(firefox)firefox_preferences::UpdateStartupPreferenceText(original,true,expected);
      else browser_preferences::EditStartupPreferencesJson(original,true,true,expected);
      Check(saved&&Read(live)==expected,"Normal publication saves only the intended startup setting and preserves exact raw Unicode/newline bytes");
      Check(details.empty(),"Successful native publication has no spurious recovery warning");
      Check(std::distance(fs::directory_iterator(directory),fs::directory_iterator{})==1,"Normal publication removes only its own staging and recovery artifacts");
    }else{
      Check(!saved,"A changed, substituted or partially published preference transaction never reports success");
      Check(!details.empty(),"A rejected browser preference publication supplies actionable failure diagnostics");
      if(mode>=5)Check(details.find(L"Recovery:")!=std::wstring::npos,"Native substitution or partial failure reports the exact retained recovery location");
      if(mode<=7){
        Check(attack.attempted&&attack.changed,"The private concurrent writer reaches the actual staged/native publication boundary");
        Check(Contains(directory,attack.first),"The first newer writer remains byte-exact in the live file or retained recovery");
        if(mode==7)Check(attack.secondChanged&&Contains(directory,attack.second),"A second writer during rollback is retained independently without data loss");
        if(mode<=2)Check(Read(live)==attack.first,"Stage-time source changes remain the exact live preferences");
        if(mode==3)Check(Read(live)==original,"A substituted stage cannot change the original live preferences");
        if(mode==4||mode==7)Check(Read(live)==attack.first,"A late displaced writer is conditionally restored as the live preferences");
        if(mode==5||mode==6)Check(Contains(directory,original),"Rejected native-stage/post-publication substitution retains the original file for recovery");
      }else{
        Check(Read(live)==original,"Both partial native publication failures restore the exact original live bytes");
      }
    }
  }
}

static void CreationAndLimitCases(const fs::path &base){
  config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);
  const auto directory=base/L"creation";fs::create_directory(directory);
  const auto missing=directory/L"Preferences";
  Check(browser_preferences::UpdateStartupPreferencesFile(missing,true,true)==browser_preferences::FileUpdateResult::missing&&!fs::exists(missing),"A missing Chromium Preferences file remains missing and is never silently created");
  Check(firefox_preferences::UpdateStartupPreferenceFile(directory,true)&&Read(directory/L"user.js")=="user_pref(\"browser.startup.page\", 3);\r\n","Firefox legitimately creates its first user.js using the requested startup setting");
  const auto raceDirectory=base/L"creation-race";fs::create_directory(raceDirectory);
  Attack race;race.mode=10;race.first="user_pref(\"keep\", \"CREATED WHILE SAVING\");\n";
  config_persistence::SetIniPublicationHookForTest(PublicationAttack,&race);HookReset reset;
  Check(!firefox_preferences::UpdateStartupPreferenceFile(raceDirectory,true)&&race.changed&&Read(raceDirectory/L"user.js")==race.first,"A competing file appearing during Firefox first-save publication is never overwritten");
  config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);
  const auto raw=directory/L"raw";
  Check(Write(raw,"12345"),"Seed private raw maximum-input fixture");
  bool called=false;std::wstring details;
  Check(!config_persistence::MutateRawFileAtomically(raw,[&](const fs::path &,std::wstring &){called=true;return true;},4,false,&details)&&!called&&Read(raw)=="12345","The shared raw API rejects oversized input before invoking a mutator");
  Write(raw,"1234");
  Check(!config_persistence::MutateRawFileAtomically(raw,[&](const fs::path &stage,std::wstring &){return Write(stage,"12345");},4,false,&details)&&Read(raw)=="1234","The shared raw API rejects oversized staged output and preserves the original");
  const auto absent=directory/L"absent";called=false;
  Check(!config_persistence::MutateRawFileAtomically(absent,[&](const fs::path &,std::wstring &){called=true;return true;},4,false,&details)&&!called&&!fs::exists(absent),"The raw API's require-existing contract refuses missing targets before mutation");
}

int wmain(int argc,wchar_t **argv){try{
  if(argc!=2)return 2;std::cout<<std::unitbuf;
  const auto base=retained_fixture::CreateRoot(fs::absolute(fs::path(argv[1])),L"startup-publication");
  AdapterCases(base,false);AdapterCases(base,true);CreationAndLimitCases(base);
  std::cout<<"Startup preference publication: "<<checks<<" checks, "<<scenarios<<" adapter scenarios, "<<failures<<" failures; actual HWND/input/foreground/clipboard/browser/network=0.\n";
  return failures?1:0;
}catch(const std::exception &error){std::cerr<<"Startup preference publication setup failed: "<<error.what()<<'\n';return 2;}}
