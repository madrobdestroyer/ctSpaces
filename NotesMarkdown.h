#pragma once

// A deliberately bounded, content-only Markdown interchange layer. The native
// client notebook remains the authority for identity, history and metadata.
// Unsupported Markdown is kept as literal text, with a preview warning.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace notes_markdown {
inline constexpr size_t kMaxCharacters=4*1024*1024;
inline constexpr size_t kMaxBlocks=100000;
inline constexpr size_t kMaxSpans=262144;
inline constexpr size_t kMaxRetainedUnits=8*1024*1024;
inline constexpr size_t kMaxLine=65536;
inline constexpr unsigned kMaxDepth=16;
enum Style : unsigned { Bold=1,Italic=2,Strike=4,Code=8 };
enum class Kind { Paragraph,Blank,Heading,Bullet,Numbered,Checklist,CodeBlock };
struct Span {
  std::wstring text,url;
  unsigned style=0;
  bool operator==(const Span &)const=default;
};
struct Block {
  Kind kind=Kind::Paragraph;
  unsigned level=0,number=1;
  bool checked=false;
  std::wstring language;
  std::vector<Span> spans;
  bool operator==(const Block &)const=default;
};
struct Document {
  std::vector<Block> blocks;
  bool trailingNewline=false;
  bool operator==(const Document &)const=default;
};
struct Result {
  bool ok=false;
  Document document;
  std::wstring error;
  std::vector<std::wstring> warnings;
};
inline bool SafeUrl(std::wstring_view url) {
  const auto equal=[](wchar_t left,wchar_t right){
    const auto fold=[](wchar_t c){return c>=L'A'&&c<=L'Z'?c+32:c;};
    return fold(left)==fold(right);
  };
  const auto starts=[&](std::wstring_view prefix){return url.size()>=prefix.size() && std::equal(prefix.begin(),prefix.end(),url.begin(),equal);};
  if(url.size()<9 || url.size()>2048 || (!starts(L"https://") && !starts(L"http://")))return false;
  const auto host=url.find(L"://")+3,hostEnd=url.find_first_of(L"/?#",host);
  if(host>=url.size() || host==hostEnd)return false;
  return std::none_of(url.begin(),url.end(),[](wchar_t c){return c<=0x20 || c==L'"' || c==L'<' || c==L'>' || c==L'\\';});
}
inline bool ValidText(std::wstring_view text) {
  if(text.size()>kMaxCharacters)return false;
  for(size_t i=0;i<text.size();++i) {
    const uint32_t c=static_cast<uint32_t>(text[i]);
    if((c<32 && c!=9 && c!=10 && c!=13) || c==127)return false;
    if constexpr(sizeof(wchar_t)==2) {
      if(c>=0xd800 && c<=0xdbff) {
        if(++i==text.size() || static_cast<uint32_t>(text[i])<0xdc00 || static_cast<uint32_t>(text[i])>0xdfff)return false;
      } else if(c>=0xdc00 && c<=0xdfff)return false;
    } else if(c>0x10ffff || (c>=0xd800 && c<=0xdfff))return false;
  }
  return true;
}
inline void Warn(Result &result,const std::wstring &warning) {
  if(std::find(result.warnings.begin(),result.warnings.end(),warning)==result.warnings.end())result.warnings.push_back(warning);
}
inline bool Escapable(wchar_t c) {return std::wstring_view(L"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~").find(c)!=std::wstring_view::npos;}
inline bool Space(wchar_t c){return c==L' ' || c==L'\t';}
inline bool Word(wchar_t c){return (c>=L'a'&&c<=L'z') || (c>=L'A'&&c<=L'Z') || (c>=L'0'&&c<=L'9') || c>=128;}
struct Parser {
  Result result;
  size_t budget=0,spanCount=0,retainedUnits=0;
  bool Spend(size_t cost) {
    if(cost>budget){result.error=L"This Markdown is too complex to preview safely. Import it as text or RTF instead.";return false;}
    budget-=cost;return true;
  }
  size_t Find(std::wstring_view source,std::wstring_view needle,size_t from) {
    for(size_t i=from;i+needle.size()<=source.size();++i) {
      if(!Spend(1))return std::wstring_view::npos;
      if(source[i]==L'\\'){++i;continue;}
      if(source.substr(i,needle.size())==needle)return i;
    }
    return std::wstring_view::npos;
  }
  bool Append(std::vector<Span> &spans,std::wstring text,unsigned style=0,std::wstring url={}) {
    if(text.empty())return true;
    const bool merge=!spans.empty() && spans.back().style==style && spans.back().url==url;
    const size_t retained=text.size()+(merge?0:url.size());
    if(retained>kMaxRetainedUnits-retainedUnits){result.error=L"This Markdown repeats too much link and formatting data to preview safely. Import it as text or RTF instead.";return false;}
    retainedUnits+=retained;
    if(merge)spans.back().text+=text;
    else {
      if(++spanCount>kMaxSpans){result.error=L"This Markdown has too many formatting runs to preview safely.";return false;}
      spans.push_back({std::move(text),std::move(url),style});
    }
    return true;
  }
  bool Inline(std::wstring_view source,std::vector<Span> &spans,unsigned style=0,const std::wstring &url={},unsigned depth=0) {
    if(depth>kMaxDepth){result.error=L"This Markdown has too many nested formatting levels.";return false;}
    std::wstring plain;
    const auto flush=[&](){if(plain.empty())return true;auto value=std::move(plain);plain.clear();return Append(spans,std::move(value),style,url);};
    for(size_t i=0;i<source.size();) {
      if(!Spend(1))return false;
      const wchar_t c=source[i];
      if(c==L'\\' && i+1<source.size() && Escapable(source[i+1])){plain+=source[i+1];i+=2;continue;}
      if(c==L'`') {
        size_t ticks=1;while(i+ticks<source.size() && source[i+ticks]==L'`')++ticks;
        const auto end=Find(source,std::wstring_view(source.data()+i,ticks),i+ticks);
        if(end!=std::wstring_view::npos) {
          if(!flush())return false;
          auto value=source.substr(i+ticks,end-i-ticks);
          if(value.size()>=2 && value.front()==L' ' && value.back()==L' ' && value.find_first_not_of(L' ')!=std::wstring_view::npos)value=value.substr(1,value.size()-2);
          if(!Append(spans,std::wstring(value),style|Code,url))return false;
          i=end+ticks;continue;
        }
      }
      if(c==L'[' && url.empty() && (i==0 || source[i-1]!=L'!')) {
        const auto close=Find(source,L"](",i+1);
        if(close!=std::wstring_view::npos) {
          size_t end=close+2;unsigned nesting=1;
          for(;end<source.size();++end) {
            if(!Spend(1))return false;
            if(source[end]==L'\\' && end+1<source.size()){++end;continue;}
            if(source[end]==L'(' && ++nesting>8)break;
            if(source[end]==L')' && --nesting==0)break;
          }
          if(end<source.size() && nesting==0) {
            std::wstring destination(source.substr(close+2,end-close-2));
            if(destination.size()>=2 && destination.front()==L'<' && destination.back()==L'>')destination=destination.substr(1,destination.size()-2);
            std::wstring unescaped;
            for(size_t p=0;p<destination.size();++p){if(destination[p]==L'\\' && p+1<destination.size() && Escapable(destination[p+1]))++p;unescaped+=destination[p];}
            if(SafeUrl(unescaped)) {
              if(!flush() || !Inline(source.substr(i+1,close-i-1),spans,style,unescaped,depth+1))return false;
              i=end+1;continue;
            }
            Warn(result,L"A link with an unsupported or unsafe destination was kept as literal text; only HTTP/HTTPS links become clickable.");
            plain+=source.substr(i,end-i+1);i=end+1;continue;
          }
        }
      }
      std::wstring_view delimiter;unsigned effect=0;
      if(source.substr(i,3)==L"***" || source.substr(i,3)==L"___"){delimiter=source.substr(i,3);effect=Bold|Italic;}
      else if(source.substr(i,2)==L"**" || source.substr(i,2)==L"__"){delimiter=source.substr(i,2);effect=Bold;}
      else if(source.substr(i,2)==L"~~"){delimiter=L"~~";effect=Strike;}
      else if(c==L'*' || c==L'_'){delimiter=source.substr(i,1);effect=Italic;}
      if(!delimiter.empty() && delimiter.front()==L'_' && i && (Word(source[i-1]) || source[i-1]==L'_'))delimiter={};
      if(!delimiter.empty() && i+delimiter.size()<source.size() && !Space(source[i+delimiter.size()])) {
        auto end=Find(source,delimiter,i+delimiter.size());
        while(end!=std::wstring_view::npos && delimiter.front()==L'_' && end+delimiter.size()<source.size() && Word(source[end+delimiter.size()]))end=Find(source,delimiter,end+delimiter.size());
        if(end!=std::wstring_view::npos && end>i+delimiter.size() && !Space(source[end-1])) {
          if(!flush() || !Inline(source.substr(i+delimiter.size(),end-i-delimiter.size()),spans,style|effect,url,depth+1))return false;
          i=end+delimiter.size();continue;
        }
      }
      plain+=c;++i;
    }
    return flush() && result.error.empty();
  }
};
inline Result Parse(std::wstring_view source) {
  Parser parser;parser.budget=source.size()*48+1024;
  if(!ValidText(source)){parser.result.error=L"Markdown must contain valid text without embedded control characters and be no larger than 4 million characters.";return parser.result;}
  std::wstring text;text.reserve(source.size());
  for(size_t i=0;i<source.size();++i){if(source[i]==L'\r'){text+=L'\n';if(i+1<source.size() && source[i+1]==L'\n')++i;}else text+=source[i];}
  parser.result.document.trailingNewline=!text.empty() && text.back()==L'\n';
  std::vector<std::wstring_view> lines;
  for(size_t first=0;first<text.size();) {
    const auto end=text.find(L'\n',first),length=(end==std::wstring::npos?text.size():end)-first;
    if(length>kMaxLine || lines.size()>=kMaxBlocks){parser.result.error=L"This Markdown has an overlong line or too many paragraphs to preview safely.";return parser.result;}
    lines.emplace_back(text.data()+first,length);if(end==std::wstring::npos)break;first=end+1;
  }
  for(size_t line=0;line<lines.size();++line) {
    auto content=lines[line];Block block;
    if(content.empty()){block.kind=Kind::Blank;parser.result.document.blocks.push_back(std::move(block));continue;}
    size_t fence=0;
    if(content.front()==L'`' || content.front()==L'~')while(fence<content.size() && content[fence]==content.front())++fence;
    if(fence>=3) {
      size_t close=line+1;
      for(;close<lines.size();++close) {
        auto candidate=lines[close];size_t run=0;
        while(run<candidate.size() && candidate[run]==content.front())++run;
        if(run>=fence && candidate.find_first_not_of(L" \t",run)==std::wstring_view::npos)break;
      }
      const auto language=content.substr(fence);
      const bool safeLanguage=language.size()<=48 && std::all_of(language.begin(),language.end(),[](wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'+'||c==L'#'||c==L'-'||c==L'_';});
      if(close<lines.size() && safeLanguage) {
        block.kind=Kind::CodeBlock;block.language=language;std::wstring value;
        for(size_t p=line+1;p<close;++p){if(p>line+1)value+=L'\n';value+=lines[p];}
        parser.Append(block.spans,std::move(value),Code);parser.result.document.blocks.push_back(std::move(block));line=close;continue;
      }
      Warn(parser.result,L"An unclosed code fence or unsupported fence label was kept as literal text.");
      const size_t last=close<lines.size()?close:lines.size()-1;
      for(size_t raw=line;raw<=last;++raw) {
        Block literal;literal.kind=lines[raw].empty()?Kind::Blank:Kind::Paragraph;
        if(!parser.Append(literal.spans,std::wstring(lines[raw])))return parser.result;
        parser.result.document.blocks.push_back(std::move(literal));
      }
      line=last;continue;
    }
    size_t prefix=0;
    while(prefix<content.size() && content[prefix]==L'#')++prefix;
    if(prefix>=1 && prefix<=6 && prefix<content.size() && content[prefix]==L' ') {block.kind=Kind::Heading;block.level=static_cast<unsigned>(prefix);content.remove_prefix(prefix+1);}
    else if(content.size()>=2 && (content[0]==L'-'||content[0]==L'*'||content[0]==L'+') && content[1]==L' ') {
      block.kind=Kind::Bullet;content.remove_prefix(2);
      if(content.size()>=4 && content[0]==L'[' && (content[1]==L' '||content[1]==L'x'||content[1]==L'X') && content[2]==L']' && content[3]==L' ') {block.kind=Kind::Checklist;block.checked=content[1]!=L' ';content.remove_prefix(4);}
    } else {
      size_t digits=0;unsigned number=0;
      while(digits<content.size() && digits<6 && content[digits]>=L'0' && content[digits]<=L'9'){number=number*10+content[digits]-L'0';++digits;}
      if(digits && number && digits+1<content.size() && content[digits]==L'.' && content[digits+1]==L' '){block.kind=Kind::Numbered;block.number=number;content.remove_prefix(digits+2);}
    }
    if(content.starts_with(L"> ") || parser.Find(content,L"<",0)!=std::wstring_view::npos || content.find(L"![")!=std::wstring_view::npos || content.starts_with(L"    ") || content.starts_with(L"\t") || content.find(L"]:")!=std::wstring_view::npos || content.find(L"|---")!=std::wstring_view::npos || content.find(L"| ---")!=std::wstring_view::npos) {
      Warn(parser.result,L"Block quotes, HTML, images, nested/indented lists, reference links and Markdown tables are kept as literal text in this importer.");
      if(!parser.Append(block.spans,std::wstring(content)))return parser.result;
    } else if(!parser.Inline(content,block.spans))return parser.result;
    parser.result.document.blocks.push_back(std::move(block));
  }
  parser.result.ok=parser.result.error.empty();return std::move(parser.result);
}
inline std::wstring Escape(std::wstring_view value) {
  std::wstring escaped;for(wchar_t c:value){if(Escapable(c))escaped+=L'\\';escaped+=c;}return escaped;
}
inline size_t LongestRun(std::wstring_view text,wchar_t marker) {
  size_t longest=0,current=0;for(wchar_t c:text){current=c==marker?current+1:0;longest=(std::max)(longest,current);}return longest;
}
inline Document Canonical(Document document) {
  for(auto &block:document.blocks) {
    if(block.kind!=Kind::Heading)block.level=0;
    if(block.kind!=Kind::Numbered)block.number=1;
    if(block.kind!=Kind::Checklist)block.checked=false;
    if(block.kind!=Kind::CodeBlock)block.language.clear();
    std::vector<Span> runs;
    for(auto &span:block.spans) {
      if(span.text.empty())continue;
      if(block.kind==Kind::CodeBlock)span.style|=Code;
      if(!runs.empty() && runs.back().style==span.style && runs.back().url==span.url)runs.back().text+=span.text;
      else runs.push_back(std::move(span));
    }
    block.spans=std::move(runs);
  }
  return document;
}
inline bool Write(const Document &document,std::wstring &markdown,std::wstring &error) {
  markdown.clear();error.clear();
  if(document.blocks.size()>kMaxBlocks){error=L"Too many Markdown paragraphs.";return false;}
  for(size_t index=0;index<document.blocks.size();++index) {
    const auto &block=document.blocks[index];if(index)markdown+=L'\n';
    if(block.kind==Kind::Heading){if(block.level<1||block.level>6){error=L"Invalid heading level.";return false;}markdown+=std::wstring(block.level,L'#')+L' ';}
    else if(block.kind==Kind::Bullet)markdown+=L"- ";
    else if(block.kind==Kind::Numbered){if(!block.number||block.number>999999){error=L"Invalid list number.";return false;}markdown+=std::to_wstring(block.number)+L". ";}
    else if(block.kind==Kind::Checklist)markdown+=block.checked?L"- [x] ":L"- [ ] ";
    if(block.kind==Kind::CodeBlock) {
      if(block.spans.size()>1){error=L"A fenced code block must contain one plain source run.";return false;}
      const std::wstring_view source=block.spans.empty()?std::wstring_view{}:block.spans[0].text;
      const std::wstring fence((std::max)(size_t{3},LongestRun(source,L'`')+1),L'`');
      markdown+=fence+block.language+L'\n';markdown+=source;markdown+=L'\n'+fence;
    } else for(const auto &span:block.spans) {
      if(!ValidText(span.text) || span.text.find_first_of(L"\r\n")!=std::wstring::npos || (span.style&~15u) || (!span.url.empty() && !SafeUrl(span.url))) {error=L"This formatting run cannot be represented safely as Markdown.";return false;}
      if(!span.url.empty())markdown+=L'[';
      if(span.style&Bold)markdown+=L"**";if(span.style&Italic)markdown+=L'*';if(span.style&Strike)markdown+=L"~~";
      if(span.style&Code) {
        const std::wstring fence(LongestRun(span.text,L'`')+1,L'`');const bool padded=span.text.find_first_not_of(L' ')!=std::wstring::npos;
        markdown+=fence;if(padded)markdown+=L' ';markdown+=span.text;if(padded)markdown+=L' ';markdown+=fence;
      } else markdown+=Escape(span.text);
      if(span.style&Strike)markdown+=L"~~";if(span.style&Italic)markdown+=L'*';if(span.style&Bold)markdown+=L"**";
      if(!span.url.empty())markdown+=L"]("+Escape(span.url)+L')';
    }
    if(markdown.size()>kMaxCharacters){markdown.clear();error=L"The Markdown export exceeds 4 million characters.";return false;}
  }
  if(document.trailingNewline)markdown+=L'\n';
  // Delimiters next to adjacent runs, leading spaces or backticks can change
  // meaning in Markdown. Refuse an unrepresentable combination rather than
  // returning a subtly different document and silently dropping formatting.
  const auto checked=Parse(markdown);
  if(!checked.ok || Canonical(checked.document)!=Canonical(document)) {
    markdown.clear();error=L"These text/formatting boundaries cannot be preserved safely as Markdown. Use RTF or simplify the formatting.";return false;
  }
  return true;
}
inline std::string RtfText(std::wstring_view text) {
  std::string output;
  const auto unit=[&](uint16_t c){output+="\\u"+std::to_string(static_cast<int16_t>(c))+"?";};
  for(wchar_t c:text) {
    if(c==L'\n'||c==L'\r')output+="\\line ";else if(c==L'\t')output+="\\tab ";
    else if(c==L'\\'||c==L'{'||c==L'}'){output+='\\';output+=static_cast<char>(c);}
    else if(c>=32&&c<127)output+=static_cast<char>(c);
    else if(static_cast<uint32_t>(c)<=0xffff)unit(static_cast<uint16_t>(c));
    else {const auto scalar=static_cast<uint32_t>(c)-0x10000;unit(static_cast<uint16_t>(0xd800+(scalar>>10)));unit(static_cast<uint16_t>(0xdc00+(scalar&1023)));}
  }
  return output;
}
struct Link {uint32_t first=0,last=0;std::wstring label,url;};
struct Rendered {bool ok=false;std::string rtf;std::wstring plain,error;std::vector<Link> links;};
inline Rendered Render(const Document &document) {
  Rendered output;output.rtf="{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}}{\\colortbl;\\red60\\green145\\blue195;}\\uc1 ";
  if(document.blocks.size()>kMaxBlocks){output.error=L"Too many Markdown paragraphs.";return output;}
  size_t spans=0;
  for(size_t index=0;index<document.blocks.size();++index) {
    const auto &block=document.blocks[index];if(index){output.rtf+="\\par ";output.plain+=L'\r';}
    output.rtf+="\\pard\\plain\\f0\\fs24\\sb0\\sa0\\sl240\\slmult1 ";
    if(block.kind==Kind::Heading)output.rtf+="\\b\\fs"+std::to_string(block.level==1?40:block.level==2?32:28)+" ";
    std::wstring prefix;
    if(block.kind==Kind::Bullet)prefix=L"\x2022 ";
    else if(block.kind==Kind::Numbered)prefix=std::to_wstring(block.number)+L". ";
    else if(block.kind==Kind::Checklist)prefix=block.checked?L"\x2611\x2003":L"\x2610\x2003";
    output.rtf+=RtfText(prefix);output.plain+=prefix;
    if(block.kind==Kind::CodeBlock && !block.language.empty()) {
      if(block.language.size()>48 || !ValidText(block.language)){output.error=L"The code language label is invalid.";return output;}
      output.rtf+="{\\b\\f0 "+RtfText(block.language)+" - Code}\\line ";
      output.plain+=block.language+L" - Code\r";
    }
    if(output.plain.size()>kMaxCharacters){output.error=L"This Markdown exceeds the note text limit.";return output;}
    for(const auto &span:block.spans) {
      if(++spans>kMaxSpans || span.text.size()>kMaxCharacters-output.plain.size() || !ValidText(span.text) || (!span.url.empty()&&!SafeUrl(span.url)) || (span.style&~15u)) {output.error=L"Invalid or excessive Markdown formatting or link destination.";return output;}
      unsigned style=span.style;if(block.kind==Kind::Heading)style|=Bold;if(block.kind==Kind::CodeBlock)style|=Code;
      if(block.kind==Kind::Checklist && block.checked)style|=Strike;
      output.rtf+="{\\b"+std::to_string(bool(style&Bold))+"\\i"+std::to_string(bool(style&Italic))+"\\strike"+std::to_string(bool(style&Strike))+"\\f"+std::to_string(bool(style&Code));
      if(!span.url.empty())output.rtf+="\\ul\\cf1";
      output.rtf+=' ';output.rtf+=RtfText(span.text);output.rtf+='}';
      const auto first=output.plain.size();
      for(wchar_t c:span.text)output.plain+=c==L'\n'?L'\r':c;
      if(!span.url.empty())output.links.push_back({static_cast<uint32_t>(first),static_cast<uint32_t>(output.plain.size()),span.text,span.url});
      if(output.rtf.size()>64*1024*1024 || output.links.size()>16384){output.error=L"This Markdown exceeds the native note or link limits.";return output;}
    }
    if(output.plain.size()>kMaxCharacters || output.rtf.size()>64*1024*1024 || output.links.size()>16384){output.error=L"This Markdown exceeds the native note or link limits.";return output;}
  }
  // RichEdit consumes the last RTF paragraph as its mandatory story terminator.
  // A second paragraph keeps an explicitly requested trailing newline visible.
  if(document.trailingNewline){output.rtf+="\\par\\par ";output.plain+=L'\r';}
  if(output.plain.size()>kMaxCharacters){output.error=L"This Markdown exceeds the note text limit.";return output;}
  output.rtf+='}';output.ok=true;return output;
}
inline Document LiteralDocument(std::wstring_view text) {
  Document document;
  std::wstring normalized;
  for(size_t i=0;i<text.size();++i){if(text[i]==L'\r'){normalized+=L'\n';if(i+1<text.size() && text[i+1]==L'\n')++i;}else normalized+=text[i];}
  document.trailingNewline=!normalized.empty()&&normalized.back()==L'\n';
  for(size_t first=0;first<normalized.size();) {
    const auto end=normalized.find(L'\n',first),last=end==std::wstring::npos?normalized.size():end;
    Block block;block.kind=last==first?Kind::Blank:Kind::Paragraph;if(last!=first)block.spans.push_back({normalized.substr(first,last-first),{},0});document.blocks.push_back(std::move(block));
    if(end==std::wstring::npos)break;first=end+1;
  }
  return document;
}
inline std::string TableRtf(unsigned rows,unsigned columns) {
  if(!rows || !columns || rows>12 || columns>12)return {};
  std::string rtf="{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}{\\colortbl;\\red128\\green128\\blue128;}\\uc1\\pard\\plain\\f0\\fs24\\par ";
  for(unsigned row=0;row<rows;++row) {
    rtf+="\\trowd\\trgaph90\\trleft0\\trrh360";
    for(unsigned column=0;column<columns;++column) {
      rtf+="\\clbrdrt\\brdrs\\brdrw10\\brdrcf1\\clbrdrl\\brdrs\\brdrw10\\brdrcf1\\clbrdrb\\brdrs\\brdrw10\\brdrcf1\\clbrdrr\\brdrs\\brdrw10\\brdrcf1\\cellx"+std::to_string((column+1)*7200/columns);
    }
    for(unsigned column=0;column<columns;++column)rtf+="\\pard\\intbl\\plain\\f0\\fs24\\sb0\\sa0 \\cell ";
    rtf+="\\row ";
  }
  return rtf+"\\pard\\plain\\f0\\fs24\\par }";
}
} // namespace notes_markdown
