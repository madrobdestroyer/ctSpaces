#define NOMINMAX
#include "../NotesMarkdown.h"
#include "../ClientNotes.h"
#include <iostream>
#include <stdexcept>
static unsigned checks=0;
static void Check(bool condition,const char *label) {
  if(!condition)throw std::runtime_error(label);
  ++checks;std::cout<<"PASS "<<label<<std::endl;
}
namespace markdown_test {
static unsigned groups=0;
#include "NotesMarkdownCoreTests.inl"
}
int main() {
  try {
    markdown_test::Core();
    std::cout<<"Markdown core: "<<checks<<" checks; "<<markdown_test::groups<<" groups; 0 failures; windows/input/clipboard/browser=0.\n";return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<std::endl;return 1;}
}
