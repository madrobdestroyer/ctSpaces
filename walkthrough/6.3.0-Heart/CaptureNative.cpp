// Record live pixels from explicitly allowed disposable application processes.
// No desktop, microphone, network, or unrelated window is captured.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include "../../ClientNotes.h"
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
static void H(HRESULT value) {if(FAILED(value)) {std::cerr<<"HRESULT "<<std::hex<<value<<std::endl;throw std::runtime_error("Native media operation failed");}}
struct CaptureWindows {std::vector<DWORD> pids;std::vector<HWND> windows;};
int wmain(int argc,wchar_t **argv) {
  if(argc==3 && !wcscmp(argv[1],L"--seed-demo")) {
    const auto destination=fs::weakly_canonical(argv[2]),build=fs::weakly_canonical(fs::current_path()/L"build");
    const auto relative=destination.lexically_relative(build);
    if(relative.empty() || *relative.begin()==L".." || !fs::exists(destination/L"video-fixture.marker")) return 2;
    for (const auto name : {L"Northstar Office (Demo)",L"Beacon Works (Demo)",L"Harbor Labs (Demo)",L"Riverview Services (Demo)"}) {
      const auto root=destination/L"Sites"/name;auto snapshot=client_notes::ReadNotebook(root);
      if (snapshot.status==client_notes::ReadStatus::Missing) {
        client_notes::NotePage rollout{L"Printer rollout",true,
          "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs24 "
          "\\b\\fs36 Printer rollout\\b0\\fs24\\par "
          "Keep the existing queue until the replacement passes its test print.\\par\\par "
          "\\b Completed\\b0\\par Driver staged on PRINT01.\\par Test queue created for reception.\\par\\par "
          "\\b Next action\\b0\\par Confirm the vendor window, then verify a print from reception.\\par\\par "
          "\\b Handover reminder\\b0\\par Record the owner and the result before closing the change.}",{}};
        rollout.id=1;rollout.active=true;rollout.created=rollout.modified=client_notes::Now();
        rollout.ticket=L"1042";rollout.ticketUrl=L"https://tickets.example.test/1042";
        rollout.status=2;rollout.tags={L"printing",L"rollout"};
        client_notes::NotePage contacts{L"Vendor contacts",true,
          "{\\rtf1\\ansi\\fs24 Vendor contact: Demo support team.\\par Next maintenance window: confirm with the client.}",{}};
        contacts.id=2;contacts.created=contacts.modified=rollout.created;
        if (!client_notes::WriteNotebook(root,{rollout,contacts},snapshot)) return 3;
      }
    }
    const auto client=destination/L"Sites/Beacon Works (Demo)";auto book=client_notes::ReadNotebook(client);
    if(book.status!=client_notes::ReadStatus::Ok) return 3;
    client_notes::NotePage page{L"Retired rollout draft",true,"{\\rtf1\\ansi Retired draft. The current rollout note contains the approved plan.}",{}};
    page.id=client_notes::NextId(book.pages);page.open=false;page.deleted=true;page.created=page.modified=client_notes::Now();
    auto pages=book.pages;pages.push_back(page);return client_notes::WriteNotebook(client,pages,book) ? 0:4;
  }
  if(argc!=5) {std::cerr<<"CaptureNative output.mp4 allowed-pids.txt stop-file ready-file\n";return 2;}
  try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const fs::path output=fs::absolute(argv[1]),allowed=fs::absolute(argv[2]),stop=fs::absolute(argv[3]),ready=fs::absolute(argv[4]);
    if(fs::exists(output) || output.parent_path()!=allowed.parent_path() || stop.parent_path()!=output.parent_path() || ready.parent_path()!=output.parent_path()) throw std::runtime_error("Use fresh files in one owned output directory");
    H(CoInitializeEx(nullptr,COINIT_MULTITHREADED));H(MFStartup(MF_VERSION));
    constexpr UINT width=1440,height=900,fps=20;constexpr LONGLONG step=10000000/fps;
    ComPtr<IMFSinkWriter> writer;ComPtr<IMFAttributes> attributes;H(MFCreateAttributes(&attributes,2));
    H(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE));
    H(MFCreateSinkWriterFromURL(output.c_str(),nullptr,attributes.Get(),&writer));
    ComPtr<IMFMediaType> format;H(MFCreateMediaType(&format));H(format->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));H(format->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264));
    H(format->SetUINT32(MF_MT_AVG_BITRATE,6000000));H(format->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
    H(MFSetAttributeSize(format.Get(),MF_MT_FRAME_SIZE,width,height));H(MFSetAttributeRatio(format.Get(),MF_MT_FRAME_RATE,fps,1));H(MFSetAttributeRatio(format.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
    DWORD stream=0;H(writer->AddStream(format.Get(),&stream));
    ComPtr<IMFMediaType> input;H(MFCreateMediaType(&input));H(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));H(input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32));
    H(input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));H(input->SetUINT32(MF_MT_DEFAULT_STRIDE,width*4));
    H(MFSetAttributeSize(input.Get(),MF_MT_FRAME_SIZE,width,height));H(MFSetAttributeRatio(input.Get(),MF_MT_FRAME_RATE,fps,1));H(MFSetAttributeRatio(input.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
    H(writer->SetInputMediaType(stream,input.Get(),nullptr));H(writer->BeginWriting());
    HDC screen=GetDC(nullptr),canvas=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-static_cast<LONG>(height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void *pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto prior=SelectObject(canvas,bitmap);
    HBRUSH background=CreateSolidBrush(RGB(18,19,23));RECT frame{0,0,width,height};
    const auto begin=std::chrono::steady_clock::now();std::ofstream(ready)<<"ready";
    UINT64 frames=0,changed=0;DWORD previousHash=0;LONGLONG lastTime=0;
    std::map<DWORD,std::shared_ptr<void>> identities;
    while(!fs::exists(stop) && frames<fps*900) {
      CaptureWindows collection;std::ifstream owners(allowed);DWORD pid;
      while(owners>>pid) {
        auto identity=identities.find(pid);
        if(identity==identities.end()) {
          HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!process)continue;
          identity=identities.emplace(pid,std::shared_ptr<void>(process,[](void *p){CloseHandle(p);})).first;
        }
        if(WaitForSingleObject(identity->second.get(),0)==WAIT_TIMEOUT)collection.pids.push_back(pid);
      }
      EnumWindows([](HWND window,LPARAM data)->BOOL {auto &c=*reinterpret_cast<CaptureWindows*>(data);DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(IsWindowVisible(window)&&!IsIconic(window)&&std::find(c.pids.begin(),c.pids.end(),pid)!=c.pids.end()) c.windows.push_back(window);return TRUE;},reinterpret_cast<LPARAM>(&collection));
      FillRect(canvas,&frame,background);
      RECT bounds{};bool hasBounds=false;
      for(HWND window:collection.windows) {RECT r{};GetWindowRect(window,&r);if(!hasBounds){bounds=r;hasBounds=true;}else UnionRect(&bounds,&bounds,&r);}
      const double scale=hasBounds ? (std::min)(2.0,(std::min)((width-80.0)/(std::max)(1L,bounds.right-bounds.left),(height-80.0)/(std::max)(1L,bounds.bottom-bounds.top))) : 1.0;
      const int originX=static_cast<int>((width-(bounds.right-bounds.left)*scale)/2),originY=static_cast<int>((height-(bounds.bottom-bounds.top)*scale)/2);
      SetStretchBltMode(canvas,HALFTONE);
      // Enumerate bottom to top, preserving actual owned popup/modal layering.
      for(auto it=collection.windows.rbegin();it!=collection.windows.rend();++it) {
        RECT r{};GetWindowRect(*it,&r);const int w=r.right-r.left,h=r.bottom-r.top;if(w<=0||h<=0||w>5000||h>5000)continue;
        HDC dc=CreateCompatibleDC(screen);HBITMAP tile=CreateCompatibleBitmap(screen,w,h);auto old=SelectObject(dc,tile);
        if(PrintWindow(*it,dc,2)) StretchBlt(canvas,originX+static_cast<int>((r.left-bounds.left)*scale),originY+static_cast<int>((r.top-bounds.top)*scale),static_cast<int>(w*scale),static_cast<int>(h*scale),dc,0,0,w,h,SRCCOPY);
        SelectObject(dc,old);DeleteObject(tile);DeleteDC(dc);
      }
      CURSORINFO cursor{sizeof(cursor)};if(GetCursorInfo(&cursor)&&cursor.flags==CURSOR_SHOWING) {
        POINT point=cursor.ptScreenPos;HWND under=WindowFromPoint(point);DWORD pid=0;GetWindowThreadProcessId(under,&pid);
        if(std::find(collection.pids.begin(),collection.pids.end(),pid)!=collection.pids.end()) {
          ICONINFO icon{};if(GetIconInfo(cursor.hCursor,&icon)) {DrawIconEx(canvas,originX+static_cast<int>((point.x-bounds.left-icon.xHotspot)*scale),originY+static_cast<int>((point.y-bounds.top-icon.yHotspot)*scale),cursor.hCursor,static_cast<int>(32*scale),static_cast<int>(32*scale),0,nullptr,DI_NORMAL);if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);}
        }
      }
      GdiFlush();DWORD hash=2166136261u;auto bytes=static_cast<unsigned char*>(pixels);for(size_t i=0;i<width*height*4;i+=59) hash=(hash^bytes[i])*16777619u;if(frames&&hash!=previousHash)++changed;previousHash=hash;
      ComPtr<IMFMediaBuffer> buffer;H(MFCreateMemoryBuffer(width*height*4,&buffer));BYTE *destination=nullptr;H(buffer->Lock(&destination,nullptr,nullptr));memcpy(destination,pixels,width*height*4);H(buffer->Unlock());H(buffer->SetCurrentLength(width*height*4));
      lastTime=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count()/100;
      ComPtr<IMFSample> sample;H(MFCreateSample(&sample));H(sample->AddBuffer(buffer.Get()));H(sample->SetSampleTime(lastTime));H(sample->SetSampleDuration(step));H(writer->WriteSample(stream,sample.Get()));++frames;
      const auto next=begin+std::chrono::milliseconds(frames*1000/fps);const auto now=std::chrono::steady_clock::now();if(next>now)Sleep(static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(next-now).count()));
    }
    H(writer->Finalize());DeleteObject(background);SelectObject(canvas,prior);DeleteObject(bitmap);DeleteDC(canvas);ReleaseDC(nullptr,screen);writer.Reset();MFShutdown();CoUninitialize();
    std::cout<<"{\"frames\":"<<frames<<",\"changedFrames\":"<<changed<<",\"nominalFps\":"<<fps<<",\"seconds\":"<<double(lastTime+step)/10000000<<",\"source\":\"live allowed application windows\"}\n";
    return frames && changed>0 ? 0:1;
  } catch(const std::exception &error) {std::cerr<<error.what()<<std::endl;return 1;}
}
